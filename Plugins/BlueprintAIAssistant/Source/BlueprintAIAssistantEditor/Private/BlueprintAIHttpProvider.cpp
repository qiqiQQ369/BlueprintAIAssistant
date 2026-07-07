#include "BlueprintAIHttpProvider.h"

#include "BlueprintAIAssistantSettings.h"
#include "BlueprintAIUsageLogger.h"
#include "Async/Async.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/CriticalSection.h"
#include "HttpModule.h"
#include "Serialization/Archive.h"
#include "Interfaces/IHttpBase.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Logging/LogMacros.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

/** URL 含 /responses 时使用 Ark Responses 的 input 结构；否则使用 OpenAI 兼容的 chat/completions（messages）。 */
static bool ShouldUseArkResponsesPayload(const FString& EndpointUrl)
{
	return EndpointUrl.Contains(TEXT("/responses"), ESearchCase::IgnoreCase);
}

/** Google Generative Language API generateContent */
static TSharedPtr<FJsonObject> BuildGeminiGenerateContentPayload(const FBlueprintAIRequest& Request)
{
	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();

	{
		TSharedPtr<FJsonObject> SysInst = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> SysParts;
		{
			TSharedPtr<FJsonObject> PartObj = MakeShared<FJsonObject>();
			PartObj->SetStringField(TEXT("text"), Request.SystemPrompt);
			SysParts.Add(MakeShared<FJsonValueObject>(PartObj));
		}
		SysInst->SetArrayField(TEXT("parts"), SysParts);
		RootObject->SetObjectField(TEXT("systemInstruction"), SysInst);
	}

	TArray<TSharedPtr<FJsonValue>> Contents;
	{
		TSharedPtr<FJsonObject> UserTurn = MakeShared<FJsonObject>();
		UserTurn->SetStringField(TEXT("role"), TEXT("user"));
		TArray<TSharedPtr<FJsonValue>> Parts;
		{
			TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
			TextPart->SetStringField(TEXT("text"), Request.UserPrompt);
			Parts.Add(MakeShared<FJsonValueObject>(TextPart));
		}
		UserTurn->SetArrayField(TEXT("parts"), Parts);
		Contents.Add(MakeShared<FJsonValueObject>(UserTurn));
	}
	RootObject->SetArrayField(TEXT("contents"), Contents);

	{
		const int32 MaxOut = (Request.MaxOutputTokens > 0) ? FMath::Clamp(Request.MaxOutputTokens, 256, 65536) : 8192;
		TSharedPtr<FJsonObject> GenCfg = MakeShared<FJsonObject>();
		GenCfg->SetNumberField(TEXT("temperature"), 0.7);
		GenCfg->SetNumberField(TEXT("maxOutputTokens"), MaxOut);
		RootObject->SetObjectField(TEXT("generationConfig"), GenCfg);
	}

	return RootObject;
}

static bool TryExtractTextFromGemini(const TSharedPtr<FJsonObject>& JsonObject, FString& OutText)
{
	const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
	if (!JsonObject->TryGetArrayField(TEXT("candidates"), Candidates) || !Candidates || Candidates->Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Cand0 = nullptr;
	if (!(*Candidates)[0]->TryGetObject(Cand0) || !Cand0 || !Cand0->IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* ContentObj = nullptr;
	if (!(*Cand0)->TryGetObjectField(TEXT("content"), ContentObj) || !ContentObj || !ContentObj->IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
	if (!(*ContentObj)->TryGetArrayField(TEXT("parts"), Parts) || !Parts)
	{
		return false;
	}

	FString Combined;
	for (const TSharedPtr<FJsonValue>& PartVal : *Parts)
	{
		const TSharedPtr<FJsonObject>* PartObj = nullptr;
		if (!PartVal.IsValid() || !PartVal->TryGetObject(PartObj) || !PartObj || !PartObj->IsValid())
		{
			continue;
		}

		FString Text;
		if ((*PartObj)->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
		{
			if (!Combined.IsEmpty())
			{
				Combined += TEXT("\n");
			}
			Combined += Text;
		}
	}

	if (Combined.IsEmpty())
	{
		return false;
	}

	OutText = Combined;
	return true;
}

static FString RedactUrlQueryKey(const FString& Url)
{
	FString Out = Url;
	int32 KeyIdx = Out.Find(TEXT("key="), ESearchCase::IgnoreCase);
	if (KeyIdx != INDEX_NONE)
	{
		Out = Out.Left(KeyIdx + 4) + TEXT("(hidden)");
	}
	return Out;
}

/** 将 HTTP 流式下载累计到内存；Serialize 由 libcurl/平台层在 IO 线程调用，需自锁。 */
struct FSseBufferArchive final : public FArchive
{
	/** 用于在 const 读路径（如 ToFString）里加锁 */
	mutable FCriticalSection Mutex;
	TArray<uint8> Bytes;

	FSseBufferArchive()
	{
		this->SetIsPersistent(false);
	}

	virtual void Serialize(void* V, int64 Length) override
	{
		if (!V || Length <= 0)
		{
			return;
		}
		FScopeLock Lock(&Mutex);
		const int32 ILen = static_cast<int32>(Length);
		const int32 Old = Bytes.Num();
		Bytes.SetNumUninitialized(Old + ILen, EAllowShrinking::No);
		FMemory::Memcpy(Bytes.GetData() + Old, V, ILen);
	}

	FString ToFString() const
	{
		FScopeLock Lock(&Mutex);
		if (Bytes.Num() == 0)
		{
			return FString();
		}
		const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		return FString(Conv.Length(), Conv.Get());
	}
};

/**
 * 解析 OpenAI 兼容的 chat/completions SSE：每行 data: {json}，拼 choices[].delta.content。
 * @return true 表示至少解析到一条 data: 行（可视为流式体）；OutText 可能仍为空（仅 role 等元数据 chunk）。
 */
static bool TryParseOpenAIChatSseToText(const FString& SseBody, FString& OutText)
{
	OutText.Reset();
	bool bSawDataLine = false;
	TArray<FString> Lines;
	SseBody.ParseIntoArrayLines(Lines, false);
	for (FString& Line : Lines)
	{
		Line.TrimStartInline();
		if (Line.IsEmpty())
		{
			continue;
		}
		// SSE 规范里的注释行
		if (Line[0] == TEXT(':'))
		{
			continue;
		}
		if (!Line.StartsWith(TEXT("data:"), ESearchCase::CaseSensitive))
		{
			continue;
		}
		bSawDataLine = true;
		const FString Payload = Line.Mid(5).TrimStart();
		if (Payload == TEXT("[DONE]"))
		{
			break;
		}
		if (Payload.IsEmpty())
		{
			continue;
		}
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Payload);
		if (!FJsonSerializer::Deserialize(R, Obj) || !Obj.IsValid())
		{
			continue;
		}
		// 流里也可能出现 error
		const TSharedPtr<FJsonObject>* Err = nullptr;
		if (Obj->TryGetObjectField(TEXT("error"), Err) && Err && Err->IsValid())
		{
			FString Msg;
			(*Err)->TryGetStringField(TEXT("message"), Msg);
			if (Msg.IsEmpty())
			{
				Msg = TEXT("stream error object");
			}
			OutText = TEXT("[stream error] ") + Msg;
			return true;
		}
		const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
		if (!Obj->TryGetArrayField(TEXT("choices"), Choices) || !Choices)
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& Ch : *Choices)
		{
			const TSharedPtr<FJsonObject>* CObj = nullptr;
			if (!Ch.IsValid() || !Ch->TryGetObject(CObj) || !CObj || !CObj->IsValid())
			{
				continue;
			}
			const TSharedPtr<FJsonObject>* Delta = nullptr;
			if ((*CObj)->TryGetObjectField(TEXT("delta"), Delta) && Delta && Delta->IsValid())
			{
				FString Part;
				if ((*Delta)->TryGetStringField(TEXT("content"), Part) && !Part.IsEmpty())
				{
					OutText += Part;
				}
			}
		}
	}
	return bSawDataLine;
}

/** 与 Epic 的 bProcessedSuccessfully 同义：true=引擎认为 HTTP 已走完收发流程，但不等于业务成功。 */
static FString BuildHttpTransportDebugLine(
	FHttpRequestPtr RequestPtr,
	FHttpResponsePtr HttpResponse,
	const bool bProcessedSuccessfully)
{
	FString S;
	S += FString::Printf(TEXT("bProcessedOk=%s"), bProcessedSuccessfully ? TEXT("true") : TEXT("false"));
	if (RequestPtr.IsValid())
	{
		S += FString::Printf(
			TEXT(" | reqStatus=%s | reqFailure=%s"),
			EHttpRequestStatus::ToString(RequestPtr->GetStatus()),
			LexToString(RequestPtr->GetFailureReason()));
	}
	else
	{
		S += TEXT(" | requestPtr=invalid");
	}
	if (HttpResponse.IsValid())
	{
		const int32 Recv = static_cast<int32>(HttpResponse->GetContent().Num());
		S += FString::Printf(
			TEXT(" | httpCode=%d | recvBytes=%d | respStatus=%s | respFailure=%s"),
			HttpResponse->GetResponseCode(),
			Recv,
			EHttpRequestStatus::ToString(HttpResponse->GetStatus()),
			LexToString(HttpResponse->GetFailureReason()));
	}
	else
	{
		S += TEXT(" | httpResponse=invalid");
	}
	return S;
}

static FString GetFailureReasonChineseBlurb(const EHttpFailureReason Reason)
{
	switch (Reason)
	{
	case EHttpFailureReason::TimedOut:
		return TEXT(
			"类型：总超时/未完成（TimedOut）。\n"
			"说明：在 Project Settings 配置的总等待时间用尽仍未完整收完响应。可能与模型过慢、网关/反代「空闲超时」、客户端 Timeout 过短、或回包在传输中中断有关。若账单仍产生，说明请求可能已到达上游。");
	case EHttpFailureReason::ConnectionError:
		return TEXT(
			"类型：连接错误（ConnectionError）。\n"
			"说明：更偏 TLS/网络/代理/DNS/对端重置/防火墙/证书问题。若更换供应商后仍无规律地复现，可优先查本机代理、VPN、公司网与系统证书，而非只换模型。");
	case EHttpFailureReason::Cancelled:
		return TEXT("类型：已取消（Cancelled）。说明：请求在引擎侧被中止。");
	case EHttpFailureReason::Other:
		return TEXT("类型：其它（Other）。说明：平台 HTTP 层报告的其它未细分原因。请结合 httpDetail/usage 日志与「导出 HTTP」。");
	case EHttpFailureReason::None:
	default:
		return TEXT("类型：未从引擎取到有效 FailureReason（或为空）。可查看下方「引擎细项」与 usage 中 httpDetail 字段。");
	}
}

static bool IsSensitiveKeyName(const FString& Key)
{
	const FString K = Key.ToLower();
	return K.Contains(TEXT("api_key")) ||
		K.Equals(TEXT("key")) ||
		K.Contains(TEXT("token")) ||
		K.Contains(TEXT("authorization")) ||
		K.Contains(TEXT("secret"));
}

static void RedactJsonValueInPlace(const TSharedPtr<FJsonValue>& V);

static void RedactJsonObjectInPlace(const TSharedPtr<FJsonObject>& Obj)
{
	if (!Obj.IsValid())
	{
		return;
	}

	for (auto& Pair : Obj->Values)
	{
		const FString& Key = Pair.Key;
		TSharedPtr<FJsonValue>& Val = Pair.Value;
		if (!Val.IsValid())
		{
			continue;
		}

		if (IsSensitiveKeyName(Key))
		{
			// 用固定占位覆盖敏感字段
			Val = MakeShared<FJsonValueString>(TEXT("(hidden)"));
			continue;
		}

		RedactJsonValueInPlace(Val);
	}
}

static void RedactJsonValueInPlace(const TSharedPtr<FJsonValue>& V)
{
	if (!V.IsValid())
	{
		return;
	}
	switch (V->Type)
	{
	case EJson::Object:
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (V->TryGetObject(Obj) && Obj && Obj->IsValid())
		{
			RedactJsonObjectInPlace(*Obj);
		}
		break;
	}
	case EJson::Array:
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (V->TryGetArray(Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& Item : *Arr)
			{
				RedactJsonValueInPlace(Item);
			}
		}
		break;
	}
	default:
		break;
	}
}

struct FLastHttpDebugInfo
{
	FCriticalSection Mutex;
	FString ProviderKind;
	FString Model;
	FString EndpointSafe;
	FString TopLevelKeys;
	FString Extractor;
	FString RedactedBody; // 尽量是 JSON（脱敏后）
	FDateTime Timestamp;
};

static FLastHttpDebugInfo& GetLastHttpDebugInfo()
{
	static FLastHttpDebugInfo Info;
	return Info;
}

static void StoreLastHttpDebugInfo(
	const FString& ProviderKind,
	const FString& Model,
	const FString& EndpointSafe,
	const FString& TopLevelKeys,
	const FString& Extractor,
	const FString& RedactedBody)
{
	FLastHttpDebugInfo& I = GetLastHttpDebugInfo();
	FScopeLock Lock(&I.Mutex);
	I.ProviderKind = ProviderKind;
	I.Model = Model;
	I.EndpointSafe = EndpointSafe;
	I.TopLevelKeys = TopLevelKeys;
	I.Extractor = Extractor;
	I.RedactedBody = RedactedBody;
	I.Timestamp = FDateTime::Now();
}

static bool WriteHttpDumpFile(
	const FString& ProviderKind,
	const FString& Model,
	const FString& EndpointSafe,
	const FString& TopLevelKeys,
	const FString& Extractor,
	const FString& RedactedBody,
	FString& OutFilePath,
	FString& OutError)
{
	const FString RootDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BlueprintAIAssistant") / TEXT("http-dumps"));
	IFileManager::Get().MakeDirectory(*RootDir, /*Tree*/true);

	const FDateTime Now = FDateTime::Now();
	const FString FileName = FString::Printf(
		TEXT("http-dump-%04d%02d%02d-%02d%02d%02d.json"),
		Now.GetYear(), Now.GetMonth(), Now.GetDay(),
		Now.GetHour(), Now.GetMinute(), Now.GetSecond());
	const FString FilePath = RootDir / FileName;

	FString Text;
	Text += FString::Printf(TEXT("{\"ts\":\"%s\",\"provider\":\"%s\",\"model\":\"%s\",\"endpoint\":\"%s\",\"topKeys\":\"%s\",\"extractor\":\"%s\",\"body\":"),
		*Now.ToIso8601(),
		*ProviderKind.ReplaceCharWithEscapedChar(),
		*Model.ReplaceCharWithEscapedChar(),
		*EndpointSafe.ReplaceCharWithEscapedChar(),
		*TopLevelKeys.ReplaceCharWithEscapedChar(),
		*Extractor.ReplaceCharWithEscapedChar());
	// body：尽量保持可读（如果已经是 JSON 就直接拼；否则包成字符串）
	if (!RedactedBody.IsEmpty() && (RedactedBody.StartsWith(TEXT("{")) || RedactedBody.StartsWith(TEXT("["))))
	{
		Text += RedactedBody;
	}
	else
	{
		Text += FString::Printf(TEXT("\"%s\""), *RedactedBody.ReplaceCharWithEscapedChar());
	}
	Text += TEXT("}\n");

	const bool bOk = FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (!bOk)
	{
		OutError = FString::Printf(TEXT("写入 http dump 失败：%s"), *FilePath);
		return false;
	}
	OutFilePath = FilePath;
	return true;
}

static TSharedPtr<FJsonObject> BuildOpenAIChatCompletionsPayload(
	const FString& Model,
	const FBlueprintAIRequest& Request,
	const bool bStream)
{
	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("model"), Model);

	TArray<TSharedPtr<FJsonValue>> Messages;
	{
		TSharedPtr<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
		SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
		SystemMessage->SetStringField(TEXT("content"), Request.SystemPrompt);
		Messages.Add(MakeShared<FJsonValueObject>(SystemMessage));
	}
	{
		TSharedPtr<FJsonObject> UserMessage = MakeShared<FJsonObject>();
		UserMessage->SetStringField(TEXT("role"), TEXT("user"));
		UserMessage->SetStringField(TEXT("content"), Request.UserPrompt);
		Messages.Add(MakeShared<FJsonValueObject>(UserMessage));
	}

	RootObject->SetArrayField(TEXT("messages"), Messages);
	// 与常见 Node fetch 体一致；默认 8192 以免长 JSON（增量 patch/DSL 修复）在 4k 处被截成非法 JSON
	const int32 MaxTokens = (Request.MaxOutputTokens > 0) ? FMath::Clamp(Request.MaxOutputTokens, 256, 128000) : 8192;
	RootObject->SetNumberField(TEXT("temperature"), 0.7);
	RootObject->SetNumberField(TEXT("max_tokens"), MaxTokens);
	if (bStream)
	{
		RootObject->SetBoolField(TEXT("stream"), true);
	}
	return RootObject;
}

static TSharedPtr<FJsonObject> BuildArkResponsesPayload(const FString& Model, const FBlueprintAIRequest& Request)
{
	// 参考 Ark Responses：curl 示例仅使用 model + input，不包含长 instructions。
	// 部分网关对 instructions 字段或中英混合策略更敏感，统一合并进 input 更贴近官方示例。
	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("model"), Model);
	// 默认曾用 2048，长 patch 极易被截断；有 Request.MaxOutputTokens 时以请求为准
	const int32 MaxOut = (Request.MaxOutputTokens > 0) ? FMath::Clamp(Request.MaxOutputTokens, 256, 128000) : 8192;
	RootObject->SetNumberField(TEXT("max_output_tokens"), MaxOut);

	const FString MergedUserText = FString::Printf(
		TEXT("%s\n\n---\n\n%s"),
		*Request.SystemPrompt,
		*Request.UserPrompt);

	TArray<TSharedPtr<FJsonValue>> InputArray;
	{
		TSharedPtr<FJsonObject> UserItem = MakeShared<FJsonObject>();
		UserItem->SetStringField(TEXT("role"), TEXT("user"));

		TArray<TSharedPtr<FJsonValue>> ContentArray;
		{
			TSharedPtr<FJsonObject> InputText = MakeShared<FJsonObject>();
			InputText->SetStringField(TEXT("type"), TEXT("input_text"));
			InputText->SetStringField(TEXT("text"), MergedUserText);
			ContentArray.Add(MakeShared<FJsonValueObject>(InputText));
		}

		UserItem->SetArrayField(TEXT("content"), ContentArray);
		InputArray.Add(MakeShared<FJsonValueObject>(UserItem));
	}

	RootObject->SetArrayField(TEXT("input"), InputArray);
	return RootObject;
}

static bool TryExtractTextFromArkResponses(const TSharedPtr<FJsonObject>& JsonObject, FString& OutText)
{
	if (!JsonObject.IsValid())
	{
		return false;
	}

	// 兼容多种可能字段：output_text（OpenAI Responses 风格）/ output[].content[].text（Ark）
	if (JsonObject->TryGetStringField(TEXT("output_text"), OutText) && !OutText.IsEmpty())
	{
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* OutputArray = nullptr;
	if (JsonObject->TryGetArrayField(TEXT("output"), OutputArray) && OutputArray)
	{
		// 优先：仅 assistant 的 message 段落里的 output_text（与官方示例一致）
		for (const TSharedPtr<FJsonValue>& ItemValue : *OutputArray)
		{
			const TSharedPtr<FJsonObject>* ItemObj = nullptr;
			if (!ItemValue.IsValid() || !ItemValue->TryGetObject(ItemObj) || !ItemObj || !ItemObj->IsValid())
			{
				continue;
			}

			FString ItemType;
			(*ItemObj)->TryGetStringField(TEXT("type"), ItemType);
			if (!ItemType.Equals(TEXT("message"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			FString Role;
			(*ItemObj)->TryGetStringField(TEXT("role"), Role);
			if (!Role.Equals(TEXT("assistant"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
			if (!(*ItemObj)->TryGetArrayField(TEXT("content"), ContentArray) || !ContentArray)
			{
				continue;
			}

			FString AssistantText;
			for (const TSharedPtr<FJsonValue>& ContentValue : *ContentArray)
			{
				const TSharedPtr<FJsonObject>* ContentObj = nullptr;
				if (!ContentValue.IsValid() || !ContentValue->TryGetObject(ContentObj) || !ContentObj || !ContentObj->IsValid())
				{
					continue;
				}

				FString ContentType;
				(*ContentObj)->TryGetStringField(TEXT("type"), ContentType);
				if (!ContentType.Equals(TEXT("output_text"), ESearchCase::IgnoreCase))
				{
					continue;
				}

				FString Text;
				if ((*ContentObj)->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
				{
					if (!AssistantText.IsEmpty())
					{
						AssistantText += TEXT("\n");
					}
					AssistantText += Text;
				}
			}

			if (!AssistantText.IsEmpty())
			{
				OutText = AssistantText;
				return true;
			}
		}

		// 回退：拼接所有带 content[].text 的条目
		FString Combined;
		for (const TSharedPtr<FJsonValue>& ItemValue : *OutputArray)
		{
			const TSharedPtr<FJsonObject>* ItemObj = nullptr;
			if (!ItemValue.IsValid() || !ItemValue->TryGetObject(ItemObj) || !ItemObj || !ItemObj->IsValid())
			{
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
			if (!(*ItemObj)->TryGetArrayField(TEXT("content"), ContentArray) || !ContentArray)
			{
				continue;
			}

			for (const TSharedPtr<FJsonValue>& ContentValue : *ContentArray)
			{
				const TSharedPtr<FJsonObject>* ContentObj = nullptr;
				if (!ContentValue.IsValid() || !ContentValue->TryGetObject(ContentObj) || !ContentObj || !ContentObj->IsValid())
				{
					continue;
				}

				FString Text;
				if ((*ContentObj)->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
				{
					if (!Combined.IsEmpty())
					{
						Combined += TEXT("\n");
					}
					Combined += Text;
				}
			}
		}

		if (!Combined.IsEmpty())
		{
			OutText = Combined;
			return true;
		}
	}

	return false;
}

static bool TryExtractTextFromOpenAIChatCompletions(const TSharedPtr<FJsonObject>& JsonObject, FString& OutText)
{
	if (!JsonObject.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!JsonObject->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* ChoiceObject = nullptr;
	if (!(*Choices)[0]->TryGetObject(ChoiceObject) || !ChoiceObject || !ChoiceObject->IsValid())
	{
		return false;
	}

	// 一些网关使用 legacy 文本字段：choices[0].text
	if ((*ChoiceObject)->TryGetStringField(TEXT("text"), OutText) && !OutText.IsEmpty())
	{
		return true;
	}

	const TSharedPtr<FJsonObject>* MessageObject = nullptr;
	if ((*ChoiceObject)->TryGetObjectField(TEXT("message"), MessageObject) && MessageObject && MessageObject->IsValid())
	{
		if ((*MessageObject)->TryGetStringField(TEXT("content"), OutText) && !OutText.IsEmpty())
		{
			return true;
		}
	}

	// 部分流式/兼容实现会把文本放在 choices[0].delta.content
	const TSharedPtr<FJsonObject>* DeltaObject = nullptr;
	if ((*ChoiceObject)->TryGetObjectField(TEXT("delta"), DeltaObject) && DeltaObject && DeltaObject->IsValid())
	{
		if ((*DeltaObject)->TryGetStringField(TEXT("content"), OutText) && !OutText.IsEmpty())
		{
			return true;
		}
	}

	return false;
}

static bool TryExtractTextFromWrappedJson(const TSharedPtr<FJsonObject>& JsonObject, FString& OutText)
{
	// 常见网关会包一层：{ data: {...} } / { result: {...} } / { response: {...} }
	if (!JsonObject.IsValid())
	{
		return false;
	}

	auto TryNested = [&](const FString& Key) -> bool
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (JsonObject->TryGetObjectField(Key, Obj) && Obj && Obj->IsValid())
		{
			return TryExtractTextFromGemini(*Obj, OutText) ||
				TryExtractTextFromArkResponses(*Obj, OutText) ||
				TryExtractTextFromOpenAIChatCompletions(*Obj, OutText) ||
				TryExtractTextFromWrappedJson(*Obj, OutText);
		}

		// 少数实现：{ result: "..." } / { data: "..." }
		FString Str;
		if (JsonObject->TryGetStringField(Key, Str) && !Str.IsEmpty())
		{
			OutText = Str;
			return true;
		}
		return false;
	};

	return TryNested(TEXT("data")) ||
		TryNested(TEXT("result")) ||
		TryNested(TEXT("response")) ||
		TryNested(TEXT("payload"));
}

static FString JsonTopLevelKeysToString(const TSharedPtr<FJsonObject>& JsonObject)
{
	if (!JsonObject.IsValid())
	{
		return TEXT("(invalid)");
	}
	TArray<FString> Keys;
	JsonObject->Values.GetKeys(Keys);
	Keys.Sort();
	return FString::Join(Keys, TEXT(","));
}

static void DispatchHttpAttempt(
	const FBlueprintAIRequest& Request,
	TFunction<void(const FBlueprintAIResponse&)> OnCompleted,
	int32 AttemptIndex,
	int32 MaxAttempts)
{
	const UBlueprintAIAssistantSettings* Settings = GetDefault<UBlueprintAIAssistantSettings>();
	if (!Settings)
	{
		FBlueprintAIResponse Response;
		Response.Error = TEXT("设置对象不可用。");
		OnCompleted(Response);
		return;
	}

	FString EndpointUrl;
	FString Model;
	FString ApiKey;
	Settings->ResolveCurrentProvider(EndpointUrl, Model, ApiKey);

	FString ProviderKindLabel;
	FString ModelDisplay;
	FString EndpointDisplay;
	Settings->GetProviderDisplayInfo(ProviderKindLabel, ModelDisplay, EndpointDisplay);
	EndpointUrl = EndpointUrl.TrimStartAndEnd();
	Model = Model.TrimStartAndEnd();
	ApiKey = ApiKey.TrimStartAndEnd();

	const bool bIsGemini = Settings->ProviderKind == EBlueprintAIProviderKind::Gemini;
	if (bIsGemini)
	{
		if (Settings->GeminiApiKey.IsEmpty() || Settings->GeminiModel.IsEmpty() || EndpointUrl.IsEmpty())
		{
			FBlueprintAIResponse Response;
			Response.Error = TEXT("请先在 Project Settings > Plugins > Blueprint AI Assistant 的 Gemini 分类中配置 API Key 与 Model。");
			OnCompleted(Response);
			return;
		}
	}
	else if (EndpointUrl.IsEmpty() || ApiKey.IsEmpty())
	{
		FBlueprintAIResponse Response;
		Response.Error = TEXT("请先在 Project Settings > Plugins > Blueprint AI Assistant 为当前选中的 Provider 配置 Endpoint 与 API Key。");
		OnCompleted(Response);
		return;
	}

	const bool bUseArkResponses = !bIsGemini && ShouldUseArkResponsesPayload(EndpointUrl);
	// /responses 与 generateContent 暂不用 chat SSE；OpenAI/DeepSeek/豆包等 chat 路径可开 stream
	const bool bUseOpenAiSse = !bIsGemini && !bUseArkResponses && Settings->bStreamOpenAIChatCompletions;

	TSharedPtr<FJsonObject> RootObject;
	if (bIsGemini)
	{
		RootObject = BuildGeminiGenerateContentPayload(Request);
	}
	else if (bUseArkResponses)
	{
		RootObject = BuildArkResponsesPayload(Model, Request);
	}
	else
	{
		RootObject = BuildOpenAIChatCompletionsPayload(Model, Request, bUseOpenAiSse);
	}

	FString Payload;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	// 日志与错误信息中的 URL 脱敏（Gemini 的 key 在查询串里）
	const FString EndpointSafeForLog = RedactUrlQueryKey(EndpointUrl);
	UE_LOG(LogTemp, Log, TEXT("[BlueprintAIAssistant] HTTP Payload to %s:\n%s"), *EndpointSafeForLog, *Payload);

	FTCHARToUTF8 Utf8Payload(*Payload);
	TArray<uint8> ContentBytes;
	ContentBytes.Append(reinterpret_cast<const uint8*>(Utf8Payload.Get()), Utf8Payload.Length());

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(EndpointUrl);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	if (bUseOpenAiSse)
	{
		HttpRequest->SetHeader(TEXT("Accept"), TEXT("text/event-stream, application/json; charset=utf-8"));
	}
	if (!bIsGemini)
	{
		HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	}
	HttpRequest->SetTimeout(static_cast<float>(Settings->TimeoutSeconds));
	HttpRequest->SetContent(ContentBytes);

	// 流式：在响应到达过程中持续有字节，利于中间层/代理保持连接；完成后 GetResponse()->GetContent 可能为空，正文由 FSseBufferArchive 提供
	TSharedPtr<FSseBufferArchive> SseStreamBuffer;
	if (bUseOpenAiSse)
	{
		SseStreamBuffer = MakeShared<FSseBufferArchive>();
		(void)HttpRequest->SetResponseBodyReceiveStream(SseStreamBuffer.ToSharedRef());
	}

	const int32 PayloadChars = Payload.Len();
	const int32 PayloadUtf8Bytes = ContentBytes.Num();
	const int32 TimeoutSeconds = FMath::Clamp(Settings->TimeoutSeconds, 5, 600);
	const int32 SuggestedByPayloadSeconds =
		(PayloadUtf8Bytes <= 12 * 1024) ? 120 :
		(PayloadUtf8Bytes <= 32 * 1024) ? 180 :
		(PayloadUtf8Bytes <= 64 * 1024) ? 240 : 300;
	const int32 SuggestedTimeoutSeconds = FMath::Clamp(FMath::Max(SuggestedByPayloadSeconds, TimeoutSeconds + 15), 5, 600);

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[OnCompleted, EndpointSafeForLog, Request, AttemptIndex, MaxAttempts, PayloadChars, PayloadUtf8Bytes, ProviderKindLabel, ModelDisplay, TimeoutSeconds, SuggestedTimeoutSeconds, bUseOpenAiSse, SseStreamBuffer](
			FHttpRequestPtr RequestPtr,
			FHttpResponsePtr HttpResponse,
			bool bProcessedSuccessfully)
		{
			auto ScheduleRetry = [=]()
			{
				Async(
					EAsyncExecution::Thread,
					[Request, OnCompleted, AttemptIndex, MaxAttempts]()
					{
						FPlatformProcess::Sleep(0.45f);
						AsyncTask(ENamedThreads::GameThread, [Request, OnCompleted, AttemptIndex, MaxAttempts]()
						{
							DispatchHttpAttempt(Request, OnCompleted, AttemptIndex + 1, MaxAttempts);
						});
					});
			};

			FBlueprintAIResponse Response;
			if (!bProcessedSuccessfully || !HttpResponse.IsValid())
			{
				if (AttemptIndex + 1 < MaxAttempts)
				{
					ScheduleRetry();
					return;
				}

				const FString DebugLine = BuildHttpTransportDebugLine(RequestPtr, HttpResponse, bProcessedSuccessfully);
				const EHttpFailureReason ReqFr = RequestPtr.IsValid() ? RequestPtr->GetFailureReason() : EHttpFailureReason::None;
				const FString Blurb = RequestPtr.IsValid() ? GetFailureReasonChineseBlurb(ReqFr) : TEXT("（无法读取请求上的 FailureReason，见引擎细项。）");
				FString PartialNote;
				if (HttpResponse.IsValid() && HttpResponse->GetContent().Num() > 0)
				{
					PartialNote = TEXT("\n【补充】已收到部分响应字节，仍判为失败：常见为总超时/传输中断/中间层断流；若产生计费，可能上游已处理但本机未收全。");
				}
				Response.Error = FString::Printf(
					TEXT("网络层失败（可对照下方「最可能原因」与「引擎细项」定位偶发问题；换供应商仍复现时优先查本机网络/代理/网关，而非只换模型）。\n\n"
						 "%s\n"
						 "【引擎细项】%s\n"
						 "安全提示：请求可能已到达 API 侧并产生计费，但本机未收到可用于解析的完整响应；本次不自动重试。\n"
						 "Endpoint=%s  PayloadChars=%d  Utf8Bytes=%d  项目设置中超时=%d 秒  按体积参考建议=%d 秒%s\n"
						 "usage 日志中 event=timeout_suggestion 的 reason/httpDetail 字段会记录原因摘要与上述细项。"),
					*Blurb,
					*DebugLine,
					*EndpointSafeForLog,
					PayloadChars,
					PayloadUtf8Bytes,
					TimeoutSeconds,
					SuggestedTimeoutSeconds,
					*PartialNote);
				const FString LogReason = RequestPtr.IsValid()
					? FString::Printf(TEXT("network_fail_%s"), LexToString(ReqFr))
					: TEXT("network_fail_norequest");
				FBlueprintAIUsageLogger::Get().LogTimeoutSuggestion(
					/*SessionId*/ TEXT(""),
					ProviderKindLabel,
					ModelDisplay,
					EndpointSafeForLog,
					PayloadUtf8Bytes,
					TimeoutSeconds,
					SuggestedTimeoutSeconds,
					LogReason,
					DebugLine);
				OnCompleted(Response);
				return;
			}

			if (!EHttpResponseCodes::IsOk(HttpResponse->GetResponseCode()))
			{
				const FString Body = HttpResponse->GetContentAsString();
				StoreLastHttpDebugInfo(ProviderKindLabel, ModelDisplay, EndpointSafeForLog, TEXT(""), TEXT("http_error"), Body.Left(4096));
				const int32 Code = HttpResponse->GetResponseCode();
				// 走到这里表示 Unreal 已收完一次 HTTP 事务（bProcessedOk=true），可记录底层细项
				const FString Hdbg = BuildHttpTransportDebugLine(RequestPtr, HttpResponse, true);
				if (Code == 408 || Code == 504)
				{
					Response.Error = FString::Printf(
						TEXT("请求失败，HTTP %d（网关/对端时间相关）。正文：%s\n"
							 "安全提示：此类状态多为服务端/网关/上游已判定超时，请求可能已被处理并产生计费，本次不自动重试。"
							 "若偶发、换线路仍无规律，请优先排查代理/反代/公司网络 idle 超时，其次再调大项目 Timeout。\n"
							 "当前项目 Timeout=%d 秒；按体积分档参考可尝试 %d 秒。\n"
							 "【引擎细项】%s"),
						Code, *Body, TimeoutSeconds, SuggestedTimeoutSeconds, *Hdbg);
					FBlueprintAIUsageLogger::Get().LogTimeoutSuggestion(
						/*SessionId*/ TEXT(""),
						ProviderKindLabel,
						ModelDisplay,
						EndpointSafeForLog,
						PayloadUtf8Bytes,
						TimeoutSeconds,
						SuggestedTimeoutSeconds,
						FString::Printf(TEXT("http_%d"), Code),
						Hdbg + TEXT(" | bodyHead=") + Body.Left(200));
				}
				else
				{
					const TCHAR* HttpHint = TEXT("");
					if (Code == 401 || Code == 403)
					{
						HttpHint = TEXT("【最可能】鉴权/权限：检查 API Key、Project 设置里是否填对、及网关是否改写了 Authorization。");
					}
					else if (Code == 429)
					{
						HttpHint = TEXT("【最可能】限流：请降低并发或稍后重试，或到提供商控制台查看额度。");
					}
					else if (Code == 502 || Code == 503)
					{
						HttpHint = TEXT("【最可能】网关或上游暂不可用，多为对端/代理问题；若长时间仅偶发，可忽略。");
					}
					Response.Error = FString::Printf(
						TEXT("请求失败，HTTP %d: %s\n%s\n"
							 "【引擎细项】%s\n"
							 "说明：这不是「网络完全不通」，而是已建立 HTTP 事务后，对端/网关主动返回的代码。若换供应商也偶发，优先看代理/网络中间层，而非本插件逻辑。"),
						Code, *Body, HttpHint, *Hdbg);
				}
				OnCompleted(Response);
				return;
			}

			FString RawBody;
			if (bUseOpenAiSse && SseStreamBuffer.IsValid())
			{
				RawBody = SseStreamBuffer->ToFString();
			}
			if (RawBody.IsEmpty() && HttpResponse.IsValid())
			{
				RawBody = HttpResponse->GetContentAsString();
			}

			// 流式先尝试解析 SSE 拼出全文（不依赖单段大 JSON，减轻 idle 掐断）
			if (bUseOpenAiSse)
			{
				FString StreamText;
				if (TryParseOpenAIChatSseToText(RawBody, StreamText))
				{
					if (StreamText.StartsWith(TEXT("[stream error]")))
					{
						Response.bSuccess = false;
						Response.Error = StreamText;
						StoreLastHttpDebugInfo(ProviderKindLabel, ModelDisplay, EndpointSafeForLog, TEXT("sse"), TEXT("stream_error"), RawBody.Left(4096));
						OnCompleted(Response);
						return;
					}
					if (!StreamText.IsEmpty())
					{
						Response.bSuccess = true;
						Response.Content = StreamText;
						StoreLastHttpDebugInfo(ProviderKindLabel, ModelDisplay, EndpointSafeForLog, TEXT("sse"), TEXT("openai_chat_stream"), Response.Content.Left(3000));
						OnCompleted(Response);
						return;
					}
					// 走到这里：有 data: 行但无 delta 文本，继续尝试将整段当单 JSON
				}
			}

			TSharedPtr<FJsonObject> JsonObject;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawBody);
			if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
			{
				StoreLastHttpDebugInfo(ProviderKindLabel, ModelDisplay, EndpointSafeForLog, TEXT(""), TEXT("deserialize_fail"), RawBody.Left(4096));
				const FString Hdbg = BuildHttpTransportDebugLine(RequestPtr, HttpResponse, true);
				if (bUseOpenAiSse)
				{
					Response.Error = FString::Printf(
						TEXT("流式或混合响应体无法按 JSON 解析。若已启用「流式 SSE」，而网关不返回标准 data: 行，请在项目设置中关闭流式，或换兼容网关。\n"
							 "【正文前 800 字】\n%s\n"
							 "【引擎细项】%s"),
						*RawBody.Left(800),
						*Hdbg);
				}
				else
				{
					Response.Error = FString::Printf(
						TEXT("HTTP 200 但响应体不是合法 JSON，或只收到不完整片段。\n"
							 "这常被误判为「插件没收到回复」；更常见是正文截断、中间层断流、或实际返回了 HTML/纯文本错误页。换供应商仍可能复现（与链路有关）。\n"
							 "【正文前 600 字（便于判断是 HTML/JSON/半截）】\n%s\n"
							 "【引擎细项】%s"),
						*RawBody.Left(600),
						*Hdbg);
				}
				OnCompleted(Response);
				return;
			}

			FString Extractor = TEXT("unknown");
			if (TryExtractTextFromGemini(JsonObject, Response.Content))
			{
				Extractor = TEXT("gemini");
			}
			else if (TryExtractTextFromArkResponses(JsonObject, Response.Content))
			{
				Extractor = TEXT("ark_responses");
			}
			else if (TryExtractTextFromOpenAIChatCompletions(JsonObject, Response.Content))
			{
				Extractor = TEXT("openai_chat_completions");
			}
			else if (TryExtractTextFromWrappedJson(JsonObject, Response.Content))
			{
				Extractor = TEXT("wrapped_json");
			}
			else
			{
				const FString Keys = JsonTopLevelKeysToString(JsonObject);

				// 脱敏并保存最近一次响应
				RedactJsonObjectInPlace(JsonObject);
				FString RedactedJson;
				const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RedactedJson);
				FJsonSerializer::Serialize(JsonObject.ToSharedRef(), W);

				StoreLastHttpDebugInfo(ProviderKindLabel, ModelDisplay, EndpointSafeForLog, Keys, TEXT("extract_fail"), RedactedJson);
				FBlueprintAIUsageLogger::Get().LogHttpExtractFailure(
					/*SessionId*/ TEXT(""),
					ProviderKindLabel,
					ModelDisplay,
					EndpointSafeForLog,
					Keys,
					RedactedJson.Left(1024));

				Response.Error = FString::Printf(TEXT("无法从响应中提取文本内容（未知 JSON 结构）。TopLevelKeys=[%s]。可点击「导出最近一次 HTTP 响应（脱敏）」生成排查文件。"), *Keys);
				OnCompleted(Response);
				return;
			}

			// 成功时也更新最近一次响应（便于用户导出排查“内容不对”的情况）
			{
				const FString Keys = JsonTopLevelKeysToString(JsonObject);
				RedactJsonObjectInPlace(JsonObject);
				FString RedactedJson;
				const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RedactedJson);
				FJsonSerializer::Serialize(JsonObject.ToSharedRef(), W);
				StoreLastHttpDebugInfo(ProviderKindLabel, ModelDisplay, EndpointSafeForLog, Keys, Extractor, RedactedJson);
			}

			Response.bSuccess = true;
			OnCompleted(Response);
		});

	if (!HttpRequest->ProcessRequest())
	{
		if (AttemptIndex + 1 < MaxAttempts)
		{
			Async(
				EAsyncExecution::Thread,
				[Request, OnCompleted, AttemptIndex, MaxAttempts]()
				{
					FPlatformProcess::Sleep(0.45f);
					AsyncTask(ENamedThreads::GameThread, [Request, OnCompleted, AttemptIndex, MaxAttempts]()
					{
						DispatchHttpAttempt(Request, OnCompleted, AttemptIndex + 1, MaxAttempts);
					});
				});
			return;
		}

		FBlueprintAIResponse Response;
		Response.Error = FString::Printf(
			TEXT("请求未能发出（ProcessRequest 返回 false）。Endpoint=%s，Model=%s，PayloadChars=%d，Utf8Bytes=%d。\n"
				 "【说明】此时请求尚未进入 IHttp 完成回调，无法提供 FailureReason/收包字节等细项。若偶发，可尝试重启编辑器、检查本机安全软件/系统代理/Http 模块是否已 Init。\n"
				 "为避免不确定状态下重复扣费，本次不自动重试。当前超时：%d 秒。可在 Project Settings -> Blueprint AI Assistant -> Provider -> Timeout Seconds 调大到 %d 秒（按当前 Payload 大小估算）。"),
			*EndpointSafeForLog,
			*Model,
			PayloadChars,
			PayloadUtf8Bytes,
			TimeoutSeconds,
			SuggestedTimeoutSeconds);
		FBlueprintAIUsageLogger::Get().LogTimeoutSuggestion(
			/*SessionId*/ TEXT(""),
			ProviderKindLabel,
			ModelDisplay,
			EndpointSafeForLog,
			PayloadUtf8Bytes,
			TimeoutSeconds,
			SuggestedTimeoutSeconds,
			TEXT("process_request_false"),
			TEXT("FHttpRequest::ProcessRequest 返回 false；请求未入队，无 IHttp 回调/FailureReason 细项。多见于模块未 Ready、或平台 HTTP 拒绝创建请求。建议重启编辑器/检查本机安全软件/代理。"));
		OnCompleted(Response);
		return;
	}
}

void FBlueprintAIHttpProvider::SendRequest(const FBlueprintAIRequest& Request, TFunction<void(const FBlueprintAIResponse&)> OnCompleted)
{
	// LLM API requests are not idempotent from a billing perspective: the provider may
	// charge even if the editor times out before receiving the response.
	constexpr int32 MaxAttempts = 1;
	DispatchHttpAttempt(Request, OnCompleted, 0, MaxAttempts);
}

bool FBlueprintAIHttpProvider::ExportLastHttpResponseDump(FString& OutFilePath, FString& OutError)
{
	FLastHttpDebugInfo& I = GetLastHttpDebugInfo();
	FScopeLock Lock(&I.Mutex);
	if (I.RedactedBody.IsEmpty())
	{
		OutError = TEXT("没有可导出的 HTTP 响应。请先发起一次请求（Ask/DSL/Ping 等）。");
		return false;
	}

	return WriteHttpDumpFile(I.ProviderKind, I.Model, I.EndpointSafe, I.TopLevelKeys, I.Extractor, I.RedactedBody, OutFilePath, OutError);
}
