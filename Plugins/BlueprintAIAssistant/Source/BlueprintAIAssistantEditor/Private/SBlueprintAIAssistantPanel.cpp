#include "SBlueprintAIAssistantPanel.h"

#include "BlueprintAIHttpProvider.h"
#include "BlueprintAIProviderTypes.h"
#include "BlueprintAISceneTemplates.h"
#include "BlueprintAIUsageLogger.h"
#include "BlueprintContextCollector.h"
#include "BlueprintDslExecutor.h"
#include "BlueprintDslTypes.h"
#include "BlueprintPromptBuilder.h"
#include "BlueprintAIAssistantSettings.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/LogMacros.h"
#include "Async/Async.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Editor/TransBuffer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintAIAssistant, Log, All);

static FString ClipOneLine(const FString& In, int32 MaxLen = 420)
{
	FString S = In;
	S.ReplaceInline(TEXT("\r"), TEXT(""));
	S.ReplaceInline(TEXT("\n"), TEXT(" "));
	S.TrimStartAndEndInline();
	if (S.Len() > MaxLen)
	{
		S = S.Left(MaxLen) + TEXT("…");
	}
	return S;
}

static FString ClipStrForMultiturn(const FString& S, int32 MaxLen)
{
	if (MaxLen <= 0)
	{
		return FString();
	}
	if (S.Len() <= MaxLen)
	{
		return S;
	}
	return S.Left(MaxLen) + TEXT("\n…(截断)");
}

static FString CategorizeFailureFromText(const FString& Text)
{
	const FString S = Text;
	if (S.Contains(TEXT("无法从响应中提取文本内容")) || S.Contains(TEXT("响应解析失败")) || S.Contains(TEXT("请求失败")) || S.Contains(TEXT("网络请求失败")))
	{
		return TEXT("http");
	}
	if (S.Contains(TEXT("未找到 fromPin=")) || S.Contains(TEXT("未找到 toPin=")) || S.Contains(TEXT("未找到 pinName=")) || S.Contains(TEXT("valueFromPin=")))
	{
		return TEXT("pin_resolve");
	}
	if (S.Contains(TEXT("未找到 fromNodeId=")) || S.Contains(TEXT("未找到 toNodeId=")) || S.Contains(TEXT("未找到 nodeId=")))
	{
		return TEXT("node_resolve");
	}
	if (S.Contains(TEXT("连线预检失败")))
	{
		return TEXT("connect_precheck");
	}
	if (S.Contains(TEXT("DSL 解析失败")) || S.Contains(TEXT("无法解析")) || S.Contains(TEXT("schema")) || S.Contains(TEXT("缺少")))
	{
		return TEXT("json_schema");
	}
	if (S.Contains(TEXT("requiresConfirmation=true")) || S.Contains(TEXT("高风险")))
	{
		return TEXT("high_risk_cancel");
	}
	return TEXT("execute");
}

static int32 GetUndoCountSafe()
{
	if (GEditor && GEditor->Trans)
	{
		return GEditor->Trans->GetUndoCount();
	}
	return 0;
}

static int32 CalcUndoDeltaSafe(int32 Before, int32 After)
{
	if (After < Before)
	{
		return 0;
	}
	return After - Before;
}

static int32 UndoTransactionsSafe(int32 Count)
{
	if (!GEditor || Count <= 0)
	{
		return 0;
	}
	int32 Done = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		if (!GEditor->UndoTransaction(/*bCanRedo=*/true))
		{
			break;
		}
		++Done;
	}
	return Done;
}

/** 反序列化失败时，用于判断是否为“半截 JSON”（常见：max_tokens/网关限长截断或网络断在对象中间） */
static bool ContentLooksLikeTruncatedObjectJson(const FString& Content)
{
	const FString S = Content.TrimStartAndEnd();
	if (S.IsEmpty())
	{
		return true;
	}
	int32 Depth = 0;
	for (int32 i = 0; i < S.Len(); ++i)
	{
		const TCHAR C = S[i];
		if (C == TEXT('{'))
		{
			++Depth;
		}
		else if (C == TEXT('}'))
		{
			--Depth;
		}
	}
	if (Depth != 0)
	{
		return true;
	}
	const TCHAR Last = S[S.Len() - 1];
	if (Last == TEXT(',') || Last == TEXT(':'))
	{
		return true;
	}
	return false;
}

static bool TryParseDslPatchJson(
	const FString& Content,
	TArray<TSharedPtr<FJsonObject>>& OutOps,
	FString& OutNotes,
	FString& OutError)
{
	OutOps.Reset();
	OutNotes.Empty();
	OutError.Empty();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		if (ContentLooksLikeTruncatedObjectJson(Content))
		{
			OutError = TEXT("无法解析为完整 JSON，输出疑似在末尾被截断（常见：输出长度上限/网关限长；插件已对增量修复请求使用更大的 max_tokens，若仍出现可尝试减少每步信息或分次修复）。");
		}
		else
		{
			OutError = TEXT("无法解析模型输出为 JSON。");
		}
		return false;
	}

	FString Mode;
	Root->TryGetStringField(TEXT("mode"), Mode);
	if (!Mode.Equals(TEXT("patch"), ESearchCase::IgnoreCase))
	{
		OutError = TEXT("JSON.mode != patch");
		return false;
	}

	Root->TryGetStringField(TEXT("notes"), OutNotes);

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Root->TryGetArrayField(TEXT("ops"), Arr) || !Arr)
	{
		OutError = TEXT("JSON 缺少 ops 数组。");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		if (!V.IsValid() || V->Type != EJson::Object)
		{
			continue;
		}
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (O.IsValid())
		{
			OutOps.Add(O);
		}
	}

	if (OutOps.Num() == 0)
	{
		OutError = TEXT("ops 为空。");
		return false;
	}
	return true;
}

static FString BuildSystemPromptDslPatchJson()
{
	return TEXT(
		"你是 Unreal Engine 5.6 蓝图 DSL 的“增量修改器”。请用简体中文思考，但你必须只输出一个合法 JSON 对象本身，且不要输出任何额外文字（不要 Markdown、不要代码块、不要前后缀）。"
		"重要：从第一个字符 `{` 开始到最后一个 `}` 结束。"
		"你的输入会包含：用户的修改意图、最近失败信息、以及现有 DSL steps（严格 JSON）。"
		"你不能全量重写 steps；你必须输出一个 patch（只包含需要变更的部分）。"
		"输出 JSON Schema："
		"{\"mode\":\"patch\",\"notes\":\"一句话说明\",\"ops\":["
		"{\"op\":\"remove\",\"stepId\":\"S3\"},"
		"{\"op\":\"replace\",\"stepId\":\"S5\",\"step\":{...完整 step 对象...}},"
		"{\"op\":\"insertAfter\",\"afterStepId\":\"S5\",\"step\":{...完整 step 对象...}},"
		"{\"op\":\"append\",\"step\":{...完整 step 对象...}}"
		"]}"
		"规则："
		"1) step 对象必须符合现有 DSL Schema v2（与原 steps 相同字段）。"
		"2) 只修复与用户意图/失败相关的最小集合，不要改无关步骤。"
		"3) 如果需要新增步骤，stepId 必须唯一（可用 S101/S102 等）。"
		"4) 避免引入高风险操作；若必须，引导设置 requiresConfirmation=true。");
}

static bool LooksLikePlaceholderOnlyDsl(const TArray<FBlueprintDslActionStep>& Steps)
{
	if (Steps.Num() == 0)
	{
		return false;
	}

	int32 RealEffect = 0;
	int32 Placeholder = 0;

	for (const FBlueprintDslActionStep& S : Steps)
	{
		if (S.ActionType == EBlueprintDslActionType::SetVariable)
		{
			RealEffect++;
			continue;
		}
		if (S.ActionType == EBlueprintDslActionType::CreateNode)
		{
			const FString T = S.NodeType;
			if (T.Equals(TEXT("SetVariable"), ESearchCase::IgnoreCase) ||
				T.Equals(TEXT("SpawnActorFromClass"), ESearchCase::IgnoreCase) ||
				T.Equals(TEXT("GetVariable"), ESearchCase::IgnoreCase))
			{
				RealEffect++;
				continue;
			}
			if (T.Equals(TEXT("Delay"), ESearchCase::IgnoreCase))
			{
				Placeholder++;
				continue;
			}
			if (T.Equals(TEXT("CallFunction"), ESearchCase::IgnoreCase))
			{
				if (S.FunctionName.Equals(TEXT("PrintString"), ESearchCase::IgnoreCase))
				{
					Placeholder++;
					continue;
				}
				RealEffect++;
				continue;
			}
			continue;
		}
		if (S.ActionType == EBlueprintDslActionType::Comment)
		{
			Placeholder++;
			continue;
		}
	}

	if (RealEffect == 0)
	{
		const float Ratio = (float)Placeholder / (float)FMath::Max(1, Steps.Num());
		return Ratio >= 0.5f;
	}
	return false;
}

static void GetProviderKindAndModel(FString& OutKind, FString& OutModel)
{
	OutKind = TEXT("Unknown");
	OutModel = TEXT("Unknown");
	if (const UBlueprintAIAssistantSettings* Settings = GetDefault<UBlueprintAIAssistantSettings>())
	{
		FString Endpoint;
		Settings->GetProviderDisplayInfo(OutKind, OutModel, Endpoint);
	}
}

static FString GetProviderSummaryLine()
{
	const UBlueprintAIAssistantSettings* Settings = GetDefault<UBlueprintAIAssistantSettings>();
	if (!Settings)
	{
		return TEXT("Provider=Unknown");
	}

	FString KindLabel;
	FString ModelDisplay;
	FString EndpointDisplay;
	Settings->GetProviderDisplayInfo(KindLabel, ModelDisplay, EndpointDisplay);

	return FString::Printf(TEXT("Provider=%s | Model=%s | Endpoint=%s"), *KindLabel, *ModelDisplay, *EndpointDisplay);
}

static bool TryParseClarifyJson(const FString& Content, TArray<SBlueprintAIAssistantPanel::FClarifyQuestion>& OutQs, FString& OutError)
{
	OutQs.Reset();
	OutError.Empty();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("无法解析模型输出为 JSON。");
		return false;
	}

	FString Mode;
	Root->TryGetStringField(TEXT("mode"), Mode);
	if (!Mode.Equals(TEXT("clarify"), ESearchCase::IgnoreCase))
	{
		OutError = TEXT("JSON.mode != clarify");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Root->TryGetArrayField(TEXT("questions"), Arr) || !Arr)
	{
		OutError = TEXT("JSON 缺少 questions 数组。");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		if (!V.IsValid() || V->Type != EJson::Object)
		{
			continue;
		}
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid())
		{
			continue;
		}
		SBlueprintAIAssistantPanel::FClarifyQuestion Q;
		O->TryGetStringField(TEXT("id"), Q.Id);
		O->TryGetStringField(TEXT("question"), Q.Question);
		const TArray<TSharedPtr<FJsonValue>>* OptionsArr = nullptr;
		if (O->TryGetArrayField(TEXT("options"), OptionsArr) && OptionsArr)
		{
			for (const TSharedPtr<FJsonValue>& OptVal : *OptionsArr)
			{
				if (!OptVal.IsValid())
				{
					continue;
				}
				FString Opt;
				if (OptVal->TryGetString(Opt))
				{
					Opt.TrimStartAndEndInline();
					if (!Opt.IsEmpty())
					{
						Q.Options.AddUnique(Opt);
					}
				}
			}
		}
		Q.Id = Q.Id.IsEmpty() ? FString::Printf(TEXT("Q%d"), OutQs.Num() + 1) : Q.Id;
		if (!Q.Question.IsEmpty())
		{
			OutQs.Add(Q);
		}
	}

	if (OutQs.Num() == 0)
	{
		OutError = TEXT("questions 为空。");
		return false;
	}
	return true;
}

static bool TryParsePlanJson(const FString& Content, FString& OutSummary, TArray<SBlueprintAIAssistantPanel::FPlanItem>& OutItems, FString& OutError)
{
	OutItems.Reset();
	OutSummary.Empty();
	OutError.Empty();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("无法解析模型输出为 JSON。");
		return false;
	}

	FString Mode;
	Root->TryGetStringField(TEXT("mode"), Mode);
	if (!Mode.Equals(TEXT("plan"), ESearchCase::IgnoreCase))
	{
		OutError = TEXT("JSON.mode != plan");
		return false;
	}

	Root->TryGetStringField(TEXT("summary"), OutSummary);
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Root->TryGetArrayField(TEXT("items"), Arr) || !Arr)
	{
		OutError = TEXT("JSON 缺少 items 数组。");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		if (!V.IsValid() || V->Type != EJson::Object)
		{
			continue;
		}
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid())
		{
			continue;
		}
		SBlueprintAIAssistantPanel::FPlanItem It;
		O->TryGetStringField(TEXT("stepId"), It.StepId);
		O->TryGetStringField(TEXT("title"), It.Title);
		O->TryGetStringField(TEXT("targetHint"), It.TargetHint);
		O->TryGetStringField(TEXT("dslPrompt"), It.DslPrompt);
		It.StepId = It.StepId.IsEmpty() ? FString::Printf(TEXT("P%d"), OutItems.Num() + 1) : It.StepId;
		if (!It.Title.IsEmpty() && !It.DslPrompt.IsEmpty())
		{
			OutItems.Add(It);
		}
	}

	if (OutItems.Num() == 0)
	{
		OutError = TEXT("items 为空。");
		return false;
	}
	return true;
}

enum class ETriageDecision : uint8
{
	DirectDsl,
	NeedClarify,
	NeedPlan,
};

static ETriageDecision TriageUserIntentForDsl(const FString& UserQuestion)
{
	const FString Q = UserQuestion;

	// 跨蓝图/系统级关键词（更倾向先出计划）
	const TCHAR* PlanKeywords[] = {
		TEXT("任务"), TEXT("背包"), TEXT("存档"), TEXT("成就"), TEXT("商店"),
		TEXT("UI"), TEXT("Widget"), TEXT("HUD"),
		TEXT("PlayerState"), TEXT("GameMode"), TEXT("GameInstance"),
		TEXT("多个蓝图"), TEXT("跨蓝图"), TEXT("跨几个蓝图"), TEXT("联动"),
		TEXT("网络"), TEXT("多人"), TEXT("同步"), TEXT("服务器"),
		TEXT("关卡"), TEXT("地图"), TEXT("切换关卡"),
	};
	int32 PlanHits = 0;
	for (const TCHAR* K : PlanKeywords)
	{
		if (Q.Contains(K))
		{
			PlanHits++;
		}
	}

	// 如果提到了多个 BP_ 或多个资产名，也倾向计划
	int32 BpTokenHits = 0;
	{
		int32 SearchFrom = 0;
		while (true)
		{
			const int32 Idx = Q.Find(TEXT("BP_"), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
			if (Idx == INDEX_NONE)
			{
				break;
			}
			BpTokenHits++;
			SearchFrom = Idx + 3;
			if (BpTokenHits >= 2)
			{
				break;
			}
		}
	}
	if (PlanHits >= 2 || BpTokenHits >= 2)
	{
		return ETriageDecision::NeedPlan;
	}

	// 明显缺少关键信息：没有触发、没有对象、没有目标等 → 先澄清
	const TCHAR* TriggerHints[] = {
		TEXT("BeginPlay"), TEXT("Tick"), TEXT("Overlap"), TEXT("碰到"), TEXT("触发"),
		TEXT("按键"), TEXT("输入"), TEXT("点击"), TEXT("交互"),
	};
	bool bHasTriggerHint = false;
	for (const TCHAR* K : TriggerHints)
	{
		if (Q.Contains(K, ESearchCase::IgnoreCase))
		{
			bHasTriggerHint = true;
			break;
		}
	}

	const bool bVeryAbstract =
		Q.Contains(TEXT("做一个")) || Q.Contains(TEXT("实现一个")) || Q.Contains(TEXT("系统")) || Q.Contains(TEXT("功能"));
	if (!bHasTriggerHint && bVeryAbstract)
	{
		return ETriageDecision::NeedClarify;
	}

	return ETriageDecision::DirectDsl;
}

static bool TryExtractStepsJson(const FString& InContent, TArray<FString>& OutSteps, FString& OutError)
{
	OutSteps.Reset();
	OutError.Empty();

	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(InContent);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		OutError = TEXT("无法解析模型输出为 JSON。");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* StepsArray = nullptr;
	if (!JsonObject->TryGetArrayField(TEXT("steps"), StepsArray) || !StepsArray)
	{
		OutError = TEXT("JSON 中缺少 steps 字段。");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& StepVal : *StepsArray)
	{
		if (!StepVal.IsValid())
		{
			continue;
		}

		// 由于模型要求输出 steps 为字符串，这里直接取 AsString（非字符串则可能为空）
		const FString StepText = StepVal->AsString();
		if (!StepText.IsEmpty())
		{
			OutSteps.Add(StepText);
		}
	}

	if (OutSteps.Num() == 0)
	{
		OutError = TEXT("steps 数组为空。");
		return false;
	}

	return true;
}

static FString DslActionTypeToDisplayString(EBlueprintDslActionType Type)
{
	switch (Type)
	{
	case EBlueprintDslActionType::CreateNode:
		return TEXT("CreateNode");
	case EBlueprintDslActionType::ConnectPins:
		return TEXT("ConnectPins");
	case EBlueprintDslActionType::SetPinDefault:
		return TEXT("SetPinDefault");
	case EBlueprintDslActionType::Comment:
		return TEXT("Comment");
	case EBlueprintDslActionType::GetVariable:
		return TEXT("GetVariable");
	case EBlueprintDslActionType::SetVariable:
		return TEXT("SetVariable");
	default:
		return TEXT("Unknown");
	}
}

static bool LooksLikeManualPrereqText(const FString& InText)
{
	const FString S = InText.TrimStartAndEnd();
	if (S.IsEmpty())
	{
		return false;
	}

	return
		S.Contains(TEXT("手动创建")) ||
		S.Contains(TEXT("请先")) ||
		S.Contains(TEXT("若不存在")) ||
		S.Contains(TEXT("如果不存在")) ||
		S.Contains(TEXT("需用户")) ||
		S.Contains(TEXT("需要用户")) ||
		S.Contains(TEXT("前置条件")) ||
		S.Contains(TEXT("先创建")) ||
		S.Contains(TEXT("先补")) ||
		S.Contains(TEXT("manual"), ESearchCase::IgnoreCase) ||
		S.Contains(TEXT("if not exists"), ESearchCase::IgnoreCase) ||
		S.Contains(TEXT("create "), ESearchCase::IgnoreCase);
}

static FString BuildManualPrereqSummaryFromSteps(const TArray<FBlueprintDslActionStep>& Steps, int32 MaxItems = 5)
{
	TArray<FString> Lines;
	TSet<FString> Seen;
	for (int32 i = 0; i < Steps.Num(); ++i)
	{
		const FBlueprintDslActionStep& Step = Steps[i];
		TArray<FString> Candidates;
		if (!Step.Description.IsEmpty())
		{
			Candidates.Add(Step.Description.TrimStartAndEnd());
		}
		if (Step.ActionType == EBlueprintDslActionType::Comment && !Step.CommentText.IsEmpty())
		{
			Candidates.Add(Step.CommentText.TrimStartAndEnd());
		}

		for (const FString& C : Candidates)
		{
			if (!LooksLikeManualPrereqText(C))
			{
				continue;
			}

			const FString Line = FString::Printf(TEXT("- 第 %d 步：%s"), i + 1, *C.Left(220));
			if (!Seen.Contains(Line))
			{
				Seen.Add(Line);
				Lines.Add(Line);
			}
			if (Lines.Num() >= MaxItems)
			{
				break;
			}
		}

		if (Lines.Num() >= MaxItems)
		{
			break;
		}
	}

	if (Lines.Num() == 0)
	{
		return FString();
	}

	FString Out = TEXT("检测到以下前置步骤需要手动处理（建议先完成再执行 DSL）：\n");
	for (const FString& L : Lines)
	{
		Out += L + TEXT("\n");
	}
	return Out.TrimEnd();
}

static FString NormalizeForHeuristic(const FString& In)
{
	FString S = In.TrimStartAndEnd();
	S.ReplaceInline(TEXT(" "), TEXT(""), ESearchCase::IgnoreCase);
	S.ReplaceInline(TEXT("_"), TEXT(""), ESearchCase::IgnoreCase);
	return S;
}

static bool IsLikelyBuiltinFunctionName(const FString& RawName)
{
	const FString N = NormalizeForHeuristic(RawName).ToLower();
	if (N.IsEmpty())
	{
		return true;
	}
	// 显式 owner 前缀多为库函数
	if (RawName.Contains(TEXT("::")))
	{
		return true;
	}

	// 含运算符/比较符的“语义函数名”（如 "int + int" / "Vector * Float"）不应被当作自定义函数创建
	// 这类名称会在执行器侧映射到真实函数（Add_IntInt / Multiply_VectorFloat 等）。
	for (int32 i = 0; i < RawName.Len(); ++i)
	{
		const TCHAR C = RawName[i];
		if (C == TEXT('+') || C == TEXT('-') || C == TEXT('*') || C == TEXT('/') ||
			C == TEXT('>') || C == TEXT('<') || C == TEXT('=') || C == TEXT('!') ||
			C == TEXT('&') || C == TEXT('|'))
		{
			return true;
		}
	}

	// 常见事件名（被模型误写为 CallFunction 时）：不应建函数图
	if (N == TEXT("onactorbeginoverlap") || N == TEXT("onactorendoverlap") ||
		N == TEXT("oncomponentbeginoverlap") || N == TEXT("oncomponentendoverlap") ||
		N == TEXT("beginplay") || N == TEXT("receivebeginplay") || N == TEXT("tick") || N == TEXT("receive tick"))
	{
		return true;
	}

	// 少量常用内置/库函数白名单（保守：宁可不自动建，也不误建）
	static const TCHAR* Builtins[] = {
		TEXT("printstring"),
		TEXT("delay"),
		TEXT("getplayercharacter"),
		TEXT("getplayercontroller"),
		TEXT("getactorlocation"),
		TEXT("setactorlocation"),
		TEXT("destroyactor"),
		TEXT("linetracesingle"),
		TEXT("spheretracesingle"),
		TEXT("isvalid"),
		TEXT("makevector"),
		TEXT("breakvector"),
		TEXT("makerotator"),
		TEXT("breakrotator"),
		TEXT("add_intint"),
		TEXT("add_floatfloat"),
		TEXT("conv_floattostring"),
		TEXT("equalequal_intint"),
		TEXT("notequal_intint"),
		TEXT("greater_intint"),
		TEXT("less_intint")
	};
	for (const TCHAR* B : Builtins)
	{
		// 注意：N 已做了去空格/去下划线归一化，Builtin 也需同样归一化，否则像 Add_IntInt 会误判为自定义函数
		if (N == NormalizeForHeuristic(FString(B)).ToLower())
		{
			return true;
		}
	}
	return false;
}

static FString GuessVarBaseTypeForAutoCreate(const FString& VarName)
{
	const FString S = NormalizeForHeuristic(VarName);
	if (S.IsEmpty())
	{
		return TEXT("int");
	}

	// bXxx -> bool
	if (S.Len() >= 2 && S[0] == TEXT('b') && FChar::IsUpper(S[1]))
	{
		return TEXT("bool");
	}
	const FString Lower = S.ToLower();
	if (Lower.StartsWith(TEXT("is")) || Lower.StartsWith(TEXT("has")) || Lower.StartsWith(TEXT("can")) || Lower.StartsWith(TEXT("should")))
	{
		return TEXT("bool");
	}
	// XxxCount/Num/Index/Id -> int
	if (Lower.EndsWith(TEXT("count")) || Lower.EndsWith(TEXT("num")) || Lower.EndsWith(TEXT("index")) || Lower.EndsWith(TEXT("id")))
	{
		return TEXT("int");
	}
	// Speed/Rate/Time -> float（常见）
	if (Lower.Contains(TEXT("speed")) || Lower.Contains(TEXT("rate")) || Lower.Contains(TEXT("time")))
	{
		return TEXT("float");
	}
	return TEXT("int");
}

static FString GuessBaseTypeFromLiteral(const FString& DefaultValue)
{
	const FString S = DefaultValue.TrimStartAndEnd();
	if (S.IsEmpty())
	{
		return TEXT("int");
	}
	if (S.Equals(TEXT("true"), ESearchCase::IgnoreCase) || S.Equals(TEXT("false"), ESearchCase::IgnoreCase))
	{
		return TEXT("bool");
	}
	// int or float
	bool bHasDot = false;
	for (int32 i = 0; i < S.Len(); ++i)
	{
		const TCHAR C = S[i];
		if (C == TEXT('.')) { bHasDot = true; continue; }
		if ((C == TEXT('-') && i == 0) || (C >= TEXT('0') && C <= TEXT('9')))
		{
			continue;
		}
		// 非纯数字
		return TEXT("string");
	}
	return bHasDot ? TEXT("float") : TEXT("int");
}

static FString GuessBaseTypeFromNameOrSource(const FString& PinOrVarName, const FString& SourceVarName, const FString& SourceLiteral)
{
	// 优先：字面量
	if (!SourceLiteral.TrimStartAndEnd().IsEmpty())
	{
		return GuessBaseTypeFromLiteral(SourceLiteral);
	}
	// 次优：来源变量名
	if (!SourceVarName.TrimStartAndEnd().IsEmpty())
	{
		return GuessVarBaseTypeForAutoCreate(SourceVarName);
	}
	// 兜底：参数名
	return GuessVarBaseTypeForAutoCreate(PinOrVarName);
}

static bool IsIgnorableCallPinName(const FString& PinName)
{
	const FString N = NormalizeForHeuristic(PinName).ToLower();
	return N.IsEmpty() ||
		N == TEXT("execute") ||
		N == TEXT("then") ||
		N == TEXT("returnvalue") ||
		N == TEXT("self") ||
		N == TEXT("worldcontextobject");
}

static void BuildAutoPrereqFixSteps(
	UBlueprint* Blueprint,
	const TArray<FBlueprintDslActionStep>& Steps,
	TArray<FBlueprintDslActionStep>& OutAutoSteps,
	FString& OutSummary)
{
	OutAutoSteps.Reset();
	OutSummary.Reset();
	if (!Blueprint || Steps.Num() == 0)
	{
		return;
	}

	// 现有变量
	TSet<FName> ExistingVars;
	for (const FBPVariableDescription& V : Blueprint->NewVariables)
	{
		ExistingVars.Add(V.VarName);
	}

	// DSL 已声明要创建的变量/函数
	TSet<FString> DeclaredCreateVar;
	TSet<FString> DeclaredCreateFunc;
	FString InferredTargetBpPath;
	for (const auto& Step : Steps)
	{
		if (Step.ActionType == EBlueprintDslActionType::CreateMemberVariable && !Step.VarName.IsEmpty())
		{
			DeclaredCreateVar.Add(Step.VarName.TrimStartAndEnd());
		}
		if (Step.ActionType == EBlueprintDslActionType::CreateFunctionGraph && !Step.FunctionGraphName.IsEmpty())
		{
			DeclaredCreateFunc.Add(Step.FunctionGraphName.TrimStartAndEnd());
		}
		if (!Step.TargetBlueprintAssetPath.TrimStartAndEnd().IsEmpty())
		{
			const FString P = Step.TargetBlueprintAssetPath.TrimStartAndEnd();
			if (InferredTargetBpPath.IsEmpty())
			{
				InferredTargetBpPath = P;
			}
			else if (!InferredTargetBpPath.Equals(P, ESearchCase::IgnoreCase))
			{
				// 多个不同目标 BP：不推断，避免误写到错误资产
				InferredTargetBpPath.Reset();
			}
		}
	}

	auto FunctionGraphExists = [&](const FString& Name) -> bool
	{
		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* G : AllGraphs)
		{
			if (G && G->GetName().Equals(Name, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	};

	// 1) 缺失变量：扫描 Get/SetVariable + CreateNode(GetVariable/SetVariable)
	TArray<FString> MissingVars;
	{
		TSet<FString> Seen;
		for (const auto& Step : Steps)
		{
			const bool bUsesVar =
				(Step.ActionType == EBlueprintDslActionType::GetVariable || Step.ActionType == EBlueprintDslActionType::SetVariable) ||
				(Step.ActionType == EBlueprintDslActionType::CreateNode &&
					(Step.NodeType.Equals(TEXT("GetVariable"), ESearchCase::IgnoreCase) || Step.NodeType.Equals(TEXT("SetVariable"), ESearchCase::IgnoreCase)));
			if (!bUsesVar)
			{
				continue;
			}
			const FString VN = Step.VarName.TrimStartAndEnd();
			if (VN.IsEmpty())
			{
				continue;
			}
			if (DeclaredCreateVar.Contains(VN))
			{
				continue;
			}
			if (ExistingVars.Contains(FName(*VN)))
			{
				continue;
			}
			if (!Seen.Contains(VN))
			{
				Seen.Add(VN);
				MissingVars.Add(VN);
			}
		}
	}

	// 2) 缺失自定义函数：扫描 CallFunction
	TArray<FString> MissingFuncs;
	{
		TSet<FString> Seen;
		for (const auto& Step : Steps)
		{
			if (!(Step.ActionType == EBlueprintDslActionType::CreateNode && Step.NodeType.Equals(TEXT("CallFunction"), ESearchCase::IgnoreCase)))
			{
				continue;
			}
			const FString FN = Step.FunctionName.TrimStartAndEnd();
			if (FN.IsEmpty() || IsLikelyBuiltinFunctionName(FN))
			{
				continue;
			}
			if (DeclaredCreateFunc.Contains(FN))
			{
				continue;
			}
			if (FunctionGraphExists(FN))
			{
				continue;
			}
			// 启发式：UpdateCoinUI 这种大驼峰/中文函数名优先认为是自定义
			if (!Seen.Contains(FN))
			{
				Seen.Add(FN);
				MissingFuncs.Add(FN);
			}
		}
	}

	if (MissingVars.Num() == 0 && MissingFuncs.Num() == 0)
	{
		return;
	}

	// 建立 nodeId -> varName / defaultValue 的索引，便于推断函数参数类型
	TMap<FString, FString> NodeIdToVarName;
	TMap<FString, FString> NodeIdToLiteral;
	for (const auto& Step : Steps)
	{
		if ((Step.ActionType == EBlueprintDslActionType::GetVariable || Step.ActionType == EBlueprintDslActionType::SetVariable) && !Step.NodeId.IsEmpty() && !Step.VarName.IsEmpty())
		{
			NodeIdToVarName.Add(Step.NodeId, Step.VarName);
		}
		if (Step.ActionType == EBlueprintDslActionType::CreateNode &&
			(Step.NodeType.Equals(TEXT("GetVariable"), ESearchCase::IgnoreCase) || Step.NodeType.Equals(TEXT("SetVariable"), ESearchCase::IgnoreCase)) &&
			!Step.NodeId.IsEmpty() && !Step.VarName.IsEmpty())
		{
			NodeIdToVarName.Add(Step.NodeId, Step.VarName);
		}
		if (Step.ActionType == EBlueprintDslActionType::SetPinDefault && !Step.NodeId.IsEmpty() && !Step.DefaultValue.IsEmpty())
		{
			// 记录某节点被设置过默认值（用于推断参数为字面量）
			NodeIdToLiteral.Add(Step.NodeId + TEXT("::") + Step.PinName, Step.DefaultValue);
		}
		if (Step.ActionType == EBlueprintDslActionType::SetVariable && !Step.NodeId.IsEmpty() && !Step.DefaultValue.IsEmpty())
		{
			NodeIdToLiteral.Add(Step.NodeId + TEXT("::") + TEXT("value"), Step.DefaultValue);
		}
	}

	for (const FString& VN : MissingVars)
	{
		FBlueprintDslActionStep S;
		S.ActionType = EBlueprintDslActionType::CreateMemberVariable;
		S.Description = FString::Printf(TEXT("自动补齐：创建成员变量 %s"), *VN);
		S.VarName = VN;
		S.MemberVarType.Type = GuessVarBaseTypeForAutoCreate(VN);
		S.TargetBlueprintAssetPath = InferredTargetBpPath;
		OutAutoSteps.Add(MoveTemp(S));
	}
	for (const FString& FN : MissingFuncs)
	{
		FBlueprintDslActionStep S;
		S.ActionType = EBlueprintDslActionType::CreateFunctionGraph;
		S.Description = FString::Printf(TEXT("自动补齐：创建函数骨架 %s（从调用点推断参数/返回）"), *FN);
		S.FunctionGraphName = FN;
		S.FunctionGraphKind = TEXT("Callable");
		S.TargetBlueprintAssetPath = InferredTargetBpPath;

		// 推断 params/returns：从 CallFunction 的节点 pin 使用情况反推
		TSet<FString> ParamNames;
		TMap<FString, FString> ParamTypeGuess;
		TSet<FString> ReturnNames;
		TMap<FString, FString> ReturnTypeGuess;

		// 找到所有调用该函数的 CallFunction 节点 id
		TSet<FString> CallNodeIds;
		for (const auto& Step0 : Steps)
		{
			if (Step0.ActionType == EBlueprintDslActionType::CreateNode &&
				Step0.NodeType.Equals(TEXT("CallFunction"), ESearchCase::IgnoreCase) &&
				Step0.FunctionName.TrimStartAndEnd().Equals(FN, ESearchCase::IgnoreCase) &&
				!Step0.NodeId.IsEmpty())
			{
				CallNodeIds.Add(Step0.NodeId);
			}
		}

		for (const auto& Step1 : Steps)
		{
			// 1) 连接到 Call 节点的输入 pin → 参数
			if (Step1.ActionType == EBlueprintDslActionType::ConnectPins && CallNodeIds.Contains(Step1.ToNodeId))
			{
				const FString Pin = Step1.ToPin.TrimStartAndEnd();
				if (!IsIgnorableCallPinName(Pin))
				{
					ParamNames.Add(Pin);
					const FString SrcVar = NodeIdToVarName.FindRef(Step1.FromNodeId);
					const FString SrcLit = NodeIdToLiteral.FindRef(Step1.FromNodeId + TEXT("::") + Step1.FromPin);
					ParamTypeGuess.Add(Pin, GuessBaseTypeFromNameOrSource(Pin, SrcVar, SrcLit));
				}
			}
			// 2) SetPinDefault 在 Call 节点上设置某 pin → 也当参数（字面量）
			if (Step1.ActionType == EBlueprintDslActionType::SetPinDefault && CallNodeIds.Contains(Step1.NodeId))
			{
				const FString Pin = Step1.PinName.TrimStartAndEnd();
				if (!IsIgnorableCallPinName(Pin))
				{
					ParamNames.Add(Pin);
					ParamTypeGuess.Add(Pin, GuessBaseTypeFromNameOrSource(Pin, /*SourceVar*/ FString(), Step1.DefaultValue));
				}
			}

			// 3) 从 Call 节点的 ReturnValue 接出 → 推断有返回值
			if (Step1.ActionType == EBlueprintDslActionType::ConnectPins && CallNodeIds.Contains(Step1.FromNodeId))
			{
				const FString Pin = Step1.FromPin.TrimStartAndEnd();
				if (NormalizeForHeuristic(Pin).ToLower() == TEXT("returnvalue"))
				{
					const FString RetName = TEXT("ReturnValue");
					ReturnNames.Add(RetName);
					const FString DstVar = NodeIdToVarName.FindRef(Step1.ToNodeId);
					ReturnTypeGuess.Add(RetName, GuessBaseTypeFromNameOrSource(RetName, DstVar, /*Literal*/ FString()));
				}
			}
		}

		// 组装 params/returns（保持稳定顺序）
		TArray<FString> ParamList = ParamNames.Array();
		ParamList.Sort();
		for (const FString& Pn : ParamList)
		{
			FBlueprintDslSimplePinDecl P;
			P.Name = Pn;
			P.Type.Type = ParamTypeGuess.FindRef(Pn);
			S.FunctionParams.Add(MoveTemp(P));
		}
		TArray<FString> RetList = ReturnNames.Array();
		RetList.Sort();
		for (const FString& Rn : RetList)
		{
			FBlueprintDslSimplePinDecl R;
			R.Name = Rn;
			R.Type.Type = ReturnTypeGuess.FindRef(Rn);
			S.FunctionReturns.Add(MoveTemp(R));
		}

		OutAutoSteps.Add(MoveTemp(S));
	}

	OutSummary = TEXT("检测到以下前置项可自动补齐（无需手改 JSON）：\n");
	for (const FString& VN : MissingVars)
	{
		OutSummary += FString::Printf(TEXT("- 缺成员变量：%s（将自动创建）\n"), *VN);
	}
	for (const FString& FN : MissingFuncs)
	{
		OutSummary += FString::Printf(TEXT("- 缺自定义函数：%s（将自动推断参数/返回并创建骨架）\n"), *FN);
	}
	OutSummary = OutSummary.TrimEnd();
}

static bool TryBuildOptionalBridgeSummaryFromText(const FString& InText, FString& OutSummary, int32 MaxItems = 5)
{
	OutSummary.Reset();
	if (InText.IsEmpty())
	{
		return false;
	}

	TArray<FString> Lines;
	InText.ParseIntoArrayLines(Lines, true);
	TArray<FString> Hits;
	for (const FString& L : Lines)
	{
		const FString S = L.TrimStartAndEnd();
		if (S.IsEmpty())
		{
			continue;
		}
		if (S.Contains(TEXT("自动重连")) || S.Contains(TEXT("已跳过（可选）")) || S.Contains(TEXT("可选函数")) || S.Contains(TEXT("引用可选节点")))
		{
			Hits.Add(S.Left(220));
			if (Hits.Num() >= MaxItems)
			{
				break;
			}
		}
	}

	if (Hits.Num() == 0)
	{
		return false;
	}

	OutSummary = TEXT("执行器已自动处理可选节点：\n");
	for (const FString& H : Hits)
	{
		OutSummary += TEXT("- ") + H + TEXT("\n");
	}
	OutSummary = OutSummary.TrimEnd();
	return true;
}

FString SBlueprintAIAssistantPanel::BuildDslSummaryForMultiturn(const TArray<FBlueprintDslActionStep>& Steps) const
{
	if (Steps.Num() == 0)
	{
		return FString();
	}
	const int32 MaxLines = 16;
	const int32 MaxTotal = 1000;
	FString Out;
	for (int32 i = 0; i < Steps.Num() && i < MaxLines; ++i)
	{
		const FBlueprintDslActionStep& St = Steps[i];
		const FString Desc = St.Description.IsEmpty() ? TEXT("(无描述)") : St.Description;
		Out += FString::Printf(TEXT("%d) [%s] %s\n"), i + 1, *DslActionTypeToDisplayString(St.ActionType), *Desc);
		if (Out.Len() > MaxTotal)
		{
			Out = Out.Left(MaxTotal) + TEXT("\n…(截断)");
			break;
		}
	}
	if (Steps.Num() > MaxLines)
	{
		Out += TEXT("…(更多步骤已省略)\n");
	}
	return Out;
}

FString SBlueprintAIAssistantPanel::AugmentUserPromptWithMultiTurn(
	const FString& BaseUserPrompt,
	const TArray<FBlueprintDslActionStep>& DslForContextSummary) const
{
	const UBlueprintAIAssistantSettings* AISettings = GetDefault<UBlueprintAIAssistantSettings>();
	if (!AISettings || !AISettings->bEnableMultiTurnContext)
	{
		return BaseUserPrompt;
	}

	const int32 MaxExchanges = FMath::Clamp(AISettings->MultiTurnMaxRecentExchanges, 1, 20);
	const int32 MaxBlock = FMath::Max(1024, AISettings->MultiTurnMaxContextChars);
	const int32 MaxMsg = FMath::Max(200, AISettings->MultiTurnMaxMessageChars);

	auto BuildMid = [this, MaxExchanges, MaxMsg](int32 StartIdx) -> FString
	{
		FString Mid;
		const int32 N = MultiTurnHistory.Num();
		const int32 From = FMath::Clamp(StartIdx, 0, N);
		int32 R = 0;
		for (int32 i = From; i < N; ++i)
		{
			if (R >= MaxExchanges)
			{
				break;
			}
			const FChatExchange& E = MultiTurnHistory[i];
			++R;
			Mid += FString::Printf(
				TEXT("【第%d轮】\n用户：%s\n助手：%s\n\n"),
				R,
				*ClipStrForMultiturn(E.User, MaxMsg),
				*ClipStrForMultiturn(E.Assistant, MaxMsg));
		}
		return Mid;
	};

	int32 StartIdx = FMath::Max(0, MultiTurnHistory.Num() - MaxExchanges);

	FString Extra;
	if (!LastFailureHintForNextTurn.IsEmpty())
	{
		Extra += TEXT("【最近失败/错误摘要（便于你继续修复）】\n");
		Extra += ClipStrForMultiturn(LastFailureHintForNextTurn, 1000);
		Extra += TEXT("\n\n");
	}
	if (DslForContextSummary.Num() > 0)
	{
		Extra += TEXT("【生成本请求前，面板中已有的 DSL 步骤摘要】\n");
		Extra += BuildDslSummaryForMultiturn(DslForContextSummary);
		Extra += TEXT("\n");
	}

	const FString Head = TEXT("【多轮对话上下文（Phase 6.A）】\n");
	const FString Sep = TEXT("---\n【当前请求与蓝图信息见下文】\n\n");

	for (;;)
	{
		const FString Mid = BuildMid(StartIdx);
		const FString All = Head + Mid + Extra + Sep + BaseUserPrompt;
		if (All.Len() <= MaxBlock)
		{
			return All;
		}
		if (MultiTurnHistory.Num() == 0)
		{
			// 无私历时仅 Extra 可能撑爆；逐步去掉
			const FString A2 = Head + Sep + BaseUserPrompt;
			if (A2.Len() <= MaxBlock)
			{
				return A2;
			}
			return BaseUserPrompt;
		}
		if (StartIdx < MultiTurnHistory.Num() - 1)
		{
			++StartIdx;
			continue;
		}
		const FString AllNoExtra = Head + Mid + Sep + BaseUserPrompt;
		if (AllNoExtra.Len() <= MaxBlock)
		{
			return AllNoExtra;
		}
		return Head + TEXT("（多轮上下文仍过长，已仅保留当前请求。可点「清空多轮历史」。）\n") + Sep + BaseUserPrompt;
	}
}

void SBlueprintAIAssistantPanel::RegisterMultiTurnExchange(const FString& UserQ, const FString& AssistantText)
{
	const UBlueprintAIAssistantSettings* AISettings = GetDefault<UBlueprintAIAssistantSettings>();
	if (!AISettings || !AISettings->bEnableMultiTurnContext)
	{
		InvalidateMultiturnWidgets();
		return;
	}

	const int32 MaxEx = FMath::Clamp(AISettings->MultiTurnMaxRecentExchanges, 1, 20);
	const int32 MaxMsg = FMath::Max(200, AISettings->MultiTurnMaxMessageChars);

	FChatExchange Ex;
	Ex.User = ClipStrForMultiturn(UserQ, MaxMsg);
	Ex.Assistant = ClipStrForMultiturn(AssistantText, MaxMsg);
	MultiTurnHistory.Add(Ex);
	while (MultiTurnHistory.Num() > MaxEx)
	{
		MultiTurnHistory.RemoveAt(0);
	}
	InvalidateMultiturnWidgets();
}

void SBlueprintAIAssistantPanel::NoteFailureHintForNextTurn(const FString& ShortHint)
{
	if (ShortHint.IsEmpty())
	{
		return;
	}
	LastFailureHintForNextTurn = ClipStrForMultiturn(ShortHint, 1200);
	InvalidateMultiturnWidgets();
}

void SBlueprintAIAssistantPanel::ClearMultiTurnHistory()
{
	MultiTurnHistory.Reset();
	LastFailureHintForNextTurn.Empty();
	ResponseText = TEXT("已清空多轮对话历史与最近失败摘要（已生成的 DSL 预览未删除）。");
	InvalidateMultiturnWidgets();
}

void SBlueprintAIAssistantPanel::InvalidateMultiturnWidgets()
{
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SBlueprintAIAssistantPanel::SetLlmRequestInFlight(bool bInFlight)
{
	if (bLlmRequestInFlight == bInFlight)
	{
		return;
	}
	bLlmRequestInFlight = bInFlight;
	Invalidate(EInvalidateWidgetReason::Layout);
}

FText SBlueprintAIAssistantPanel::GetMultiTurnStatusText() const
{
	const UBlueprintAIAssistantSettings* S = GetDefault<UBlueprintAIAssistantSettings>();
	const int32 N = S ? FMath::Clamp(S->MultiTurnMaxRecentExchanges, 1, 20) : 6;
	if (!S || !S->bEnableMultiTurnContext)
	{
		return FText::FromString(TEXT("多轮(6.A)：已关闭（项目设置 → 对话 | Phase 6.A）"));
	}
	if (MultiTurnHistory.Num() == 0)
	{
		return FText::FromString(FString::Printf(TEXT("多轮(6.A)：0 / 最多 %d 轮将注入"), N));
	}
	return FText::FromString(FString::Printf(
		TEXT("多轮(6.A)：已记录 %d 轮（最多向模型注入最近 %d 轮 + DSL/失败摘要，见项目设置）"),
		MultiTurnHistory.Num(),
		N));
}

FReply SBlueprintAIAssistantPanel::OnClearMultiTurnHistoryClicked()
{
	ClearMultiTurnHistory();
	return FReply::Handled();
}

void SBlueprintAIAssistantPanel::Construct(const FArguments& InArgs)
{
	Provider = MakeShared<FBlueprintAIHttpProvider>();
	ResponseText = TEXT("输入你的蓝图问题，然后点击“生成建议”。");
	CurrentDslSteps.Reset();
	CurrentDslStepSelected.Reset();
	LastFailureCategoryForGuidance.Reset();
	LastFailureDetailForGuidance.Reset();

	const float BtnH = 28.0f;
	const float BtnWPrimary = 160.0f;
	const float BtnWSecondary = 140.0f;

	ChildSlot
	[
		SNew(SBorder)
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Blueprint AI Assistant")))
					]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([] { return FText::FromString(GetProviderSummaryLine()); })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::FromString(TEXT("常用场景快捷入口（点击后自动填入 Prompt 并生成 DSL）：")))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 6.0f)
			[
				BuildSceneTemplateBar()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 4.0f)
			[
				SAssignNew(InputTextBox, SMultiLineEditableTextBox)
				.HintText(FText::FromString(TEXT("例如：我想做一个按键交互开门蓝图，应该怎么搭？")))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
					.ToolTipText(FText::FromString(TEXT("清空多轮对话历史与「最近失败摘要」；不删除当前 DSL 预览。可在项目设置中关闭多轮注入。")))
					.Text(FText::FromString(TEXT("清空多轮历史")))
					.OnClicked(this, &SBlueprintAIAssistantPanel::OnClearMultiTurnHistoryClicked)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([this]() { return GetMultiTurnStatusText(); })
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 8.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::FromString(TEXT("主流程：先生成 → 再执行（批量或单步）→ 失败时按引导继续。")))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(BtnWPrimary)
						.HeightOverride(BtnH)
						[
							SNew(SButton)
							.IsEnabled_Lambda([this]() { return !bLlmRequestInFlight; })
							.Text_Lambda([this]()
							{
								return bLlmRequestInFlight
									? FText::FromString(TEXT("正在生成中…"))
									: FText::FromString(TEXT("生成可执行步骤（DSL）"));
							})
							.ToolTipText_Lambda([this]()
							{
								return bLlmRequestInFlight
									? FText::FromString(TEXT("当前有 LLM 请求进行中，按钮已暂时禁用以免重复提交。"))
									: FText::FromString(TEXT("根据输入与蓝图上下文生成可执行的 DSL 步骤。"));
							})
							.OnClicked(this, &SBlueprintAIAssistantPanel::OnGenerateDslClicked)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(BtnWPrimary)
						.HeightOverride(BtnH)
						[
							SNew(SButton)
							.Text(FText::FromString(TEXT("执行所选步骤")))
							.OnClicked(this, &SBlueprintAIAssistantPanel::OnExecuteSelectedDslClicked)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(BtnWSecondary)
						.HeightOverride(BtnH)
						[
							SNew(SButton)
							.ToolTipText(FText::FromString(TEXT("按步骤数量连续 Undo，把本次 DSL 产生的改动全部回退（等同于连按多次 Ctrl+Z）。")))
							.Text(FText::FromString(TEXT("撤销本次改动")))
							.OnClicked(this, &SBlueprintAIAssistantPanel::OnUndoLastDslClicked)
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text(FText::FromString(TEXT("提示：单步执行可点每行「执行」。")))
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.HeaderContent()
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("高级工具（日志 / 导出 / 别名 / 反馈 / 诊断）")))
				]
				.BodyContent()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.IsEnabled_Lambda([this]() { return !bLlmRequestInFlight; })
								.Text(FText::FromString(TEXT("生成建议")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnAskClicked)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.IsEnabled_Lambda([this]() { return !bLlmRequestInFlight; })
								.Text(FText::FromString(TEXT("生成步骤清单")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnGenerateStepsClicked)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.IsEnabled_Lambda([this]() { return !bLlmRequestInFlight; })
								.Text(FText::FromString(TEXT("连接测试")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnTestConnectionClicked)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("复制输出区")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnCopyClicked)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("插入注释到蓝图")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnInsertCommentClicked)
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.ToolTipText(FText::FromString(TEXT("打开本机使用日志目录（Saved/BlueprintAIAssistant），用于反馈问题")))
								.Text(FText::FromString(TEXT("打开日志目录")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnOpenLogFolderClicked)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.ToolTipText(FText::FromString(TEXT("扫描 Saved/BlueprintAIAssistant/usage-*.log，生成试用 KPI 复盘报告。")))
								.Text(FText::FromString(TEXT("生成 KPI 报告")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnGenerateKpiReportClicked)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.ToolTipText(FText::FromString(TEXT("将最近一次 HTTP 响应写入 Saved/BlueprintAIAssistant/http-dumps/（已脱敏），用于兼容网关排查。")))
								.Text(FText::FromString(TEXT("导出 HTTP（脱敏）")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnExportLastHttpDumpClicked)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.ToolTipText(FText::FromString(TEXT("把最近一次 prompt + DSL + 错误/结果打包成 markdown 文件，方便反馈给开发同学。")))
								.Text(FText::FromString(TEXT("反馈本次问题")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnSubmitFeedbackClicked)
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.ToolTipText(FText::FromString(TEXT("扫描使用日志中的 pin 相关失败，生成别名候选报告（不会自动改代码）。")))
								.Text(FText::FromString(TEXT("生成 Pin 别名候选")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnGeneratePinAliasSuggestionsClicked)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.ToolTipText(FText::FromString(TEXT("从日志目录中读取最新 pin-alias-suggestions-*.md，并在下方生成可勾选列表。")))
								.Text(FText::FromString(TEXT("导入候选")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnImportPinAliasSuggestionsClicked)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.ToolTipText(FText::FromString(TEXT("把勾选的候选写入 Saved/BlueprintAIAssistant/pin-aliases.json 并立即热更新生效。")))
								.Text(FText::FromString(TEXT("写入并启用别名表")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnWritePinAliasesJsonClicked)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.ToolTipText(FText::FromString(TEXT("重新加载 Saved/BlueprintAIAssistant/pin-aliases.json（不需要重启编辑器）。")))
								.Text(FText::FromString(TEXT("重新加载别名表")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnReloadPinAliasesClicked)
							]
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBorder)
				.Padding(6.0f)
				.Visibility_Lambda([this]()
				{
					return PinAliasCandidates.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SAssignNew(PinAliasCandidatesBox, SVerticalBox)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				.Visibility_Lambda([this]()
				{
					return PendingClarifyQuestions.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("需要补充信息（澄清问题）")))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SAssignNew(ClarifyBox, SVerticalBox)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.IsEnabled_Lambda([this]() { return !bLlmRequestInFlight; })
						.Text_Lambda([this]()
						{
							return bLlmRequestInFlight
								? FText::FromString(TEXT("正在生成中…"))
								: FText::FromString(TEXT("基于补充信息生成 DSL"));
						})
						.ToolTipText_Lambda([this]()
						{
							return bLlmRequestInFlight
								? FText::FromString(TEXT("当前有 LLM 请求进行中，按钮已暂时禁用以免重复提交。"))
								: FText::FromString(TEXT("将已填写的澄清信息合并到问题中并生成 DSL。"));
						})
						.OnClicked(this, &SBlueprintAIAssistantPanel::OnGenerateDslFromClarifyClicked)
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				.Visibility_Lambda([this]()
				{
					return PendingPlanItems.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("跨蓝图实施计划（可逐步落到 DSL）")))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SAssignNew(PlanItemsBox, SVerticalBox)
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				.Visibility_Lambda([this]()
				{
					return LastFailureDetailForGuidance.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("失败引导（可继续推进）")))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text_Lambda([this]()
						{
							const FString CatDisplay = LastStepFailures.Num() > 0
								? FDslStepFailure::CategoryToDisplayString(LastStepFailures[0].Category)
								: ([this]() -> FString
								{
									if (LastFailureCategoryForGuidance.IsEmpty())
									{
										return TEXT("未知");
									}
									if (LastFailureCategoryForGuidance.Equals(TEXT("optional_bridge"), ESearchCase::IgnoreCase))
									{
										return TEXT("可选步骤已自动处理");
									}
									return LastFailureCategoryForGuidance;
								})();
							return FText::FromString(FString::Printf(TEXT("%s%s"), *CatDisplay, LastFailureDetailForGuidance.IsEmpty() ? TEXT("") : *("\n" + LastFailureDetailForGuidance.Left(600))));
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("按建议重生 DSL")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnRegenerateDslFromFailureClicked)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.Text(FText::FromString(TEXT("复制修复提示词")))
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnCopyFailureFixPromptClicked)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(BtnWSecondary)
							.HeightOverride(BtnH)
							[
								SNew(SButton)
								.IsEnabled_Lambda([this]() { return !bLlmRequestInFlight; })
								.Text_Lambda([this]()
								{
									return bLlmRequestInFlight
										? FText::FromString(TEXT("请求中…"))
										: FText::FromString(TEXT("请求增量修复(6.B)"));
								})
								.OnClicked(this, &SBlueprintAIAssistantPanel::OnRequestDslPatchFixClicked)
							]
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(this, &SBlueprintAIAssistantPanel::GetResponseText)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("DSL 步骤预览")))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				[
					SAssignNew(DslStepsBox, SVerticalBox)
				]
			]
				]
			]
		]
	];

	RebuildDslPreview();
}

void SBlueprintAIAssistantPanel::UpdateFailureGuidanceFromResponseText()
{
	// 仅在“失败/错误/预检失败”等情况下更新卡片，避免成功提示覆盖掉已有引导。
	const FString S = ResponseText;
	FString OptionalBridgeSummary;
	const bool bHasOptionalBridgeInfo = TryBuildOptionalBridgeSummaryFromText(S, OptionalBridgeSummary);
	const bool bLooksLikeFailure =
		S.Contains(TEXT("失败")) ||
		S.Contains(TEXT("错误")) ||
		S.Contains(TEXT("连线预检失败")) ||
		S.Contains(TEXT("未找到")) ||
		S.Contains(TEXT("无法解析")) ||
		S.Contains(TEXT("请求失败")) ||
		S.Contains(TEXT("取消执行")) ||
		S.Contains(TEXT("未完成"));

	if (!bLooksLikeFailure && !bHasOptionalBridgeInfo)
	{
		return;
	}

	if (!bLooksLikeFailure && bHasOptionalBridgeInfo)
	{
		LastFailureCategoryForGuidance = TEXT("optional_bridge");
		LastFailureDetailForGuidance = OptionalBridgeSummary.Left(600);
		return;
	}

	LastFailureCategoryForGuidance = CategorizeFailureFromText(S);
	LastFailureDetailForGuidance = ClipOneLine(S, 600);

	// 追加“手动推进”提示：即使 DSL 无法执行，也能让地编按图索骥继续搭建/连线。
	FString Manual;
	if (LastFailureCategoryForGuidance == TEXT("json_schema"))
	{
		Manual =
			TEXT("手动推进建议：\n")
			TEXT("1) 先点「生成步骤清单」拿到纯人工操作步骤；\n")
			TEXT("2) 若是模板类需求（如播放蒙太奇）：在 EventGraph 放 BeginPlay → PlayAnimMontage（Character/Mesh 需要你手动指定）→ PrintString；\n")
			TEXT("3) PlayAnimMontage 需要你手动在 Details 里选择 Montage 资源（或稍后再补）。");
	}
	else if (LastFailureCategoryForGuidance == TEXT("node_resolve"))
	{
		Manual =
			TEXT("手动推进建议：\n")
			TEXT("1) 在蓝图空白处右键搜索缺失节点名（事件/函数）；\n")
			TEXT("2) 事件类：BeginPlay/ActorBeginOverlap/ActorEndOverlap/InputKey 应放在 EventGraph 顶部；\n")
			TEXT("3) 再按错误信息把执行线（白色）从事件 then 接到目标节点 execute。");
	}
	else if (LastFailureCategoryForGuidance == TEXT("pin_resolve"))
	{
		Manual =
			TEXT("手动推进建议：\n")
			TEXT("1) 选中节点看每个 Pin 的真实名字/类型（或右键节点「刷新节点」）；\n")
			TEXT("2) 结构体/命中结果常需要先 Break（例如 HitResult 先 BreakHitResult 再用 HitActor）；\n")
			TEXT("3) Cast：Object 是数据 pin，执行线应接 Cast Succeeded/Failed。");
	}
	else if (LastFailureCategoryForGuidance == TEXT("connect_precheck"))
	{
		Manual =
			TEXT("手动推进建议：\n")
			TEXT("1) 看预检错误里两端 Pin 类型，确认是否同类型/可隐式转换；\n")
			TEXT("2) 不可直连时：插入转换/Break/Make 节点做中间桥接；\n")
			TEXT("3) 先把执行链路走通（白线），再补数据线。");
	}
	else if (LastFailureCategoryForGuidance == TEXT("http"))
	{
		Manual =
			TEXT("手动推进建议：\n")
			TEXT("1) 先点「生成步骤清单」拿到本地可执行的人肉步骤；\n")
			TEXT("2) 检查 Project Settings → Blueprint AI Assistant 的 Endpoint/Key/超时；\n")
			TEXT("3) 必要时点「导出 HTTP（脱敏）」把 dump 发给开发排查网关。");
	}

	if (!Manual.IsEmpty())
	{
		LastFailureDetailForGuidance += TEXT("\n\n") + Manual.Left(290);
	}
	if (bHasOptionalBridgeInfo)
	{
		LastFailureDetailForGuidance += TEXT("\n\n") + OptionalBridgeSummary.Left(260);
	}
}

void SBlueprintAIAssistantPanel::ClearFailureGuidanceState()
{
	LastFailureCategoryForGuidance.Reset();
	LastFailureDetailForGuidance.Reset();
	LastStepFailures.Reset();
	LastFailureStepIndex = INDEX_NONE;
	LastFailureNodeId.Reset();
	LastFailurePinName.Reset();
	LastFailureOriginalDslStep.Reset();
}

void SBlueprintAIAssistantPanel::ApplyStructuredFailures(const TArray<FDslStepFailure>& Failures)
{
	LastStepFailures = Failures;
	if (Failures.Num() == 0)
	{
		return;
	}

	// 用首个（最早发生）的失败作为引导卡片的主要信息
	const FDslStepFailure& First = Failures[0];

	// 类别 → 字符串（供 BuildFailureFixPromptText 等文字路径使用）
	LastFailureCategoryForGuidance = [&]() -> FString {
		switch (First.Category)
		{
		case EDslFailureCategory::NodeResolve:      return TEXT("node_resolve");
		case EDslFailureCategory::PinResolve:       return TEXT("pin_resolve");
		case EDslFailureCategory::ConnectPrecheck:  return TEXT("connect_precheck");
		case EDslFailureCategory::ConnectFail:      return TEXT("connect_fail");
		case EDslFailureCategory::FunctionNotFound: return TEXT("function_not_found");
		case EDslFailureCategory::ClassNotFound:    return TEXT("class_not_found");
		case EDslFailureCategory::GraphResolve:     return TEXT("graph_resolve");
		case EDslFailureCategory::SetPinFail:       return TEXT("set_pin_fail");
		case EDslFailureCategory::SchemaError:      return TEXT("json_schema");
		case EDslFailureCategory::Http:             return TEXT("http");
		default:                                    return TEXT("unknown");
		}
	}();

	// 组装详情文本（先给地编一个“一眼能定位”的摘要，再展开细节）
	FString Detail;
	const FString CatDisplay = FDslStepFailure::CategoryToDisplayString(First.Category);
	const FString StepDesc = (First.StepIndex != INDEX_NONE)
		? FString::Printf(TEXT("第 %d 步"), First.StepIndex + 1) : TEXT("");
	LastFailureStepIndex = First.StepIndex;
	LastFailureNodeId = First.NodeId;
	LastFailurePinName = First.PinName;
	LastFailureOriginalDslStep.Reset();
	if (CurrentDslSteps.IsValidIndex(First.StepIndex))
	{
		TArray<FBlueprintDslActionStep> OneStep;
		OneStep.Add(CurrentDslSteps[First.StepIndex]);
		LastFailureOriginalDslStep = SerializeDslStepsToJson(OneStep, 2);
	}

	// 快速定位摘要：优先显示连线的 from/to，其次显示 node/pin。
	FString QuickLocate = StepDesc.IsEmpty() ? TEXT("步骤未知") : StepDesc;
	if (!First.FromNodeId.IsEmpty() || !First.ToNodeId.IsEmpty())
	{
		const FString FromLabel = FString::Printf(TEXT("%s.%s"),
			First.FromNodeId.IsEmpty() ? TEXT("?") : *First.FromNodeId,
			First.FromPin.IsEmpty() ? TEXT("?") : *First.FromPin);
		const FString ToLabel = FString::Printf(TEXT("%s.%s"),
			First.ToNodeId.IsEmpty() ? TEXT("?") : *First.ToNodeId,
			First.ToPin.IsEmpty() ? TEXT("?") : *First.ToPin);
		QuickLocate += FString::Printf(TEXT(" | %s -> %s"), *FromLabel, *ToLabel);
	}
	else if (!First.NodeId.IsEmpty())
	{
		QuickLocate += FString::Printf(TEXT(" | 节点=%s"), *First.NodeId);
		if (!First.PinName.IsEmpty())
		{
			QuickLocate += FString::Printf(TEXT(".%s"), *First.PinName);
		}
	}

	Detail += FString::Printf(TEXT("快速定位：%s\n"), *QuickLocate.Left(220));
	Detail += FString::Printf(TEXT("[%s] %s\n"), *CatDisplay, *StepDesc);

	if (!First.NodeId.IsEmpty())
	{
		Detail += FString::Printf(TEXT("节点：%s\n"), *First.NodeId);
	}
	if (!First.PinName.IsEmpty())
	{
		Detail += FString::Printf(TEXT("引脚：%s\n"), *First.PinName);
	}
	if (First.Category == EDslFailureCategory::ConnectPrecheck ||
		First.Category == EDslFailureCategory::ConnectFail)
	{
		if (!First.FromNodeId.IsEmpty() || !First.FromPin.IsEmpty())
		{
			Detail += FString::Printf(TEXT("来源：%s.%s\n"), *First.FromNodeId, *First.FromPin);
		}
		if (!First.ToNodeId.IsEmpty() || !First.ToPin.IsEmpty())
		{
			Detail += FString::Printf(TEXT("目标：%s.%s\n"), *First.ToNodeId, *First.ToPin);
		}
	}
	Detail += TEXT("\n") + First.RawError.Left(300);

	if (Failures.Num() > 1)
	{
		Detail += FString::Printf(TEXT("\n\n（共 %d 处失败，仅展示首个）"), Failures.Num());
	}

	// 追加手动推进建议
	const FString Manual = FDslStepFailure::BuildManualGuidance(First);
	if (!Manual.IsEmpty())
	{
		Detail += TEXT("\n\n") + Manual;
	}

	LastFailureDetailForGuidance = Detail.Left(800);
}

FString SBlueprintAIAssistantPanel::BuildStructuredFailureSummaryForPatchPrompt() const
{
	if (LastStepFailures.Num() <= 0)
	{
		return LastFailureDetailForGuidance;
	}

	FString Out;
	const int32 MaxFailures = FMath::Min(LastStepFailures.Num(), 3);
	Out += FString::Printf(TEXT("结构化失败（展示前 %d 条）：\n"), MaxFailures);
	for (int32 i = 0; i < MaxFailures; ++i)
	{
		const FDslStepFailure& F = LastStepFailures[i];
		const FString Cat = FDslStepFailure::CategoryToDisplayString(F.Category);
		const FString StepLabel = (F.StepIndex != INDEX_NONE)
			? FString::Printf(TEXT("S%d"), F.StepIndex + 1)
			: TEXT("S?");
		Out += FString::Printf(TEXT("- %s [%s] node=%s pin=%s from=%s.%s to=%s.%s err=%s\n"),
			*StepLabel,
			*Cat,
			F.NodeId.IsEmpty() ? TEXT("-") : *F.NodeId,
			F.PinName.IsEmpty() ? TEXT("-") : *F.PinName,
			F.FromNodeId.IsEmpty() ? TEXT("-") : *F.FromNodeId,
			F.FromPin.IsEmpty() ? TEXT("-") : *F.FromPin,
			F.ToNodeId.IsEmpty() ? TEXT("-") : *F.ToNodeId,
			F.ToPin.IsEmpty() ? TEXT("-") : *F.ToPin,
			*ClipOneLine(F.RawError, 200));
	}

	if (!LastFailureOriginalDslStep.IsEmpty())
	{
		Out += TEXT("\n首个失败对应原始步骤（严格 JSON）：\n");
		Out += LastFailureOriginalDslStep;
	}
	return Out;
}

FString SBlueprintAIAssistantPanel::BuildFailureFixPromptText() const
{
	FString Out;
	Out += TEXT("我在 Unreal 蓝图里用 Blueprint AI Assistant 执行 DSL 失败了。请你给出“能让地编继续推进”的修复建议，并生成一份新的可执行 DSL（只针对失败点做最小修改，不要全量重写）。\n");

	if (!LastFailureCategoryForGuidance.IsEmpty() || !LastFailureDetailForGuidance.IsEmpty())
	{
		Out += FString::Printf(TEXT("\n失败信息（摘要）：[%s] %s\n"),
			LastFailureCategoryForGuidance.IsEmpty() ? TEXT("unknown") : *LastFailureCategoryForGuidance,
			*LastFailureDetailForGuidance);
	}
	const FString Structured = BuildStructuredFailureSummaryForPatchPrompt();
	if (!Structured.IsEmpty())
	{
		Out += TEXT("\n失败信息（结构化）：\n");
		Out += Structured;
		Out += TEXT("\n");
	}

	Out += TEXT("\n当前 DSL（严格 JSON）：\n");
	Out += SerializeDslStepsToJson(CurrentDslSteps, /*Version*/ 2);

	Out += TEXT("\n\n修复要求：\n");
	Out += TEXT("- 若错误是连线类型不兼容，请明确说明正确连法，并在 DSL 中补齐缺失节点/连线。\n");
	Out += TEXT("- 常见坑：LineTrace 的 OutHit 是 HitResult，不能直接接 Cast.Object；应 BreakHitResult 再用 HitActor 接 Cast.Object。\n");
	Out += TEXT("- 常见坑：Cast.Object 是数据引脚，不能接 execute；执行线应接 Cast Succeeded / Cast Failed。\n");
	return Out;
}

FReply SBlueprintAIAssistantPanel::OnCopyFailureFixPromptClicked()
{
	const FString Prompt = BuildFailureFixPromptText();
	if (Prompt.IsEmpty())
	{
		ResponseText = TEXT("当前没有可复制的修复提示词。");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}

	FPlatformApplicationMisc::ClipboardCopy(*Prompt);
	ResponseText = TEXT("已复制“修复提示词”（可粘贴到输入框或发给开发同学）。");
	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnRegenerateDslFromFailureClicked()
{
	if (!InputTextBox.IsValid())
	{
		ResponseText = TEXT("输入框不可用。");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}

	const FString FixPrompt = BuildFailureFixPromptText();
	InputTextBox->SetText(FText::FromString(FixPrompt.Left(1500)));
	ResponseText = TEXT("已把“修复提示词”写入输入框（已截断）。你可以补充细节后再点「生成可执行步骤（DSL）」。");
	UpdateFailureGuidanceFromResponseText();
	return FReply::Handled();
}

static void NormalizeMissingStepIds(TArray<FBlueprintDslActionStep>& Steps)
{
	int32 Next = 1;
	for (FBlueprintDslActionStep& S : Steps)
	{
		if (S.StepId.IsEmpty())
		{
			S.StepId = FString::Printf(TEXT("S%d"), Next++);
		}
		else
		{
			Next++;
		}
	}
}

static bool ApplyDslPatchOps(
	TArray<FBlueprintDslActionStep>& InOutSteps,
	const TArray<TSharedPtr<FJsonObject>>& Ops,
	FString& OutError,
	FString& OutSummary,
	int32& OutAppliedCount)
{
	OutError.Empty();
	OutSummary.Empty();
	OutAppliedCount = 0;
	NormalizeMissingStepIds(InOutSteps);
	int32 AppliedCount = 0;
	TArray<FString> SummaryLines;

	auto FindIndexByStepId = [&](const FString& StepId) -> int32
	{
		for (int32 i = 0; i < InOutSteps.Num(); ++i)
		{
			if (InOutSteps[i].StepId.Equals(StepId, ESearchCase::IgnoreCase))
			{
				return i;
			}
		}
		return INDEX_NONE;
	};
	auto NormalizeIdKey = [](const FString& Id) -> FString
	{
		return Id.TrimStartAndEnd().ToLower();
	};

	auto ParseStepObject = [&](const TSharedPtr<FJsonObject>& StepObj, FBlueprintDslActionStep& OutStep, FString& Err) -> bool
	{
		// 复用现有严格解析器：包装成 {"version":2,"steps":[...]}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("version"), 2);
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueObject>(StepObj));
		Root->SetArrayField(TEXT("steps"), Arr);

		FString Serialized;
		const TSharedRef<TJsonWriter<TCHAR>> Writer = TJsonWriterFactory<TCHAR>::Create(&Serialized);
		FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

		TArray<FBlueprintDslActionStep> Parsed;
		if (!ParseDslStepsFromJson(Serialized, Parsed, Err) || Parsed.Num() == 0)
		{
			return false;
		}
		OutStep = Parsed[0];
		if (OutStep.StepId.IsEmpty())
		{
			// patch step 若没 stepId，尽量从对象里读
			StepObj->TryGetStringField(TEXT("stepId"), OutStep.StepId);
		}
		return true;
	};
	TSet<FString> UsedStepIds;
	for (const FBlueprintDslActionStep& S : InOutSteps)
	{
		const FString Key = NormalizeIdKey(S.StepId);
		if (Key.IsEmpty())
		{
			OutError = TEXT("现有 DSL 存在空 stepId，无法应用 patch。");
			return false;
		}
		if (UsedStepIds.Contains(Key))
		{
			OutError = FString::Printf(TEXT("现有 DSL 存在重复 stepId：%s"), *S.StepId);
			return false;
		}
		UsedStepIds.Add(Key);
	}
	auto EnsureUniqueStepId = [&](FBlueprintDslActionStep& InOutStep) -> bool
	{
		FString Key = NormalizeIdKey(InOutStep.StepId);
		if (!Key.IsEmpty() && !UsedStepIds.Contains(Key))
		{
			return true;
		}
		for (int32 i = 1; i <= 5000; ++i)
		{
			const FString Candidate = FString::Printf(TEXT("S%d"), InOutSteps.Num() + i);
			const FString CandidateKey = NormalizeIdKey(Candidate);
			if (!UsedStepIds.Contains(CandidateKey))
			{
				InOutStep.StepId = Candidate;
				return true;
			}
		}
		return false;
	};
	auto StepLabel = [](const FBlueprintDslActionStep& Step) -> FString
	{
		FString Label = Step.StepId.IsEmpty() ? TEXT("(无 stepId)") : Step.StepId;
		if (!Step.Description.IsEmpty())
		{
			Label += FString::Printf(TEXT("：%s"), *ClipOneLine(Step.Description, 80));
		}
		else if (!Step.NodeId.IsEmpty())
		{
			Label += FString::Printf(TEXT("：node=%s"), *Step.NodeId);
		}
		return Label;
	};

	for (const TSharedPtr<FJsonObject>& OpObj : Ops)
	{
		if (!OpObj.IsValid())
		{
			continue;
		}
		FString Op;
		OpObj->TryGetStringField(TEXT("op"), Op);
		Op = Op.TrimStartAndEnd();
		if (Op.IsEmpty())
		{
			continue;
		}

		if (Op.Equals(TEXT("remove"), ESearchCase::IgnoreCase))
		{
			FString StepId;
			OpObj->TryGetStringField(TEXT("stepId"), StepId);
			if (StepId.TrimStartAndEnd().IsEmpty())
			{
				OutError = TEXT("remove 缺少 stepId。");
				return false;
			}
			const int32 Idx = FindIndexByStepId(StepId);
			if (Idx == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("remove 找不到目标 stepId：%s"), *StepId);
				return false;
			}
			UsedStepIds.Remove(NormalizeIdKey(InOutSteps[Idx].StepId));
			SummaryLines.Add(FString::Printf(TEXT("remove：%s"), *StepLabel(InOutSteps[Idx])));
			InOutSteps.RemoveAt(Idx);
			++AppliedCount;
			continue;
		}

		if (Op.Equals(TEXT("replace"), ESearchCase::IgnoreCase))
		{
			FString StepId;
			OpObj->TryGetStringField(TEXT("stepId"), StepId);
			if (StepId.TrimStartAndEnd().IsEmpty())
			{
				OutError = TEXT("replace 缺少 stepId。");
				return false;
			}
			const int32 Idx = FindIndexByStepId(StepId);
			if (Idx == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("replace 找不到目标 stepId：%s"), *StepId);
				return false;
			}
			const TSharedPtr<FJsonObject>* StepObjPtr = nullptr;
			if (!OpObj->TryGetObjectField(TEXT("step"), StepObjPtr) || !StepObjPtr || !StepObjPtr->IsValid())
			{
				OutError = TEXT("replace 缺少 step 对象。");
				return false;
			}
			FBlueprintDslActionStep NewStep;
			FString Err;
			if (!ParseStepObject(*StepObjPtr, NewStep, Err))
			{
				OutError = FString::Printf(TEXT("replace.step 解析失败：%s"), *Err);
				return false;
			}
			if (NewStep.StepId.IsEmpty())
			{
				NewStep.StepId = StepId;
			}
			const FString OldKey = NormalizeIdKey(InOutSteps[Idx].StepId);
			const FString NewKey = NormalizeIdKey(NewStep.StepId);
			if (!OldKey.Equals(NewKey, ESearchCase::CaseSensitive) && UsedStepIds.Contains(NewKey))
			{
				OutError = FString::Printf(TEXT("replace 引入重复 stepId：%s"), *NewStep.StepId);
				return false;
			}
			UsedStepIds.Remove(OldKey);
			UsedStepIds.Add(NewKey);
			SummaryLines.Add(FString::Printf(TEXT("replace：%s -> %s"), *StepLabel(InOutSteps[Idx]), *StepLabel(NewStep)));
			InOutSteps[Idx] = NewStep;
			++AppliedCount;
			continue;
		}

		if (Op.Equals(TEXT("insertAfter"), ESearchCase::IgnoreCase))
		{
			FString AfterId;
			OpObj->TryGetStringField(TEXT("afterStepId"), AfterId);
			if (AfterId.TrimStartAndEnd().IsEmpty())
			{
				OutError = TEXT("insertAfter 缺少 afterStepId。");
				return false;
			}
			const int32 AfterIdx = FindIndexByStepId(AfterId);
			if (AfterIdx == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("insertAfter 找不到 afterStepId：%s"), *AfterId);
				return false;
			}
			const TSharedPtr<FJsonObject>* StepObjPtr = nullptr;
			if (!OpObj->TryGetObjectField(TEXT("step"), StepObjPtr) || !StepObjPtr || !StepObjPtr->IsValid())
			{
				OutError = TEXT("insertAfter 缺少 step 对象。");
				return false;
			}
			FBlueprintDslActionStep NewStep;
			FString Err;
			if (!ParseStepObject(*StepObjPtr, NewStep, Err))
			{
				OutError = FString::Printf(TEXT("insertAfter.step 解析失败：%s"), *Err);
				return false;
			}
			if (!EnsureUniqueStepId(NewStep))
			{
				OutError = TEXT("insertAfter 无法分配唯一 stepId。");
				return false;
			}
			const FString NewKey = NormalizeIdKey(NewStep.StepId);
			UsedStepIds.Add(NewKey);
			const int32 InsertAt = AfterIdx + 1;
			InOutSteps.Insert(NewStep, InsertAt);
			SummaryLines.Add(FString::Printf(TEXT("insertAfter %s：%s"), *AfterId, *StepLabel(NewStep)));
			++AppliedCount;
			continue;
		}

		if (Op.Equals(TEXT("append"), ESearchCase::IgnoreCase))
		{
			const TSharedPtr<FJsonObject>* StepObjPtr = nullptr;
			if (!OpObj->TryGetObjectField(TEXT("step"), StepObjPtr) || !StepObjPtr || !StepObjPtr->IsValid())
			{
				OutError = TEXT("append 缺少 step 对象。");
				return false;
			}
			FBlueprintDslActionStep NewStep;
			FString Err;
			if (!ParseStepObject(*StepObjPtr, NewStep, Err))
			{
				OutError = FString::Printf(TEXT("append.step 解析失败：%s"), *Err);
				return false;
			}
			if (!EnsureUniqueStepId(NewStep))
			{
				OutError = TEXT("append 无法分配唯一 stepId。");
				return false;
			}
			const FString NewKey = NormalizeIdKey(NewStep.StepId);
			UsedStepIds.Add(NewKey);
			InOutSteps.Add(NewStep);
			SummaryLines.Add(FString::Printf(TEXT("append：%s"), *StepLabel(NewStep)));
			++AppliedCount;
			continue;
		}

		OutError = FString::Printf(TEXT("不支持的 patch op：%s"), *Op);
		return false;
	}

	if (AppliedCount <= 0)
	{
		OutError = TEXT("patch 未产生任何变更。");
		return false;
	}

	OutAppliedCount = AppliedCount;
	OutSummary = FString::Join(SummaryLines, TEXT("\n"));
	return true;
}

FReply SBlueprintAIAssistantPanel::OnRequestDslPatchFixClicked()
{
	if (CurrentDslSteps.Num() == 0)
	{
		ResponseText = TEXT("当前没有 DSL 步骤，无法做增量修复。请先生成 DSL。");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}
	if (bLlmRequestInFlight)
	{
		ResponseText = TEXT("上一条 LLM 请求仍在进行中，请稍候…");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}

	SetLlmRequestInFlight(true);

	ResponseText = TEXT("正在请求增量修复（6.B patch），请稍候...");
	UpdateFailureGuidanceFromResponseText();

	const FString StructuredFailure = BuildStructuredFailureSummaryForPatchPrompt();
	const FString UserPatchIntent = FString::Printf(
		TEXT("请根据失败信息修复 DSL（只改动必要 steps，不要全量重写）。\n\n失败摘要：%s\n\n失败结构化上下文：\n%s\n\n现有 DSL（严格 JSON）：\n%s"),
		*LastFailureDetailForGuidance,
		*StructuredFailure,
		*SerializeDslStepsToJson(CurrentDslSteps, 2));

	FBlueprintAIRequest Request;
	Request.SystemPrompt = BuildSystemPromptDslPatchJson();
	Request.UserPrompt = UserPatchIntent;
	// 增量 patch 常为整段大 JSON，避免沿用 4k/2k 等默认输出导致在 ops 中途中断
	Request.MaxOutputTokens = 16384;

	FString Kind, Model;
	GetProviderKindAndModel(Kind, Model);
	LastCategory = TEXT("dsl_patch");
	const FString SessionId = FBlueprintAIUsageLogger::Get().LogRequestStart(TEXT("dsl_patch"), Kind, Model, ClipOneLine(UserPatchIntent, 800));
	const double StartSec = FPlatformTime::Seconds();

	Provider->SendRequest(
		Request,
		[this, SessionId, StartSec](const FBlueprintAIResponse& Resp)
		{
			AsyncTask(ENamedThreads::GameThread, [this, SessionId, StartSec, Resp]()
			{
				FOptionalLlmRequestClear LlmClear{this};
				const int32 DurMs = (int32)((FPlatformTime::Seconds() - StartSec) * 1000.0);
				FBlueprintAIUsageLogger::Get().LogRequestEnd(SessionId, Resp.bSuccess, DurMs, Resp.Content.Len(), Resp.Error);

				if (!Resp.bSuccess)
				{
					ResponseText = FString::Printf(TEXT("增量修复请求失败：%s"), *Resp.Error);
					UpdateFailureGuidanceFromResponseText();
					return;
				}

				TArray<TSharedPtr<FJsonObject>> Ops;
				FString Notes;
				FString Err;
				if (!TryParseDslPatchJson(Resp.Content, Ops, Notes, Err))
				{
					FBlueprintAIUsageLogger::Get().LogDslPatchResult(SessionId, false, 0, 0, FString(), Err);
					ResponseText = FString::Printf(TEXT("增量修复解析失败：%s\n\n模型原始输出：\n%s"), *Err, *Resp.Content);
					UpdateFailureGuidanceFromResponseText();
					return;
				}

				TArray<FBlueprintDslActionStep> NewSteps = CurrentDslSteps;
				FString ApplyErr;
				FString PatchSummary;
				int32 AppliedPatchCount = 0;
				if (!ApplyDslPatchOps(NewSteps, Ops, ApplyErr, PatchSummary, AppliedPatchCount))
				{
					FBlueprintAIUsageLogger::Get().LogDslPatchResult(SessionId, false, Ops.Num(), AppliedPatchCount, PatchSummary, ApplyErr);
					ResponseText = FString::Printf(TEXT("增量修复 patch 应用失败：%s"), *ApplyErr);
					UpdateFailureGuidanceFromResponseText();
					return;
				}
				FBlueprintAIUsageLogger::Get().LogDslPatchResult(SessionId, true, Ops.Num(), AppliedPatchCount, PatchSummary, FString());

				CurrentDslSteps = NewSteps;
				CurrentDslStepSelected.SetNum(CurrentDslSteps.Num());
				for (bool& B : CurrentDslStepSelected) { B = true; }

				RebuildDslPreview();
				const FString NotesLine = Notes.IsEmpty()
					? TEXT("")
					: FString::Printf(TEXT("%s\n\n"), *Notes);
				ResponseText = FString::Printf(
					TEXT("已应用增量修复 patch：%s本次变更（%d/%d）：\n%s\n\n请复查下方步骤后再执行。"),
					*NotesLine,
					AppliedPatchCount,
					Ops.Num(),
					PatchSummary.IsEmpty() ? TEXT("（无摘要）") : *PatchSummary);
			});
		});

	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnAutoAddPrereqStepsClicked()
{
	if (PendingAutoPrereqSteps.Num() == 0 || CurrentDslSteps.Num() == 0)
	{
		ResponseText = TEXT("当前没有可自动补齐的前置项。");
		return FReply::Handled();
	}

	UBlueprint* ActiveBlueprint = FBlueprintContextCollector::GetActiveBlueprint();
	if (!ActiveBlueprint)
	{
		ResponseText = TEXT("未找到正在编辑的蓝图，无法自动补齐。请先打开目标 Blueprint。");
		return FReply::Handled();
	}

	// 将补齐 steps 插到最前面，并给它们分配唯一 stepId
	const int32 PrereqCount = PendingAutoPrereqSteps.Num();
	TArray<FBlueprintDslActionStep> NewSteps;
	NewSteps.Reserve(PendingAutoPrereqSteps.Num() + CurrentDslSteps.Num());
	for (FBlueprintDslActionStep S : PendingAutoPrereqSteps)
	{
		S.StepId.Empty(); // 统一重新分配
		NewSteps.Add(MoveTemp(S));
	}
	for (const FBlueprintDslActionStep& S : CurrentDslSteps)
	{
		NewSteps.Add(S);
	}

	// 分配 stepId：尽量用 S101/S102，避免与现有短 id 冲突
	TSet<FString> Used;
	for (const FBlueprintDslActionStep& S : NewSteps)
	{
		if (!S.StepId.IsEmpty())
		{
			Used.Add(S.StepId.TrimStartAndEnd().ToLower());
		}
	}
	int32 Next = 101;
	for (FBlueprintDslActionStep& S : NewSteps)
	{
		if (!S.StepId.IsEmpty())
		{
			continue;
		}
		while (Used.Contains(FString::Printf(TEXT("s%d"), Next)))
		{
			++Next;
		}
		S.StepId = FString::Printf(TEXT("S%d"), Next++);
		Used.Add(S.StepId.TrimStartAndEnd().ToLower());
	}

	CurrentDslSteps = MoveTemp(NewSteps);
	CurrentDslStepSelected.Init(true, CurrentDslSteps.Num());
	PendingAutoPrereqSteps.Reset();
	PendingAutoPrereqSummary.Reset();
	RebuildDslPreview();

	// 立即执行补齐 steps（并在成功后取消勾选，避免二次执行产生重复变量/函数）
	{
		const FScopedTransaction Tx(NSLOCTEXT("BlueprintAIAssistant", "AutoAddPrereq", "Blueprint AI Assistant: Auto Add Prereq"));
		TSet<UBlueprint*> BlueprintsTouched;

		for (int32 i = 0; i < PrereqCount && i < CurrentDslSteps.Num(); ++i)
		{
			FString Err;
			FDslStepFailure Fail;
			if (!FBlueprintDslExecutor::ExecuteStep(CurrentDslSteps[i], ActiveBlueprint, Err, &Fail))
			{
				ResponseText = FString::Printf(TEXT("自动补齐执行失败（第 %d 条前置）：%s\n\n提示：如果变量/函数已部分创建成功，可先检查蓝图，然后再次点击补齐或继续执行。"), i + 1, *Err);
				return FReply::Handled();
			}

			// 记录实际被修改的 Blueprint（可能是 targetBlueprint 指向的另一资产）
			const FString TargetPath = CurrentDslSteps[i].TargetBlueprintAssetPath.TrimStartAndEnd();
			if (!TargetPath.IsEmpty())
			{
				if (UObject* Loaded = LoadObject<UObject>(nullptr, *TargetPath))
				{
					if (UBlueprint* BP = Cast<UBlueprint>(Loaded))
					{
						BlueprintsTouched.Add(BP);
					}
				}
			}
			else
			{
				BlueprintsTouched.Add(ActiveBlueprint);
			}

			if (CurrentDslStepSelected.IsValidIndex(i))
			{
				CurrentDslStepSelected[i] = false;
			}
		}

		// 编译一次，确保新变量/函数进入 Skeleton，后续 CallFunction / GetVariable 能解析到
		for (UBlueprint* BP : BlueprintsTouched)
		{
			if (BP)
			{
				FKismetEditorUtilities::CompileBlueprint(BP);
			}
		}
	}

	RebuildDslPreview();
	ResponseText = TEXT("已自动补齐并执行前置步骤（变量/函数已创建并编译）。已自动取消勾选这些前置步骤，你可以直接执行剩余 DSL。");
	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnAskClicked()
{
	if (!InputTextBox.IsValid())
	{
		ResponseText = TEXT("输入框不可用。");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}

	const FString UserQuestion = InputTextBox->GetText().ToString().TrimStartAndEnd();
	if (UserQuestion.IsEmpty())
	{
		ResponseText = TEXT("请先输入问题。");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}
	if (bLlmRequestInFlight)
	{
		ResponseText = TEXT("上一条 LLM 请求仍在进行中，请稍候…");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}

	SetLlmRequestInFlight(true);

	FBlueprintEditorContext Context;
	FString ContextError;
	const bool bHasContext = FBlueprintContextCollector::TryCollectCurrentContext(Context, ContextError);

	ResponseText = TEXT("请求中，请稍候...");
	UpdateFailureGuidanceFromResponseText();

	FString BaseUserPrompt;
	if (bHasContext)
	{
		BaseUserPrompt = FBlueprintPromptBuilder::BuildUserPrompt(Context, UserQuestion);
	}
	else
	{
		BaseUserPrompt = FString::Printf(
			TEXT("当前无法获取蓝图上下文（原因：%s）。请以通用方式回答。\n\n用户问题：%s"),
			*ContextError,
			*UserQuestion);
	}

	FBlueprintAIRequest Request;
	Request.SystemPrompt = FBlueprintPromptBuilder::BuildSystemPrompt();
	Request.UserPrompt = AugmentUserPromptWithMultiTurn(BaseUserPrompt, CurrentDslSteps);

	// 避免上下文过大导致部分网关/服务端拒绝或连接异常（豆包 Responses 对大 body 更敏感）
	int32 MaxPromptChars = 8000;
	if (const UBlueprintAIAssistantSettings* AISettings = GetDefault<UBlueprintAIAssistantSettings>())
	{
		if (AISettings->ProviderKind == EBlueprintAIProviderKind::Doubao)
		{
			MaxPromptChars = 3500;
		}
	}
	if (Request.UserPrompt.Len() > MaxPromptChars)
	{
		Request.UserPrompt = Request.UserPrompt.Left(MaxPromptChars) + TEXT("\n\n(已截断上下文，避免请求过大)");
	}

	FString Kind, Model;
	GetProviderKindAndModel(Kind, Model);
	LastUserPrompt = UserQuestion;
	LastProviderKind = Kind;
	LastProviderModel = Model;
	LastCategory = TEXT("ask");
	const FString SessionId = FBlueprintAIUsageLogger::Get().LogRequestStart(TEXT("ask"), Kind, Model, UserQuestion);
	const double StartSec = FPlatformTime::Seconds();

	Provider->SendRequest(
		Request,
		[this, SessionId, StartSec, UserQuestion](const FBlueprintAIResponse& Response)
		{
			AsyncTask(ENamedThreads::GameThread, [this, Response, SessionId, StartSec, UserQuestion]()
			{
				FOptionalLlmRequestClear LlmClear{this};
				if (Response.bSuccess)
				{
					ResponseText = Response.Content;
					RegisterMultiTurnExchange(UserQuestion, Response.Content);
				}
				else
				{
					ResponseText = FString::Printf(TEXT("请求失败：%s"), *Response.Error);
					NoteFailureHintForNextTurn(Response.Error);
					RegisterMultiTurnExchange(
						UserQuestion,
						FString::Printf(TEXT("（请求失败）%s"), *Response.Error));
				}
				const int32 DurMs = (int32)((FPlatformTime::Seconds() - StartSec) * 1000.0);
				FBlueprintAIUsageLogger::Get().LogRequestEnd(SessionId, Response.bSuccess, DurMs, Response.Content.Len(), Response.Error);
				UE_LOG(LogBlueprintAIAssistant, Log, TEXT("AI response completed. success=%s"), Response.bSuccess ? TEXT("true") : TEXT("false"));
			});
		});

	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnGenerateStepsClicked()
{
	if (!InputTextBox.IsValid())
	{
		ResponseText = TEXT("输入框不可用。");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}

	const FString UserQuestion = InputTextBox->GetText().ToString().TrimStartAndEnd();
	if (UserQuestion.IsEmpty())
	{
		ResponseText = TEXT("请先输入问题。");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}
	if (bLlmRequestInFlight)
	{
		ResponseText = TEXT("上一条 LLM 请求仍在进行中，请稍候…");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}

	SetLlmRequestInFlight(true);

	FBlueprintEditorContext Context;
	FString ContextError;
	const bool bHasContext = FBlueprintContextCollector::TryCollectCurrentContext(Context, ContextError);

	ResponseText = TEXT("请求中，请稍候...");
	UpdateFailureGuidanceFromResponseText();

	FString BaseUserPrompt;
	if (bHasContext)
	{
		BaseUserPrompt = FBlueprintPromptBuilder::BuildUserPrompt(Context, UserQuestion);
	}
	else
	{
		BaseUserPrompt = FString::Printf(
			TEXT("当前无法获取蓝图上下文（原因：%s）。请仍然给出通用的步骤清单。\n\n用户问题：%s"),
			*ContextError,
			*UserQuestion);
	}

	FBlueprintAIRequest Request;
	Request.SystemPrompt = FBlueprintPromptBuilder::BuildSystemPromptGuidedStepsJson();
	Request.UserPrompt = AugmentUserPromptWithMultiTurn(BaseUserPrompt, CurrentDslSteps);

	FString Kind, Model;
	GetProviderKindAndModel(Kind, Model);
	LastUserPrompt = UserQuestion;
	LastProviderKind = Kind;
	LastProviderModel = Model;
	LastCategory = TEXT("guided_steps");
	const FString SessionId = FBlueprintAIUsageLogger::Get().LogRequestStart(TEXT("guided_steps"), Kind, Model, UserQuestion);
	const double StartSec = FPlatformTime::Seconds();

	Provider->SendRequest(
		Request,
		[this, SessionId, StartSec, UserQuestion](const FBlueprintAIResponse& Response)
		{
			AsyncTask(ENamedThreads::GameThread, [this, Response, SessionId, StartSec, UserQuestion]()
			{
				FOptionalLlmRequestClear LlmClear{this};
				const int32 DurMs = (int32)((FPlatformTime::Seconds() - StartSec) * 1000.0);
				FBlueprintAIUsageLogger::Get().LogRequestEnd(SessionId, Response.bSuccess, DurMs, Response.Content.Len(), Response.Error);

				if (!Response.bSuccess)
				{
					ResponseText = FString::Printf(TEXT("请求失败：%s"), *Response.Error);
					NoteFailureHintForNextTurn(Response.Error);
					RegisterMultiTurnExchange(
						UserQuestion,
						FString::Printf(TEXT("（请求失败）%s"), *Response.Error));
					UpdateFailureGuidanceFromResponseText();
					return;
				}

				TArray<FString> Steps;
				FString Error;
				if (TryExtractStepsJson(Response.Content, Steps, Error))
				{
					FString Out;
					Out += TEXT("步骤清单（按顺序操作）：\n");
					for (int32 i = 0; i < Steps.Num(); ++i)
					{
						Out += FString::Printf(TEXT("%d. %s\n"), i + 1, *Steps[i]);
					}
					ResponseText = Out;
					RegisterMultiTurnExchange(UserQuestion, Out);
					UpdateFailureGuidanceFromResponseText();
				}
				else
				{
					ResponseText = FString::Printf(TEXT("步骤清单解析失败：%s\n\n模型原始输出：\n%s"), *Error, *Response.Content);
					NoteFailureHintForNextTurn(Error);
					RegisterMultiTurnExchange(UserQuestion, FString::Printf(TEXT("（解析失败）%s"), *Error));
					UpdateFailureGuidanceFromResponseText();
				}
			});
		});

	return FReply::Handled();
}

void SBlueprintAIAssistantPanel::RebuildDslPreview()
{
	if (!DslStepsBox.IsValid())
	{
		return;
	}

	DslStepsBox->ClearChildren();

	if (CurrentDslSteps.Num() == 0)
	{
		DslStepsBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(FText::FromString(TEXT("暂无 DSL 步骤。点击“生成可执行步骤（DSL）”后会显示在这里。")))
		];
		return;
	}

	// 自动补齐：缺变量/缺自定义函数 → 一键插入 CreateMemberVariable/CreateFunctionGraph
	PendingAutoPrereqSteps.Reset();
	PendingAutoPrereqSummary.Reset();
	if (UBlueprint* BP = FBlueprintContextCollector::GetActiveBlueprint())
	{
		BuildAutoPrereqFixSteps(BP, CurrentDslSteps, PendingAutoPrereqSteps, PendingAutoPrereqSummary);
	}

	const FString ManualPrereq = BuildManualPrereqSummaryFromSteps(CurrentDslSteps);
	if (!PendingAutoPrereqSummary.IsEmpty() || !ManualPrereq.IsEmpty())
	{
		FString Combined = PendingAutoPrereqSummary;
		if (!ManualPrereq.IsEmpty())
		{
			if (!Combined.IsEmpty())
			{
				Combined += TEXT("\n\n");
			}
			Combined += ManualPrereq;
		}

		DslStepsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(SBorder)
			.Padding(8.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FLinearColor(1.0f, 0.82f, 0.25f, 1.0f))
					.Text(FText::FromString(Combined))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Visibility_Lambda([this]()
					{
						return PendingAutoPrereqSteps.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.IsEnabled_Lambda([this]() { return !bLlmRequestInFlight && CurrentDslSteps.Num() > 0; })
					.Text(FText::FromString(TEXT("一键补齐前置（自动创建缺失变量/函数）")))
					.ToolTipText(FText::FromString(TEXT("将在 DSL 最前面插入 CreateMemberVariable / CreateFunctionGraph 步骤，并刷新预览。")))
					.OnClicked(this, &SBlueprintAIAssistantPanel::OnAutoAddPrereqStepsClicked)
				]
			]
		];
	}

	for (int32 i = 0; i < CurrentDslSteps.Num(); ++i)
	{
		const int32 StepIndex = i;
		const FBlueprintDslActionStep& Step = CurrentDslSteps[StepIndex];
		const FString Desc = Step.Description.IsEmpty() ? TEXT("(无描述)") : Step.Description;
		const FString Line = FString::Printf(
			TEXT("%d) [%s] %s"),
			StepIndex + 1,
			*DslActionTypeToDisplayString(Step.ActionType),
			*Desc);

		DslStepsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			.Padding(0.0f, 2.0f, 6.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, StepIndex]()
				{
					return (CurrentDslStepSelected.IsValidIndex(StepIndex) && CurrentDslStepSelected[StepIndex])
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, StepIndex](ECheckBoxState NewState)
				{
					if (CurrentDslStepSelected.IsValidIndex(StepIndex))
					{
						CurrentDslStepSelected[StepIndex] = (NewState == ECheckBoxState::Checked);
					}
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString(Line))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Top)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("执行")))
				.OnClicked(FOnClicked::CreateLambda([this, StepIndex]()
				{
					return OnExecuteOneDslStepClicked(StepIndex);
				}))
			]
		];
	}
}

FReply SBlueprintAIAssistantPanel::OnTestConnectionClicked()
{
	if (bLlmRequestInFlight)
	{
		ResponseText = TEXT("上一条 LLM 请求仍在进行中，请稍候…");
		return FReply::Handled();
	}

	SetLlmRequestInFlight(true);

	ResponseText = TEXT("正在测试连接，请稍候...");

	FBlueprintAIRequest Request;
	Request.SystemPrompt = TEXT("You are a connectivity test assistant. Reply with exactly: OK");
	Request.UserPrompt = TEXT("Return exactly OK");

	FString Kind, Model;
	GetProviderKindAndModel(Kind, Model);
	const FString SessionId = FBlueprintAIUsageLogger::Get().LogRequestStart(TEXT("ping"), Kind, Model, Request.UserPrompt);
	const double StartSec = FPlatformTime::Seconds();

	Provider->SendRequest(
		Request,
		[this, SessionId, StartSec](const FBlueprintAIResponse& Response)
		{
			AsyncTask(ENamedThreads::GameThread, [this, Response, SessionId, StartSec]()
			{
				FOptionalLlmRequestClear LlmClear{this};
				const int32 DurMs = (int32)((FPlatformTime::Seconds() - StartSec) * 1000.0);
				FBlueprintAIUsageLogger::Get().LogRequestEnd(SessionId, Response.bSuccess, DurMs, Response.Content.Len(), Response.Error);

				if (Response.bSuccess)
				{
					ResponseText = FString::Printf(
						TEXT("连接成功。模型已响应：%s"),
						*Response.Content.Left(80));
				}
				else
				{
					ResponseText = FString::Printf(TEXT("连接失败：%s"), *Response.Error);
				}
			});
		});

	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnGenerateDslClicked()
{
	if (!InputTextBox.IsValid())
	{
		ResponseText = TEXT("输入框不可用。");
		return FReply::Handled();
	}

	const FString UserQuestion = InputTextBox->GetText().ToString().TrimStartAndEnd();
	if (UserQuestion.IsEmpty())
	{
		ResponseText = TEXT("请先输入问题。");
		return FReply::Handled();
	}
	if (bLlmRequestInFlight)
	{
		ResponseText = TEXT("上一条 LLM 请求仍在进行中，请稍候…");
		return FReply::Handled();
	}

	const ETriageDecision Decision = TriageUserIntentForDsl(UserQuestion);
	if (Decision == ETriageDecision::DirectDsl)
	{
		StartGenerateDslWithQuestion(UserQuestion);
	}
	else if (Decision == ETriageDecision::NeedPlan)
	{
		RequestPlanForQuestion(UserQuestion);
	}
	else
	{
		RequestClarifyForQuestion(UserQuestion);
	}

	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnGenerateDslFromClarifyClicked()
{
	if (PendingClarifyQuestions.Num() == 0 || PendingClarifyOriginalQuestion.IsEmpty())
	{
		ResponseText = TEXT("当前没有待回答的澄清问题。");
		return FReply::Handled();
	}
	if (bLlmRequestInFlight)
	{
		ResponseText = TEXT("上一条 LLM 请求仍在进行中，请稍候…");
		return FReply::Handled();
	}

	FString Extra;
	for (const FClarifyQuestion& Q : PendingClarifyQuestions)
	{
		if (!Q.Answer.TrimStartAndEnd().IsEmpty())
		{
			Extra += FString::Printf(TEXT("- %s：%s\n"), *Q.Question, *Q.Answer.TrimStartAndEnd());
		}
	}
	if (Extra.IsEmpty())
	{
		ResponseText = TEXT("请先填写至少 1 条补充信息。");
		return FReply::Handled();
	}

	const FString Effective = PendingClarifyOriginalQuestion
		+ TEXT("\n\n补充信息：\n")
		+ Extra;

	StartGenerateDslWithQuestion(Effective);
	return FReply::Handled();
}

void SBlueprintAIAssistantPanel::StartGenerateDslWithQuestion(const FString& EffectiveQuestion)
{
	const FString UserQuestion = EffectiveQuestion.TrimStartAndEnd();
	if (UserQuestion.IsEmpty())
	{
		ResponseText = TEXT("请先输入问题。");
		UpdateFailureGuidanceFromResponseText();
		return;
	}
	if (bLlmRequestInFlight)
	{
		ResponseText = TEXT("上一条 LLM 请求仍在进行中，请稍候…");
		UpdateFailureGuidanceFromResponseText();
		return;
	}

	SetLlmRequestInFlight(true);

	// 清理 Phase 6 相关 UI 状态（避免旧问题残留影响新一轮）
	PendingClarifyQuestions.Reset();
	PendingClarifyOriginalQuestion.Empty();
	if (ClarifyBox.IsValid())
	{
		ClarifyBox->ClearChildren();
	}
	PendingPlanItems.Reset();
	if (PlanItemsBox.IsValid())
	{
		PlanItemsBox->ClearChildren();
	}

	FBlueprintEditorContext Context;
	FString ContextError;
	const bool bHasContext = FBlueprintContextCollector::TryCollectCurrentContext(Context, ContextError);

	ResponseText = TEXT("请求中，请稍候...");
	UpdateFailureGuidanceFromResponseText();
	const TArray<FBlueprintDslActionStep> PrevDslForMultiturn = CurrentDslSteps;
	CurrentDslSteps.Reset();
	CurrentDslStepSelected.Reset();
	RebuildDslPreview();

	FString BaseUserPrompt;
	if (bHasContext)
	{
		BaseUserPrompt = FBlueprintPromptBuilder::BuildUserPrompt(Context, UserQuestion);
	}
	else
	{
		BaseUserPrompt = FString::Printf(
			TEXT("当前无法获取蓝图上下文（原因：%s）。请仍然输出可执行的通用 DSL（基于 EventGraph）。\n\n用户问题：%s"),
			*ContextError,
			*UserQuestion);
	}

	FBlueprintAIRequest Request;
	Request.SystemPrompt = FBlueprintPromptBuilder::BuildSystemPromptDslJson();
	Request.UserPrompt = AugmentUserPromptWithMultiTurn(BaseUserPrompt, PrevDslForMultiturn);

	int32 MaxPromptChars = 8000;
	if (const UBlueprintAIAssistantSettings* AISettings = GetDefault<UBlueprintAIAssistantSettings>())
	{
		if (AISettings->ProviderKind == EBlueprintAIProviderKind::Doubao)
		{
			MaxPromptChars = 3500;
		}
	}
	if (Request.UserPrompt.Len() > MaxPromptChars)
	{
		Request.UserPrompt = Request.UserPrompt.Left(MaxPromptChars) + TEXT("\n\n(已截断上下文，避免请求过大)");
	}

	FString Kind, Model;
	GetProviderKindAndModel(Kind, Model);
	LastUserPrompt = UserQuestion;
	LastProviderKind = Kind;
	LastProviderModel = Model;
	LastCategory = TEXT("dsl");
	TransactionsSinceDslGenerated = 0;
	const FString SessionId = FBlueprintAIUsageLogger::Get().LogRequestStart(TEXT("dsl"), Kind, Model, UserQuestion);
	CurrentDslSessionId = SessionId;
	const double StartSec = FPlatformTime::Seconds();

	using FSendDslRequestFn = TFunction<void(const FBlueprintAIRequest& InRequest, int32 Attempt, const FString& PrevParseError, const FString& PrevRaw)>;
	TSharedPtr<FSendDslRequestFn> SendDslRequestPtr = MakeShared<FSendDslRequestFn>();
	*SendDslRequestPtr = [this, SessionId, StartSec, SendDslRequestPtr, UserQuestion](const FBlueprintAIRequest& InRequest, int32 Attempt, const FString& PrevParseError, const FString& PrevRaw)
	{
		Provider->SendRequest(
			InRequest,
			[this, SessionId, StartSec, Attempt, PrevParseError, PrevRaw, SendDslRequestPtr, UserQuestion](const FBlueprintAIResponse& Response)
			{
				AsyncTask(ENamedThreads::GameThread, [this, Response, SessionId, StartSec, Attempt, PrevParseError, PrevRaw, SendDslRequestPtr, UserQuestion]()
				{
					FOptionalLlmRequestClear LlmClear{this};
					const int32 DurMs = (int32)((FPlatformTime::Seconds() - StartSec) * 1000.0);
					FBlueprintAIUsageLogger::Get().LogRequestEnd(SessionId, Response.bSuccess, DurMs, Response.Content.Len(), Response.Error);

					if (!Response.bSuccess)
					{
						ResponseText = FString::Printf(TEXT("请求失败：%s"), *Response.Error);
						NoteFailureHintForNextTurn(Response.Error);
						RegisterMultiTurnExchange(UserQuestion, FString::Printf(TEXT("（请求失败）%s"), *Response.Error));
						UpdateFailureGuidanceFromResponseText();
						return;
					}

					TArray<FBlueprintDslActionStep> Steps;
					FString Error;
					const bool bParsed = ParseDslStepsFromJson(Response.Content, Steps, Error);
					FBlueprintAIUsageLogger::Get().LogDslParsed(SessionId, bParsed ? Steps.Num() : 0, bParsed, Error);

					if (bParsed)
					{
						FBlueprintAIUsageLogger::Get().LogRetry(
							SessionId,
							TEXT("parse_retry"),
							/*triggered*/ Attempt > 0,
							/*succeeded*/ Attempt > 0);

						CurrentDslSteps = MoveTemp(Steps);
						CurrentDslStepSelected.Init(true, CurrentDslSteps.Num());
						ResponseText = (Attempt == 0)
							? TEXT("DSL 已生成。请在下方勾选步骤后点「执行所选 DSL 步骤」，或逐行点「执行」。")
							: TEXT("DSL 已自动修复并生成。请在下方勾选步骤后点「执行所选 DSL 步骤」，或逐行点「执行」。");

						if (LooksLikePlaceholderOnlyDsl(CurrentDslSteps))
						{
							ResponseText += TEXT("\n\n[提示] 当前 DSL 更像“占位示意”（主要是 PrintString/Delay/Comment），可能并未实现实际玩法逻辑。"
								"建议：补充触发事件/目标 Actor 类/变量名/Widget，或直接使用上方的场景模板（如“金币拾取”）。");
						}

						RebuildDslPreview();
						RegisterMultiTurnExchange(
							UserQuestion,
							FString::Printf(TEXT("（已生成 DSL：%d 步）"), CurrentDslSteps.Num()));
						ClearFailureGuidanceState();
						return;
					}

					// 解析失败自动重试一次（最多 1 次）
					const UBlueprintAIAssistantSettings* AISettings = GetDefault<UBlueprintAIAssistantSettings>();
					const bool bAllowRetry = (Attempt == 0) && (!AISettings || AISettings->bAutoRetryOnceOnDslParseFailure);
					if (bAllowRetry)
					{
						FBlueprintAIUsageLogger::Get().LogRetry(SessionId, TEXT("parse_retry"), /*triggered*/ true, /*succeeded*/ false);
						ResponseText = TEXT("DSL 解析失败，正在自动修复并重试一次...");
						UpdateFailureGuidanceFromResponseText();

						auto Clip = [](const FString& S) -> FString
						{
							const int32 MaxLen = 1400;
							if (S.Len() <= MaxLen)
							{
								return S;
							}
							return S.Left(900) + TEXT("\n...\n") + S.Right(450);
						};

						FBlueprintAIRequest RetryReq;
						RetryReq.SystemPrompt = FBlueprintPromptBuilder::BuildSystemPromptDslJson();
						RetryReq.UserPrompt = FString::Printf(
							TEXT("你上一次输出的 DSL JSON 无法被解析或不符合 schema。\n")
							TEXT("解析错误：%s\n\n")
							TEXT("请你【只输出】修正后的 DSL JSON（一个 JSON 对象，包含 version 与 steps 数组）。不要输出解释、不要输出 Markdown 代码块。\n\n")
							TEXT("上一次原始输出（节选）：\n%s"),
							*Error,
							*Clip(Response.Content));
						RetryReq.MaxOutputTokens = 16384;

						LlmClear.Abandon();
						(*SendDslRequestPtr)(RetryReq, /*Attempt*/ 1, Error, Response.Content);
						return;
					}

					ResponseText = (Attempt == 0)
						? FString::Printf(TEXT("DSL 解析失败：%s\n\n模型原始输出：\n%s"), *Error, *Response.Content)
						: FString::Printf(TEXT("DSL 自动修复失败（已重试 1 次）。\n第一次解析错误：%s\n第二次解析错误：%s\n\n第二次原始输出：\n%s"), *PrevParseError, *Error, *Response.Content);
					NoteFailureHintForNextTurn(Error);
					RegisterMultiTurnExchange(UserQuestion, FString::Printf(TEXT("（DSL 解析失败）%s"), *Error));
					UpdateFailureGuidanceFromResponseText();
				});
			});
	};

	(*SendDslRequestPtr)(Request, /*Attempt*/ 0, /*PrevParseError*/ FString(), /*PrevRaw*/ FString());
}

void SBlueprintAIAssistantPanel::RequestClarifyForQuestion(const FString& UserQuestion)
{
	if (bLlmRequestInFlight)
	{
		ResponseText = TEXT("上一条 LLM 请求仍在进行中，请稍候…");
		return;
	}

	SetLlmRequestInFlight(true);

	PendingPlanItems.Reset();
	if (PlanItemsBox.IsValid())
	{
		PlanItemsBox->ClearChildren();
	}

	PendingClarifyOriginalQuestion = UserQuestion;
	PendingClarifyQuestions.Reset();
	if (ClarifyBox.IsValid())
	{
		ClarifyBox->ClearChildren();
	}

	FBlueprintEditorContext Context;
	FString ContextError;
	const bool bHasContext = FBlueprintContextCollector::TryCollectCurrentContext(Context, ContextError);
	const TArray<FBlueprintDslActionStep> PrevDslForMultiturn = CurrentDslSteps;

	FString BaseUserPrompt;
	if (bHasContext)
	{
		BaseUserPrompt = FBlueprintPromptBuilder::BuildUserPrompt(Context, UserQuestion);
	}
	else
	{
		BaseUserPrompt = FString::Printf(
			TEXT("当前无法获取蓝图上下文（原因：%s）。请仍然基于通用蓝图给出澄清问题。\n\n用户问题：%s"),
			*ContextError,
			*UserQuestion);
	}

	FBlueprintAIRequest Req;
	Req.SystemPrompt = FBlueprintPromptBuilder::BuildSystemPromptClarifyJson();
	Req.UserPrompt = AugmentUserPromptWithMultiTurn(BaseUserPrompt, PrevDslForMultiturn);
	ResponseText = TEXT("检测到需求信息可能不足：正在生成澄清问题...");

	FString Kind, Model;
	GetProviderKindAndModel(Kind, Model);
	LastUserPrompt = UserQuestion;
	LastProviderKind = Kind;
	LastProviderModel = Model;
	LastCategory = TEXT("clarify");
	const FString SessionId = FBlueprintAIUsageLogger::Get().LogRequestStart(TEXT("clarify"), Kind, Model, UserQuestion);
	const double StartSec = FPlatformTime::Seconds();

	Provider->SendRequest(
		Req,
		[this, SessionId, StartSec, UserQuestion](const FBlueprintAIResponse& Response)
		{
			AsyncTask(ENamedThreads::GameThread, [this, SessionId, StartSec, UserQuestion, Response]()
			{
				FOptionalLlmRequestClear LlmClear{this};
				const int32 DurMs = (int32)((FPlatformTime::Seconds() - StartSec) * 1000.0);
				FBlueprintAIUsageLogger::Get().LogRequestEnd(SessionId, Response.bSuccess, DurMs, Response.Content.Len(), Response.Error);

				if (!Response.bSuccess)
				{
					ResponseText = FString::Printf(TEXT("请求失败：%s"), *Response.Error);
					NoteFailureHintForNextTurn(Response.Error);
					RegisterMultiTurnExchange(UserQuestion, FString::Printf(TEXT("（请求失败）%s"), *Response.Error));
					return;
				}

				TArray<FClarifyQuestion> Qs;
				FString ParseErr;
				if (!TryParseClarifyJson(Response.Content, Qs, ParseErr))
				{
					ResponseText = FString::Printf(TEXT("澄清问题解析失败：%s\n\n模型原始输出：\n%s"), *ParseErr, *Response.Content);
					NoteFailureHintForNextTurn(ParseErr);
					RegisterMultiTurnExchange(UserQuestion, FString::Printf(TEXT("（澄清解析失败）%s"), *ParseErr));
					return;
				}

				PendingClarifyOriginalQuestion = UserQuestion;
				PendingClarifyQuestions = MoveTemp(Qs);

				if (ClarifyBox.IsValid())
				{
					ClarifyBox->ClearChildren();
					for (int32 i = 0; i < PendingClarifyQuestions.Num(); ++i)
					{
						const int32 Idx = i;
						const FString QLine = FString::Printf(TEXT("%s：%s"), *PendingClarifyQuestions[Idx].Id, *PendingClarifyQuestions[Idx].Question);
						TSharedRef<SVerticalBox> QuestionWidget = SNew(SVerticalBox);
						QuestionWidget->AddSlot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(QLine))
						];

						if (PendingClarifyQuestions[Idx].Options.Num() > 0)
						{
							TSharedRef<SWrapBox> OptionsWrap = SNew(SWrapBox);
							OptionsWrap->AddSlot()
							.Padding(0.0f, 0.0f, 4.0f, 4.0f)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("预选：")))
							];

							for (const FString& OptionText : PendingClarifyQuestions[Idx].Options)
							{
								const FString OptionCopy = OptionText;
								OptionsWrap->AddSlot()
								.Padding(0.0f, 0.0f, 4.0f, 4.0f)
								[
									SNew(SButton)
									.Text(FText::FromString(OptionCopy))
									.OnClicked_Lambda([this, Idx, OptionCopy]()
									{
										if (PendingClarifyQuestions.IsValidIndex(Idx))
										{
											PendingClarifyQuestions[Idx].Answer = OptionCopy;
										}
										return FReply::Handled();
									})
								];
							}

							QuestionWidget->AddSlot()
							.AutoHeight()
							.Padding(0.0f, 4.0f, 0.0f, 0.0f)
							[
								OptionsWrap
							];
						}

						QuestionWidget->AddSlot()
						.AutoHeight()
						.Padding(0.0f, 2.0f, 0.0f, 0.0f)
						[
							SNew(SEditableTextBox)
							.HintText(FText::FromString(TEXT("自定义 / 编辑答案（可留空）")))
							.Text_Lambda([this, Idx]()
							{
								return PendingClarifyQuestions.IsValidIndex(Idx)
									? FText::FromString(PendingClarifyQuestions[Idx].Answer)
									: FText::GetEmpty();
							})
							.OnTextChanged_Lambda([this, Idx](const FText& NewText)
							{
								if (PendingClarifyQuestions.IsValidIndex(Idx))
								{
									PendingClarifyQuestions[Idx].Answer = NewText.ToString();
								}
							})
						];

						ClarifyBox->AddSlot()
						.AutoHeight()
						.Padding(0.0f, 4.0f)
						[
							QuestionWidget
						];
					}
				}

				ResponseText = TEXT("请先回答上方 2～4 个澄清问题，然后点击「基于补充信息生成 DSL」。");
				RegisterMultiTurnExchange(UserQuestion, TEXT("（已生成澄清问题）"));
			});
		});
}

void SBlueprintAIAssistantPanel::RequestPlanForQuestion(const FString& UserQuestion)
{
	if (bLlmRequestInFlight)
	{
		ResponseText = TEXT("上一条 LLM 请求仍在进行中，请稍候…");
		return;
	}

	SetLlmRequestInFlight(true);

	PendingClarifyQuestions.Reset();
	PendingClarifyOriginalQuestion.Empty();
	if (ClarifyBox.IsValid())
	{
		ClarifyBox->ClearChildren();
	}

	PendingPlanItems.Reset();
	if (PlanItemsBox.IsValid())
	{
		PlanItemsBox->ClearChildren();
	}

	FBlueprintEditorContext Context;
	FString ContextError;
	const bool bHasContext = FBlueprintContextCollector::TryCollectCurrentContext(Context, ContextError);
	const TArray<FBlueprintDslActionStep> PrevDslForMultiturn = CurrentDslSteps;

	FString BaseUserPrompt;
	if (bHasContext)
	{
		BaseUserPrompt = FBlueprintPromptBuilder::BuildUserPrompt(Context, UserQuestion);
	}
	else
	{
		BaseUserPrompt = FString::Printf(
			TEXT("当前无法获取蓝图上下文（原因：%s）。请仍然基于通用蓝图给出实施计划。\n\n用户问题：%s"),
			*ContextError,
			*UserQuestion);
	}

	FBlueprintAIRequest Req;
	Req.SystemPrompt = FBlueprintPromptBuilder::BuildSystemPromptPlanJson();
	Req.UserPrompt = AugmentUserPromptWithMultiTurn(BaseUserPrompt, PrevDslForMultiturn);
	ResponseText = TEXT("检测到可能跨蓝图/系统级需求：正在生成实施计划...");

	FString Kind, Model;
	GetProviderKindAndModel(Kind, Model);
	LastUserPrompt = UserQuestion;
	LastProviderKind = Kind;
	LastProviderModel = Model;
	LastCategory = TEXT("plan");
	const FString SessionId = FBlueprintAIUsageLogger::Get().LogRequestStart(TEXT("plan"), Kind, Model, UserQuestion);
	const double StartSec = FPlatformTime::Seconds();

	Provider->SendRequest(
		Req,
		[this, SessionId, StartSec, UserQuestion](const FBlueprintAIResponse& Response)
		{
			AsyncTask(ENamedThreads::GameThread, [this, SessionId, StartSec, UserQuestion, Response]()
			{
				FOptionalLlmRequestClear LlmClear{this};
				const int32 DurMs = (int32)((FPlatformTime::Seconds() - StartSec) * 1000.0);
				FBlueprintAIUsageLogger::Get().LogRequestEnd(SessionId, Response.bSuccess, DurMs, Response.Content.Len(), Response.Error);

				if (!Response.bSuccess)
				{
					ResponseText = FString::Printf(TEXT("请求失败：%s"), *Response.Error);
					NoteFailureHintForNextTurn(Response.Error);
					RegisterMultiTurnExchange(UserQuestion, FString::Printf(TEXT("（请求失败）%s"), *Response.Error));
					return;
				}

				FString Summary;
				TArray<FPlanItem> Items;
				FString ParseErr;
				if (!TryParsePlanJson(Response.Content, Summary, Items, ParseErr))
				{
					ResponseText = FString::Printf(TEXT("计划解析失败：%s\n\n模型原始输出：\n%s"), *ParseErr, *Response.Content);
					NoteFailureHintForNextTurn(ParseErr);
					RegisterMultiTurnExchange(UserQuestion, FString::Printf(TEXT("（计划解析失败）%s"), *ParseErr));
					return;
				}

				PendingPlanItems = MoveTemp(Items);
				if (PlanItemsBox.IsValid())
				{
					PlanItemsBox->ClearChildren();
					for (int32 i = 0; i < PendingPlanItems.Num(); ++i)
					{
						const int32 Idx = i;
						const FPlanItem& It = PendingPlanItems[Idx];
						const FString Line = FString::Printf(TEXT("%s) %s\n目标：%s"), *It.StepId, *It.Title, *It.TargetHint);
						PlanItemsBox->AddSlot()
						.AutoHeight()
						.Padding(0.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SNew(STextBlock)
								.AutoWrapText(true)
								.Text(FText::FromString(Line))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SButton)
								.IsEnabled_Lambda([this]() { return !bLlmRequestInFlight; })
								.Text_Lambda([this]()
								{
									return bLlmRequestInFlight
										? FText::FromString(TEXT("正在生成中…"))
										: FText::FromString(TEXT("生成该步 DSL"));
								})
								.ToolTipText(FText::FromString(TEXT("将该计划步作为单蓝图请求，直接生成 DSL（需要你先打开对应蓝图）。")))
								.OnClicked_Lambda([this, Idx]()
								{
									if (bLlmRequestInFlight)
									{
										return FReply::Handled();
									}
									if (PendingPlanItems.IsValidIndex(Idx))
									{
										StartGenerateDslWithQuestion(PendingPlanItems[Idx].DslPrompt);
									}
									return FReply::Handled();
								})
							]
						];
					}
				}

				ResponseText = Summary.IsEmpty()
					? TEXT("已生成跨蓝图实施计划。你可以逐步点每条的「生成该步 DSL」。")
					: FString::Printf(TEXT("计划摘要：%s\n\n已生成跨蓝图实施计划。你可以逐步点每条的「生成该步 DSL」。"), *Summary);
				RegisterMultiTurnExchange(UserQuestion, TEXT("（已生成跨蓝图实施计划）"));
			});
		});
}

FReply SBlueprintAIAssistantPanel::OnExecuteSelectedDslClicked()
{
	if (CurrentDslSteps.Num() == 0)
	{
		ResponseText = TEXT("当前没有 DSL 步骤，请先用「生成可执行步骤（DSL）」。");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}

	UBlueprint* Blueprint = FBlueprintContextCollector::GetActiveBlueprint();
	if (!Blueprint)
	{
		ResponseText = TEXT("未找到正在编辑的蓝图，无法执行 DSL。请先打开一个 Blueprint。");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}

	TArray<FBlueprintDslActionStep> Selected;
	for (int32 i = 0; i < CurrentDslSteps.Num(); ++i)
	{
		const bool bSel = CurrentDslStepSelected.IsValidIndex(i) ? CurrentDslStepSelected[i] : true;
		if (bSel)
		{
			Selected.Add(CurrentDslSteps[i]);
		}
	}

	if (Selected.Num() == 0)
	{
		ResponseText = TEXT("没有勾选任何步骤。请在 DSL 步骤预览中勾选要执行的行。");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}

	// 执行前显式提醒：若步骤里包含“需手动创建变量/函数”等前置条件，先让地编确认。
	{
		const FString ManualPrereq = BuildManualPrereqSummaryFromSteps(Selected, /*MaxItems*/ 6);
		if (!ManualPrereq.IsEmpty())
		{
			const EAppReturnType::Type Ret = FMessageDialog::Open(
				EAppMsgType::YesNo,
				FText::FromString(
					ManualPrereq +
					TEXT("\n\n建议先处理上述前置条件，再执行 DSL。\n仍要继续执行吗？")),
				NSLOCTEXT("BlueprintAIAssistant", "ManualPrereqConfirmTitle", "确认执行（检测到手动前置条件）"));
			if (Ret != EAppReturnType::Yes)
			{
				ResponseText = TEXT("已取消执行：请先按提示完成手动前置步骤（如创建变量/函数）后再试。");
				return FReply::Handled();
			}
		}
	}

	TArray<FBlueprintDslExecutor::FValidationIssue> Issues;
	FBlueprintDslExecutor::ValidateSteps(Selected, Blueprint, Issues);
	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	for (const auto& I : Issues)
	{
		if (I.Severity == FBlueprintDslExecutor::FValidationIssue::ESeverity::Error) ++ErrorCount;
		else if (I.Severity == FBlueprintDslExecutor::FValidationIssue::ESeverity::Warning) ++WarningCount;
	}
	FBlueprintAIUsageLogger::Get().LogValidate(CurrentDslSessionId, ErrorCount, WarningCount);
	if (ErrorCount > 0)
	{
		FString Msg = FString::Printf(TEXT("DSL 校验失败：Error=%d, Warning=%d。\n"), ErrorCount, WarningCount);
		for (const auto& I : Issues)
		{
			if (I.Severity == FBlueprintDslExecutor::FValidationIssue::ESeverity::Error)
			{
				Msg += FString::Printf(TEXT("- [Error] step=%d %s\n"), I.StepIndex + 1, *I.Message);
			}
		}
		ResponseText = Msg;
		NoteFailureHintForNextTurn(Msg);
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}
	if (WarningCount > 0)
	{
		bool bHasHighRisk = false;
		FString Msg = FString::Printf(TEXT("DSL 校验警告：Warning=%d。\n"), WarningCount);
		for (const auto& I : Issues)
		{
			if (I.Severity == FBlueprintDslExecutor::FValidationIssue::ESeverity::Warning)
			{
				Msg += FString::Printf(TEXT("- [Warn] step=%d %s\n"), I.StepIndex + 1, *I.Message);
				if (I.Message.Contains(TEXT("高风险")))
				{
					bHasHighRisk = true;
				}
			}
		}
		Msg += bHasHighRisk
			? TEXT("\n检测到高风险步骤。强烈建议：取消勾选该步骤后再批量执行；若确实需要执行，请改用该步骤后的「执行」按钮单步执行以便逐一观察。\n")
			: TEXT("\n仍将继续执行（如需更安全，可先取消勾选相关步骤）。\n");
		ResponseText = Msg;
		UpdateFailureGuidanceFromResponseText();
	}

	// Phase 5：高风险二次确认 UI（requiresConfirmation=true）
	{
		bool bHasRequiresConfirmation = false;
		for (const FBlueprintDslActionStep& Step : Selected)
		{
			if (Step.bRequiresConfirmation)
			{
				bHasRequiresConfirmation = true;
				break;
			}
		}

		if (bHasRequiresConfirmation)
		{
			const EAppReturnType::Type Ret = FMessageDialog::Open(
				EAppMsgType::YesNo,
				NSLOCTEXT("BlueprintAIAssistant", "HighRiskConfirmBody",
					"检测到 requiresConfirmation=true 的高风险步骤。\n\n"
					"建议：先取消勾选这些步骤，再批量执行；或改为逐行单步执行以便观察每一步变化。\n\n"
					"仍要继续批量执行吗？"),
				NSLOCTEXT("BlueprintAIAssistant", "HighRiskConfirmTitle", "确认执行高风险步骤"));

			if (Ret != EAppReturnType::Yes)
			{
				FBlueprintAIUsageLogger::Get().LogRetry(CurrentDslSessionId, TEXT("exec_retry"), /*triggered*/ false, /*succeeded*/ false);
				FBlueprintAIUsageLogger::Get().LogFailureCategory(CurrentDslSessionId, TEXT("high_risk_cancel"), TEXT("batch_high_risk_confirm_cancelled"));
				ResponseText = TEXT("已取消执行（检测到 requiresConfirmation=true 的高风险步骤）。");
				NoteFailureHintForNextTurn(TEXT("批量执行：用户取消高风险 requiresConfirmation 确认。"));
				UpdateFailureGuidanceFromResponseText();
				return FReply::Handled();
			}
		}
	}

	auto ExecOnce = [this, Blueprint](const TArray<FBlueprintDslActionStep>& StepsToRun, FString& OutSummary, int32& OutTxnDelta) -> bool
	{
		OutTxnDelta = 0;
		const int32 Before = GetUndoCountSafe();
		TArray<FDslStepFailure> ExecStepFailures;
		const bool bOk = FBlueprintDslExecutor::ExecuteSteps(StepsToRun, Blueprint, OutSummary, &ExecStepFailures);
		const int32 After = GetUndoCountSafe();
		OutTxnDelta = CalcUndoDeltaSafe(Before, After);
		// 兜底：如果引擎未计入 Undo（极少见），至少按 1 记，保证“撤销本次改动”可用
		if (OutTxnDelta <= 0)
		{
			OutTxnDelta = 1;
		}
		TransactionsSinceDslGenerated += OutTxnDelta;
		FBlueprintAIUsageLogger::Get().LogDslExecBatch(CurrentDslSessionId, StepsToRun.Num(), bOk, OutSummary);
		if (!bOk && ExecStepFailures.Num() > 0)
		{
			ApplyStructuredFailures(ExecStepFailures);
		}
		return bOk;
	};

	FString Summary;
	int32 TxnDelta = 0;
	const bool bOk = ExecOnce(Selected, Summary, TxnDelta);
	if (bOk)
	{
		FBlueprintAIUsageLogger::Get().LogRetry(CurrentDslSessionId, TEXT("exec_retry"), /*triggered*/ false, /*succeeded*/ false);
		ResponseText = FString::Printf(TEXT("%s\n%s"), TEXT("DSL 批量执行完成。"), *Summary);
		if (Summary.Contains(TEXT("自动重连")) || Summary.Contains(TEXT("已跳过（可选）")))
		{
			UpdateFailureGuidanceFromResponseText();
		}
		else
		{
			ClearFailureGuidanceState();
		}
		return FReply::Handled();
	}
	FBlueprintAIUsageLogger::Get().LogFailureCategory(CurrentDslSessionId, CategorizeFailureFromText(Summary), Summary);

	// Phase 5：执行失败自动重问一次（最多 1 次）
	const UBlueprintAIAssistantSettings* AISettings = GetDefault<UBlueprintAIAssistantSettings>();
	if (!AISettings || AISettings->bAutoRetryOnceOnDslExecFailure)
	{
		FBlueprintAIUsageLogger::Get().LogRetry(CurrentDslSessionId, TEXT("exec_retry"), /*triggered*/ true, /*succeeded*/ false);

		SetLlmRequestInFlight(true);

		// 先回滚本次批量执行（部分引擎工具会额外产生子事务；需按 delta 全撤）
		const int32 Undone = UndoTransactionsSafe(TxnDelta);
		TransactionsSinceDslGenerated = FMath::Max(0, TransactionsSinceDslGenerated - Undone);

		ResponseText = TEXT("DSL 批量执行失败，正在自动修复并重试一次...");
		UpdateFailureGuidanceFromResponseText();

		const FString OriginalDsl = SerializeDslStepsToJson(Selected, /*Version*/ 2);

		auto Clip = [](const FString& S) -> FString
		{
			const int32 MaxLen = 2400;
			if (S.Len() <= MaxLen)
			{
				return S;
			}
			return S.Left(1500) + TEXT("\n...\n") + S.Right(700);
		};

		FBlueprintAIRequest RetryReq;
		RetryReq.SystemPrompt = FBlueprintPromptBuilder::BuildSystemPromptDslJson();
		RetryReq.UserPrompt = FString::Printf(
			TEXT("下面这段 DSL steps 在 Unreal 蓝图里执行失败了。请你【只输出】修正后的 DSL JSON（一个 JSON 对象，包含 version 与 steps 数组），不要输出解释、不要输出 Markdown。\n\n")
			TEXT("执行失败摘要：\n%s\n\n")
			TEXT("原 DSL（严格 JSON）：\n%s"),
			*Clip(Summary),
			*OriginalDsl);
		RetryReq.MaxOutputTokens = 16384;

		Provider->SendRequest(
			RetryReq,
			[this, Blueprint, Selected, Summary](const FBlueprintAIResponse& Response)
			{
				AsyncTask(ENamedThreads::GameThread, [this, Blueprint, Selected, Summary, Response]()
				{
					FOptionalLlmRequestClear LlmClear{this};
					if (!Response.bSuccess)
					{
						FBlueprintAIUsageLogger::Get().LogFailureCategory(CurrentDslSessionId, TEXT("http"), Response.Error);
						ResponseText = FString::Printf(TEXT("DSL 自动修复请求失败：%s\n\n上一次执行失败摘要：\n%s"), *Response.Error, *Summary);
						UpdateFailureGuidanceFromResponseText();
						return;
					}

					TArray<FBlueprintDslActionStep> FixedSteps;
					FString ParseError;
					const bool bParsed = ParseDslStepsFromJson(Response.Content, FixedSteps, ParseError);
					FBlueprintAIUsageLogger::Get().LogDslParsed(CurrentDslSessionId, bParsed ? FixedSteps.Num() : 0, bParsed, ParseError);
					if (!bParsed)
					{
						FBlueprintAIUsageLogger::Get().LogFailureCategory(CurrentDslSessionId, TEXT("json_schema"), ParseError);
						ResponseText = FString::Printf(
							TEXT("DSL 自动修复失败（修正后的 DSL 仍无法解析）。\n解析错误：%s\n\n修正输出：\n%s\n\n上一次执行失败摘要：\n%s"),
							*ParseError, *Response.Content, *Summary);
						UpdateFailureGuidanceFromResponseText();
						return;
					}

					// 复用现有校验逻辑：若有 Error 则直接拒绝；Warning 允许继续（但高风险仍会弹窗确认）
					TArray<FBlueprintDslExecutor::FValidationIssue> Issues;
					FBlueprintDslExecutor::ValidateSteps(FixedSteps, Blueprint, Issues);
					for (const auto& I : Issues)
					{
						if (I.Severity == FBlueprintDslExecutor::FValidationIssue::ESeverity::Error)
						{
							FString Msg = TEXT("DSL 自动修复生成了不合法的步骤，已取消重试执行。\n");
							for (const auto& X : Issues)
							{
								if (X.Severity == FBlueprintDslExecutor::FValidationIssue::ESeverity::Error)
								{
									Msg += FString::Printf(TEXT("- [Error] step=%d %s\n"), X.StepIndex + 1, *X.Message);
								}
							}
							ResponseText = Msg;
							UpdateFailureGuidanceFromResponseText();
							return;
						}
					}

					// 若修复后的 DSL 仍含 requiresConfirmation，则复用同一确认策略
					bool bHasRequiresConfirmation = false;
					for (const FBlueprintDslActionStep& Step : FixedSteps)
					{
						if (Step.bRequiresConfirmation)
						{
							bHasRequiresConfirmation = true;
							break;
						}
					}
					if (bHasRequiresConfirmation)
					{
						const EAppReturnType::Type Ret = FMessageDialog::Open(
							EAppMsgType::YesNo,
							NSLOCTEXT("BlueprintAIAssistant", "HighRiskConfirmBodyRetry",
								"自动修复后的 DSL 仍包含 requiresConfirmation=true 的高风险步骤。\n\n仍要继续执行吗？"),
							NSLOCTEXT("BlueprintAIAssistant", "HighRiskConfirmTitleRetry", "确认执行高风险步骤（自动修复后）"));
						if (Ret != EAppReturnType::Yes)
						{
							FBlueprintAIUsageLogger::Get().LogRetry(CurrentDslSessionId, TEXT("exec_retry"), /*triggered*/ true, /*succeeded*/ false);
							FBlueprintAIUsageLogger::Get().LogFailureCategory(CurrentDslSessionId, TEXT("high_risk_cancel"), TEXT("retry_high_risk_confirm_cancelled"));
							ResponseText = TEXT("已取消重试执行（自动修复后的 DSL 含高风险步骤）。");
							UpdateFailureGuidanceFromResponseText();
							return;
						}
					}

					FString RetrySummary;
					const int32 Before = GetUndoCountSafe();
					TArray<FDslStepFailure> RetryFailures1;
					const bool bRetryOk = FBlueprintDslExecutor::ExecuteSteps(FixedSteps, Blueprint, RetrySummary, &RetryFailures1);
					const int32 After = GetUndoCountSafe();
					int32 RetryDelta = CalcUndoDeltaSafe(Before, After);
					if (RetryDelta <= 0) RetryDelta = 1;
					TransactionsSinceDslGenerated += RetryDelta;
					FBlueprintAIUsageLogger::Get().LogDslExecBatch(CurrentDslSessionId, FixedSteps.Num(), bRetryOk, RetrySummary);
					FBlueprintAIUsageLogger::Get().LogRetry(CurrentDslSessionId, TEXT("exec_retry"), /*triggered*/ true, /*succeeded*/ bRetryOk);
					if (!bRetryOk)
					{
						FBlueprintAIUsageLogger::Get().LogFailureCategory(CurrentDslSessionId, CategorizeFailureFromText(RetrySummary), RetrySummary);
					if (!bRetryOk && RetryFailures1.Num() > 0) { ApplyStructuredFailures(RetryFailures1); }
					}

					ResponseText = FString::Printf(
						TEXT("%s\n第一次失败摘要：\n%s\n\n重试结果：\n%s"),
						bRetryOk ? TEXT("DSL 已自动修复并重试执行完成。") : TEXT("DSL 已自动修复并重试，但仍未完成。"),
						*Summary,
						*RetrySummary);
					if (bRetryOk)
					{
						if (RetrySummary.Contains(TEXT("自动重连")) || RetrySummary.Contains(TEXT("已跳过（可选）")))
						{
							UpdateFailureGuidanceFromResponseText();
						}
						else
						{
							ClearFailureGuidanceState();
						}
					}
					else
					{
						UpdateFailureGuidanceFromResponseText();
					}
				});
			});

		return FReply::Handled();
	}

	FBlueprintAIUsageLogger::Get().LogRetry(CurrentDslSessionId, TEXT("exec_retry"), /*triggered*/ false, /*succeeded*/ false);
	ResponseText = FString::Printf(TEXT("%s\n%s"), TEXT("DSL 批量执行未完成。"), *Summary);
	NoteFailureHintForNextTurn(Summary);
	UpdateFailureGuidanceFromResponseText();
	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnExecuteOneDslStepClicked(int32 StepIndex)
{
	if (!CurrentDslSteps.IsValidIndex(StepIndex))
	{
		ResponseText = TEXT("步骤索引无效。");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}

	UBlueprint* Blueprint = FBlueprintContextCollector::GetActiveBlueprint();
	if (!Blueprint)
	{
		ResponseText = TEXT("未找到正在编辑的蓝图，无法执行 DSL。");
		UpdateFailureGuidanceFromResponseText();
		return FReply::Handled();
	}

	{
		TArray<FBlueprintDslActionStep> OneStep;
		OneStep.Add(CurrentDslSteps[StepIndex]);
		const FString ManualPrereq = BuildManualPrereqSummaryFromSteps(OneStep, /*MaxItems*/ 2);
		if (!ManualPrereq.IsEmpty())
		{
			const EAppReturnType::Type Ret = FMessageDialog::Open(
				EAppMsgType::YesNo,
				FText::FromString(
					ManualPrereq +
					TEXT("\n\n建议先处理上述前置条件，再执行该步骤。\n仍要继续执行吗？")),
				NSLOCTEXT("BlueprintAIAssistant", "ManualPrereqConfirmTitleOneStep", "确认单步执行（检测到手动前置条件）"));
			if (Ret != EAppReturnType::Yes)
			{
				ResponseText = TEXT("已取消单步执行：请先按提示完成手动前置步骤（如创建变量/函数）后再试。");
				return FReply::Handled();
			}
		}
	}

	FString Error;
	FDslStepFailure StepFailure;
	StepFailure.StepIndex = StepIndex;
	bool bOk = false;
	int32 TxnDelta = 0;
	const int32 BeforeUndo = GetUndoCountSafe();
	{
		const FScopedTransaction Tx(NSLOCTEXT("BlueprintAIAssistant", "ExecuteDslOneStep", "Blueprint AI Assistant: Execute DSL Step"));
		bOk = FBlueprintDslExecutor::ExecuteStep(CurrentDslSteps[StepIndex], Blueprint, Error, &StepFailure);
	}
	const int32 AfterUndo = GetUndoCountSafe();
	TxnDelta = CalcUndoDeltaSafe(BeforeUndo, AfterUndo);
	if (TxnDelta <= 0)
	{
		TxnDelta = 1;
	}
	if (bOk)
	{
		// 单步也做一次轻量排版（仅移动本步涉及的节点），减少连线杂乱
		TArray<FBlueprintDslActionStep> One;
		One.Add(CurrentDslSteps[StepIndex]);
		FBlueprintDslExecutor::AutoLayoutSteps(One, Blueprint);
	}
	TransactionsSinceDslGenerated += TxnDelta;
	const FString ActionLabel = DslActionTypeToDisplayString(CurrentDslSteps[StepIndex].ActionType);
	FBlueprintAIUsageLogger::Get().LogDslExecStep(CurrentDslSessionId, StepIndex + 1, ActionLabel, bOk, Error);

	if (bOk)
	{
		FBlueprintAIUsageLogger::Get().LogRetry(CurrentDslSessionId, TEXT("exec_retry"), /*triggered*/ false, /*succeeded*/ false);
		ResponseText = FString::Printf(TEXT("第 %d 步执行成功。"), StepIndex + 1);
		ClearFailureGuidanceState();
		return FReply::Handled();
	}

	if (!StepFailure.IsEmpty())
	{
		TArray<FDslStepFailure> OneFailure;
		OneFailure.Add(StepFailure);
		ApplyStructuredFailures(OneFailure);
	}

	FBlueprintAIUsageLogger::Get().LogFailureCategory(CurrentDslSessionId, CategorizeFailureFromText(Error), Error);

	// Phase 5：单步执行失败自动重问一次（最多 1 次）
	const UBlueprintAIAssistantSettings* AISettings = GetDefault<UBlueprintAIAssistantSettings>();
	if (!AISettings || AISettings->bAutoRetryOnceOnDslExecFailure)
	{
		FBlueprintAIUsageLogger::Get().LogRetry(CurrentDslSessionId, TEXT("exec_retry"), /*triggered*/ true, /*succeeded*/ false);

		SetLlmRequestInFlight(true);

		// 回滚本次单步事务（部分引擎工具会额外产生子事务；需按 delta 全撤）
		const int32 Undone = UndoTransactionsSafe(TxnDelta);
		TransactionsSinceDslGenerated = FMath::Max(0, TransactionsSinceDslGenerated - Undone);

		ResponseText = FString::Printf(TEXT("第 %d 步失败，正在自动修复并重试一次..."), StepIndex + 1);
		UpdateFailureGuidanceFromResponseText();

		TArray<FBlueprintDslActionStep> One;
		One.Add(CurrentDslSteps[StepIndex]);
		const FString OriginalDsl = SerializeDslStepsToJson(One, /*Version*/ 2);

		auto Clip = [](const FString& S) -> FString
		{
			const int32 MaxLen = 1800;
			if (S.Len() <= MaxLen)
			{
				return S;
			}
			return S.Left(1100) + TEXT("\n...\n") + S.Right(500);
		};

		FBlueprintAIRequest RetryReq;
		RetryReq.SystemPrompt = FBlueprintPromptBuilder::BuildSystemPromptDslJson();
		RetryReq.UserPrompt = FString::Printf(
			TEXT("下面这一步 DSL 在 Unreal 蓝图里执行失败了。请你【只输出】修正后的 DSL JSON（一个 JSON 对象，包含 version 与 steps 数组），不要输出解释、不要输出 Markdown。\n\n")
			TEXT("失败摘要：\n%s\n\n")
			TEXT("原 DSL（严格 JSON）：\n%s"),
			*Clip(Error),
			*OriginalDsl);
		RetryReq.MaxOutputTokens = 16384;

		Provider->SendRequest(
			RetryReq,
			[this, Blueprint, StepIndex, Error](const FBlueprintAIResponse& Response)
			{
				AsyncTask(ENamedThreads::GameThread, [this, Blueprint, StepIndex, Error, Response]()
				{
					FOptionalLlmRequestClear LlmClear{this};
					if (!Response.bSuccess)
					{
						FBlueprintAIUsageLogger::Get().LogFailureCategory(CurrentDslSessionId, TEXT("http"), Response.Error);
						ResponseText = FString::Printf(TEXT("单步自动修复请求失败：%s\n\n原失败：%s"), *Response.Error, *Error);
						UpdateFailureGuidanceFromResponseText();
						return;
					}

					TArray<FBlueprintDslActionStep> FixedSteps;
					FString ParseError;
					const bool bParsed = ParseDslStepsFromJson(Response.Content, FixedSteps, ParseError);
					FBlueprintAIUsageLogger::Get().LogDslParsed(CurrentDslSessionId, bParsed ? FixedSteps.Num() : 0, bParsed, ParseError);
					if (!bParsed)
					{
						FBlueprintAIUsageLogger::Get().LogFailureCategory(CurrentDslSessionId, TEXT("json_schema"), ParseError);
						ResponseText = FString::Printf(
							TEXT("单步自动修复失败（修正后的 DSL 仍无法解析）。\n解析错误：%s\n\n修正输出：\n%s\n\n原失败：%s"),
							*ParseError, *Response.Content, *Error);
						UpdateFailureGuidanceFromResponseText();
						return;
					}

					// 校验：若有 Error 则拒绝；Warning 允许继续（但高风险仍会弹窗确认）
					TArray<FBlueprintDslExecutor::FValidationIssue> Issues;
					FBlueprintDslExecutor::ValidateSteps(FixedSteps, Blueprint, Issues);
					for (const auto& I : Issues)
					{
						if (I.Severity == FBlueprintDslExecutor::FValidationIssue::ESeverity::Error)
						{
							FString Msg = TEXT("单步自动修复生成了不合法的步骤，已取消重试执行。\n");
							for (const auto& X : Issues)
							{
								if (X.Severity == FBlueprintDslExecutor::FValidationIssue::ESeverity::Error)
								{
									Msg += FString::Printf(TEXT("- [Error] step=%d %s\n"), X.StepIndex + 1, *X.Message);
								}
							}
							ResponseText = Msg;
							UpdateFailureGuidanceFromResponseText();
							return;
						}
					}

					// 若修复后的 DSL 仍含 requiresConfirmation，则弹窗确认
					bool bHasRequiresConfirmation = false;
					for (const FBlueprintDslActionStep& Step : FixedSteps)
					{
						if (Step.bRequiresConfirmation)
						{
							bHasRequiresConfirmation = true;
							break;
						}
					}
					if (bHasRequiresConfirmation)
					{
						const EAppReturnType::Type Ret = FMessageDialog::Open(
							EAppMsgType::YesNo,
							NSLOCTEXT("BlueprintAIAssistant", "HighRiskConfirmBodyRetryOneStep",
								"自动修复后的 DSL 仍包含 requiresConfirmation=true 的高风险步骤。\n\n仍要继续执行吗？"),
							NSLOCTEXT("BlueprintAIAssistant", "HighRiskConfirmTitleRetryOneStep", "确认执行高风险步骤（单步自动修复后）"));
						if (Ret != EAppReturnType::Yes)
						{
							FBlueprintAIUsageLogger::Get().LogRetry(CurrentDslSessionId, TEXT("exec_retry"), /*triggered*/ true, /*succeeded*/ false);
							FBlueprintAIUsageLogger::Get().LogFailureCategory(CurrentDslSessionId, TEXT("high_risk_cancel"), TEXT("retry_high_risk_confirm_cancelled_one_step"));
							ResponseText = TEXT("已取消重试执行（自动修复后的 DSL 含高风险步骤）。");
							UpdateFailureGuidanceFromResponseText();
							return;
						}
					}

					// 更新当前 DSL 预览：若只修正了 1 步，直接替换该行；否则保留原 DSL 列表但仍执行修正结果
					if (FixedSteps.Num() == 1 && CurrentDslSteps.IsValidIndex(StepIndex))
					{
						CurrentDslSteps[StepIndex] = FixedSteps[0];
						RebuildDslPreview();
					}

					FString RetrySummary;
					TArray<FDslStepFailure> RetryFailures2;
					const bool bRetryOk = FBlueprintDslExecutor::ExecuteSteps(FixedSteps, Blueprint, RetrySummary, &RetryFailures2);
					++TransactionsSinceDslGenerated;
					FBlueprintAIUsageLogger::Get().LogDslExecBatch(CurrentDslSessionId, FixedSteps.Num(), bRetryOk, RetrySummary);
					FBlueprintAIUsageLogger::Get().LogRetry(CurrentDslSessionId, TEXT("exec_retry"), /*triggered*/ true, /*succeeded*/ bRetryOk);
					if (!bRetryOk)
					{
						FBlueprintAIUsageLogger::Get().LogFailureCategory(CurrentDslSessionId, CategorizeFailureFromText(RetrySummary), RetrySummary);
					if (!bRetryOk && RetryFailures2.Num() > 0) { ApplyStructuredFailures(RetryFailures2); }
					}

					ResponseText = FString::Printf(
						TEXT("%s\n原失败：%s\n\n重试结果：\n%s"),
						bRetryOk ? TEXT("单步已自动修复并重试执行完成。") : TEXT("单步已自动修复并重试，但仍未完成。"),
						*Error,
						*RetrySummary);
					if (bRetryOk)
					{
						if (RetrySummary.Contains(TEXT("自动重连")) || RetrySummary.Contains(TEXT("已跳过（可选）")))
						{
							UpdateFailureGuidanceFromResponseText();
						}
						else
						{
							ClearFailureGuidanceState();
						}
					}
					else
					{
						UpdateFailureGuidanceFromResponseText();
					}
				});
			});

		return FReply::Handled();
	}

	FBlueprintAIUsageLogger::Get().LogRetry(CurrentDslSessionId, TEXT("exec_retry"), /*triggered*/ false, /*succeeded*/ false);
	ResponseText = FString::Printf(TEXT("第 %d 步失败：%s"), StepIndex + 1, *Error);
	NoteFailureHintForNextTurn(Error);
	UpdateFailureGuidanceFromResponseText();
	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnCopyClicked()
{
	if (ResponseText.IsEmpty())
	{
		ResponseText = TEXT("当前没有可复制的内容。");
		return FReply::Handled();
	}

	FPlatformApplicationMisc::ClipboardCopy(*ResponseText);
	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnInsertCommentClicked()
{
	UBlueprint* Blueprint = FBlueprintContextCollector::GetActiveBlueprint();
	if (!Blueprint)
	{
		ResponseText = TEXT("未找到正在编辑的蓝图，无法插入注释。");
		return FReply::Handled();
	}

	if (ResponseText.IsEmpty())
	{
		ResponseText = TEXT("当前没有可插入的建议文本。");
		return FReply::Handled();
	}

	if (Blueprint->UbergraphPages.Num() == 0 || !Blueprint->UbergraphPages[0])
	{
		ResponseText = TEXT("当前蓝图没有可写入的 EventGraph。");
		return FReply::Handled();
	}

	UEdGraph* TargetGraph = Blueprint->UbergraphPages[0];
	UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(TargetGraph);
	CommentNode->NodePosX = 200;
	CommentNode->NodePosY = 200;
	CommentNode->NodeWidth = 700;
	CommentNode->NodeHeight = 380;
	CommentNode->NodeComment = ResponseText.Left(1500);
	TargetGraph->AddNode(CommentNode, true, false);
	CommentNode->CreateNewGuid();
	CommentNode->SnapToGrid(16);
	CommentNode->PostPlacedNewNode();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	ResponseText = TEXT("已将建议插入 EventGraph 注释节点。");
	return FReply::Handled();
}

FText SBlueprintAIAssistantPanel::GetResponseText() const
{
	return FText::FromString(ResponseText);
}

TSharedRef<SWidget> SBlueprintAIAssistantPanel::BuildSceneTemplateBar()
{
	TSharedRef<SWrapBox> Wrap = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(4.0f, 4.0f));

	const TArray<FBlueprintAISceneTemplate>& Templates = FBlueprintAISceneTemplates::GetBuiltIn();
	for (int32 i = 0; i < Templates.Num(); ++i)
	{
		const int32 CapturedIndex = i;
		Wrap->AddSlot()
		[
			SNew(SButton)
			.IsEnabled_Lambda([this]() { return !bLlmRequestInFlight; })
			.ToolTipText(FText::FromString(Templates[i].Prompt))
			.Text(FText::FromString(Templates[i].Title))
			.OnClicked(FOnClicked::CreateLambda([this, CapturedIndex]()
			{
				return OnSceneTemplateClicked(CapturedIndex);
			}))
		];
	}
	return Wrap;
}

FReply SBlueprintAIAssistantPanel::OnSceneTemplateClicked(int32 TemplateIndex)
{
	const TArray<FBlueprintAISceneTemplate>& Templates = FBlueprintAISceneTemplates::GetBuiltIn();
	if (!Templates.IsValidIndex(TemplateIndex))
	{
		ResponseText = TEXT("场景模板索引无效。");
		return FReply::Handled();
	}

	const FBlueprintAISceneTemplate& Tpl = Templates[TemplateIndex];
	FBlueprintAIUsageLogger::Get().LogSceneTemplate(Tpl.Title);

	if (InputTextBox.IsValid())
	{
		InputTextBox->SetText(FText::FromString(Tpl.Prompt));
	}

	if (Tpl.bPreferDsl)
	{
		return OnGenerateDslClicked();
	}
	return OnAskClicked();
}

FReply SBlueprintAIAssistantPanel::OnOpenLogFolderClicked()
{
	const FString Dir = FBlueprintAIUsageLogger::Get().GetLogDirectory();
	IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
	FPlatformProcess::ExploreFolder(*Dir);
	ResponseText = FString::Printf(TEXT("已打开日志目录：%s"), *Dir);
	return FReply::Handled();
}

static void IncrementKpiCounter(TMap<FString, int32>& Map, const FString& Key, int32 Delta = 1)
{
	if (Key.IsEmpty())
	{
		return;
	}
	int32& Value = Map.FindOrAdd(Key);
	Value += Delta;
}

static FString PercentText(int32 Numerator, int32 Denominator)
{
	if (Denominator <= 0)
	{
		return TEXT("N/A");
	}
	const double Percent = 100.0 * (double)Numerator / (double)Denominator;
	return FString::Printf(TEXT("%.1f%%"), Percent);
}

static FString BoolStringField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName)
{
	FString Value;
	if (Obj.IsValid())
	{
		Obj->TryGetStringField(FieldName, Value);
	}
	return Value;
}

static bool IsTrueString(const FString& Value)
{
	return Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
		Value.Equals(TEXT("1"), ESearchCase::IgnoreCase);
}

static int32 IntStringField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName)
{
	FString Value;
	if (Obj.IsValid() && Obj->TryGetStringField(FieldName, Value))
	{
		return FCString::Atoi(*Value);
	}
	return 0;
}

static void AppendSortedCounterSection(FString& Md, const FString& Title, const TMap<FString, int32>& Counts, int32 MaxRows = 10)
{
	Md += FString::Printf(TEXT("\n## %s\n\n"), *Title);
	if (Counts.Num() == 0)
	{
		Md += TEXT("（暂无数据）\n");
		return;
	}

	TArray<TPair<FString, int32>> Rows;
	for (const TPair<FString, int32>& Pair : Counts)
	{
		Rows.Add(Pair);
	}
	Rows.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
	{
		return A.Value > B.Value;
	});

	const int32 Limit = FMath::Min(MaxRows, Rows.Num());
	for (int32 i = 0; i < Limit; ++i)
	{
		Md += FString::Printf(TEXT("- `%s`：%d\n"), *Rows[i].Key, Rows[i].Value);
	}
}

FReply SBlueprintAIAssistantPanel::OnGenerateKpiReportClicked()
{
	const FString Dir = FBlueprintAIUsageLogger::Get().GetLogDirectory();
	IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);

	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames, *(Dir / TEXT("usage-*.log")), true, false);
	if (FileNames.Num() == 0)
	{
		ResponseText = FString::Printf(TEXT("未找到 usage 日志：%s"), *(Dir / TEXT("usage-*.log")));
		return FReply::Handled();
	}
	FileNames.Sort();

	int32 ParsedLines = 0;
	int32 RequestStart = 0;
	int32 RequestEnd = 0;
	int32 RequestOk = 0;
	int32 RequestDurTotalMs = 0;
	int32 DslParsed = 0;
	int32 DslParsedOk = 0;
	int32 DslExecBatch = 0;
	int32 DslExecBatchOk = 0;
	int32 DslExecStep = 0;
	int32 DslExecStepOk = 0;
	int32 RetryTriggered = 0;
	int32 RetrySucceeded = 0;
	int32 PatchTotal = 0;
	int32 PatchOk = 0;
	int32 PatchOps = 0;
	int32 PatchApplied = 0;
	int32 TimeoutSuggestionCount = 0;

	TMap<FString, int32> RequestByCategory;
	TMap<FString, int32> RequestByProvider;
	TMap<FString, int32> FailureByCategory;
	TMap<FString, int32> RetryByPhase;
	TMap<FString, int32> TimeoutByReason;

	for (const FString& FileName : FileNames)
	{
		FString Content;
		const FString Path = Dir / FileName;
		if (!FFileHelper::LoadFileToString(Content, *Path))
		{
			continue;
		}

		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines, /*bCullEmpty=*/true);
		for (const FString& Line : Lines)
		{
			TSharedPtr<FJsonObject> Obj;
			const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Line);
			if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
			{
				continue;
			}
			++ParsedLines;

			FString Event;
			Obj->TryGetStringField(TEXT("event"), Event);
			if (Event.Equals(TEXT("request_start"), ESearchCase::IgnoreCase))
			{
				++RequestStart;
				IncrementKpiCounter(RequestByCategory, BoolStringField(Obj, TEXT("category")));
				IncrementKpiCounter(RequestByProvider, BoolStringField(Obj, TEXT("provider")));
			}
			else if (Event.Equals(TEXT("request_end"), ESearchCase::IgnoreCase))
			{
				++RequestEnd;
				if (IsTrueString(BoolStringField(Obj, TEXT("success"))))
				{
					++RequestOk;
				}
				RequestDurTotalMs += IntStringField(Obj, TEXT("durMs"));
			}
			else if (Event.Equals(TEXT("dsl_parsed"), ESearchCase::IgnoreCase))
			{
				++DslParsed;
				if (IsTrueString(BoolStringField(Obj, TEXT("ok"))))
				{
					++DslParsedOk;
				}
			}
			else if (Event.Equals(TEXT("dsl_exec_batch"), ESearchCase::IgnoreCase))
			{
				++DslExecBatch;
				if (IsTrueString(BoolStringField(Obj, TEXT("ok"))))
				{
					++DslExecBatchOk;
				}
			}
			else if (Event.Equals(TEXT("dsl_exec_step"), ESearchCase::IgnoreCase))
			{
				++DslExecStep;
				if (IsTrueString(BoolStringField(Obj, TEXT("ok"))))
				{
					++DslExecStepOk;
				}
			}
			else if (Event.Equals(TEXT("retry"), ESearchCase::IgnoreCase))
			{
				if (IsTrueString(BoolStringField(Obj, TEXT("triggered"))))
				{
					++RetryTriggered;
					IncrementKpiCounter(RetryByPhase, BoolStringField(Obj, TEXT("phase")));
					if (IsTrueString(BoolStringField(Obj, TEXT("succeeded"))))
					{
						++RetrySucceeded;
					}
				}
			}
			else if (Event.Equals(TEXT("failure_category"), ESearchCase::IgnoreCase))
			{
				IncrementKpiCounter(FailureByCategory, BoolStringField(Obj, TEXT("category")));
			}
			else if (Event.Equals(TEXT("dsl_patch_result"), ESearchCase::IgnoreCase))
			{
				++PatchTotal;
				if (IsTrueString(BoolStringField(Obj, TEXT("ok"))))
				{
					++PatchOk;
				}
				PatchOps += IntStringField(Obj, TEXT("opCount"));
				PatchApplied += IntStringField(Obj, TEXT("appliedCount"));
			}
			else if (Event.Equals(TEXT("timeout_suggestion"), ESearchCase::IgnoreCase))
			{
				++TimeoutSuggestionCount;
				IncrementKpiCounter(TimeoutByReason, BoolStringField(Obj, TEXT("reason")));
			}
		}
	}

	const FDateTime Now = FDateTime::Now();
	const FString OutPath = Dir / FString::Printf(TEXT("usage-kpi-report-%s.md"), *Now.ToString(TEXT("%Y%m%d-%H%M%S")));
	FString Md;
	Md += TEXT("# Blueprint AI Assistant Usage KPI 报告\n\n");
	Md += FString::Printf(TEXT("- 生成时间：%s\n"), *Now.ToIso8601());
	Md += FString::Printf(TEXT("- 扫描目录：`%s`\n"), *Dir);
	Md += FString::Printf(TEXT("- 扫描日志数：%d\n"), FileNames.Num());
	Md += FString::Printf(TEXT("- 有效事件行：%d\n\n"), ParsedLines);

	Md += TEXT("## 核心指标\n\n");
	Md += FString::Printf(TEXT("- 模型请求：%d 次，成功 %d 次，成功率 %s，平均耗时 %d ms\n"),
		RequestEnd, RequestOk, *PercentText(RequestOk, RequestEnd), RequestEnd > 0 ? RequestDurTotalMs / RequestEnd : 0);
	Md += FString::Printf(TEXT("- DSL 解析：%d 次，成功 %d 次，成功率 %s\n"),
		DslParsed, DslParsedOk, *PercentText(DslParsedOk, DslParsed));
	Md += FString::Printf(TEXT("- DSL 批量执行：%d 次，成功 %d 次，成功率 %s\n"),
		DslExecBatch, DslExecBatchOk, *PercentText(DslExecBatchOk, DslExecBatch));
	Md += FString::Printf(TEXT("- DSL 单步执行：%d 次，成功 %d 次，成功率 %s\n"),
		DslExecStep, DslExecStepOk, *PercentText(DslExecStepOk, DslExecStep));
	Md += FString::Printf(TEXT("- 自动重试：触发 %d 次，成功 %d 次，成功率 %s\n"),
		RetryTriggered, RetrySucceeded, *PercentText(RetrySucceeded, RetryTriggered));
	Md += FString::Printf(TEXT("- DSL Patch：%d 次，成功 %d 次，成功率 %s，累计应用 %d/%d 个 op\n"),
		PatchTotal, PatchOk, *PercentText(PatchOk, PatchTotal), PatchApplied, PatchOps);
	Md += FString::Printf(TEXT("- 超时建议：%d 次\n"), TimeoutSuggestionCount);

	AppendSortedCounterSection(Md, TEXT("请求类别分布"), RequestByCategory);
	AppendSortedCounterSection(Md, TEXT("Provider 分布"), RequestByProvider);
	AppendSortedCounterSection(Md, TEXT("失败归因 Top"), FailureByCategory);
	AppendSortedCounterSection(Md, TEXT("Retry 阶段分布"), RetryByPhase);
	AppendSortedCounterSection(Md, TEXT("超时建议原因"), TimeoutByReason);

	Md += TEXT("\n## 复盘建议\n\n");
	Md += TEXT("- 优先看“失败归因 Top”，把最高频类别转成节点别名、自动修正或更明确的失败引导。\n");
	Md += TEXT("- 如果 DSL 解析成功率低，优先检查模型输出 schema 与清洗规则。\n");
	Md += TEXT("- 如果执行成功率低但 patch 成功率高，说明 6.B 正在兜底，可继续扩大 patch 的失败上下文。\n");
	Md += TEXT("- 如果超时建议频繁出现，建议调整项目设置中的 LLM Timeout 或压缩 prompt payload。\n");

	if (!FFileHelper::SaveStringToFile(Md, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		ResponseText = FString::Printf(TEXT("KPI 报告写入失败：%s"), *OutPath);
		return FReply::Handled();
	}

	FPlatformProcess::ExploreFolder(*OutPath);
	ResponseText = FString::Printf(TEXT("已生成 KPI 报告：%s"), *OutPath);
	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnExportLastHttpDumpClicked()
{
	FString OutPath;
	FString OutError;
	if (!FBlueprintAIHttpProvider::ExportLastHttpResponseDump(OutPath, OutError))
	{
		ResponseText = FString::Printf(TEXT("导出失败：%s"), *OutError);
		return FReply::Handled();
	}

	FPlatformProcess::ExploreFolder(*OutPath);
	ResponseText = FString::Printf(TEXT("已导出 http dump：%s"), *OutPath);
	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnSubmitFeedbackClicked()
{
	const FDateTime Now = FDateTime::Now();
	const FString FileName = FString::Printf(
		TEXT("feedback-%04d%02d%02d-%02d%02d%02d.md"),
		Now.GetYear(), Now.GetMonth(), Now.GetDay(),
		Now.GetHour(), Now.GetMinute(), Now.GetSecond());
	const FString Dir = FBlueprintAIUsageLogger::Get().GetLogDirectory() / TEXT("feedback");
	const FString FilePath = Dir / FileName;

	FString Md;
	Md += FString::Printf(TEXT("# Blueprint AI Assistant 反馈\n\n"));
	Md += FString::Printf(TEXT("- 时间：%s\n"), *Now.ToIso8601());
	Md += FString::Printf(TEXT("- Provider：%s\n"), LastProviderKind.IsEmpty() ? TEXT("(未知)") : *LastProviderKind);
	Md += FString::Printf(TEXT("- Model：%s\n"), LastProviderModel.IsEmpty() ? TEXT("(未知)") : *LastProviderModel);
	Md += FString::Printf(TEXT("- Category：%s\n"), LastCategory.IsEmpty() ? TEXT("(未知)") : *LastCategory);
	Md += FString::Printf(TEXT("- Session：%s\n"), CurrentDslSessionId.IsEmpty() ? TEXT("(无)") : *CurrentDslSessionId);
	Md += FString::Printf(TEXT("- 本次 DSL 已执行事务数：%d\n"), TransactionsSinceDslGenerated);
	Md += TEXT("\n## 我的问题 / Prompt\n\n");
	if (LastUserPrompt.IsEmpty())
	{
		Md += TEXT("(本次未发送请求，或面板刚打开)\n");
	}
	else
	{
		Md += TEXT("```\n");
		Md += LastUserPrompt;
		Md += TEXT("\n```\n");
	}

	Md += TEXT("\n## 当前 DSL 步骤\n\n");
	if (CurrentDslSteps.Num() == 0)
	{
		Md += TEXT("(无)\n");
	}
	else
	{
		for (int32 i = 0; i < CurrentDslSteps.Num(); ++i)
		{
			const FBlueprintDslActionStep& Step = CurrentDslSteps[i];
			const FString Desc = Step.Description.IsEmpty() ? TEXT("(无描述)") : Step.Description;
			Md += FString::Printf(TEXT("%d. [%s] %s\n"), i + 1, *DslActionTypeToDisplayString(Step.ActionType), *Desc);
			if (!Step.NodeId.IsEmpty())      Md += FString::Printf(TEXT("   - nodeId: `%s`\n"), *Step.NodeId);
			if (!Step.NodeType.IsEmpty())    Md += FString::Printf(TEXT("   - nodeType: `%s`\n"), *Step.NodeType);
			if (!Step.FunctionName.IsEmpty())Md += FString::Printf(TEXT("   - functionName: `%s`\n"), *Step.FunctionName);
			if (!Step.TargetGraph.IsEmpty()) Md += FString::Printf(TEXT("   - targetGraph: `%s`\n"), *Step.TargetGraph);
			if (!Step.VarName.IsEmpty())     Md += FString::Printf(TEXT("   - varName: `%s`\n"), *Step.VarName);
			if (!Step.PinName.IsEmpty())     Md += FString::Printf(TEXT("   - pinName: `%s`\n"), *Step.PinName);
			if (!Step.FromNodeId.IsEmpty())  Md += FString::Printf(TEXT("   - fromNodeId: `%s` fromPin: `%s`\n"), *Step.FromNodeId, *Step.FromPin);
			if (!Step.ToNodeId.IsEmpty())    Md += FString::Printf(TEXT("   - toNodeId: `%s` toPin: `%s`\n"), *Step.ToNodeId, *Step.ToPin);
			if (!Step.DefaultValue.IsEmpty()) Md += FString::Printf(TEXT("   - defaultValue: `%s`\n"), *Step.DefaultValue);
			if (!Step.CommentText.IsEmpty()) Md += FString::Printf(TEXT("   - commentText: `%s`\n"), *Step.CommentText);
			if (!Step.ValueFromNodeId.IsEmpty()) Md += FString::Printf(TEXT("   - valueFromNodeId: `%s` valueFromPin: `%s`\n"), *Step.ValueFromNodeId, *Step.ValueFromPin);
			if (Step.bRequiresConfirmation)  Md += TEXT("   - requiresConfirmation: true\n");
		}
	}

	Md += TEXT("\n## 面板最近输出 / 错误\n\n");
	Md += TEXT("```\n");
	Md += ResponseText.IsEmpty() ? TEXT("(无输出)") : ResponseText;
	Md += TEXT("\n```\n");

	Md += TEXT("\n## 补充说明（地编手动填写）\n\n");
	Md += TEXT("> 你预期看到什么？实际遇到什么？哪个步骤出错？\n\n");
	Md += TEXT("- \n- \n- \n");

	IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
	const bool bOk = FFileHelper::SaveStringToFile(
		Md,
		*FilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	if (!bOk)
	{
		ResponseText = FString::Printf(TEXT("反馈文件写入失败：%s"), *FilePath);
		return FReply::Handled();
	}

	FPlatformProcess::ExploreFolder(*FilePath);
	ResponseText = FString::Printf(TEXT("已生成反馈文件：%s\n请打开后补充「补充说明」，再发到群里/邮件给开发同学。"), *FilePath);
	return FReply::Handled();
}

static FString NormalizePinToken(FString In)
{
	FString S = In.TrimStartAndEnd().ToLower();
	S.ReplaceInline(TEXT(" "), TEXT(""));
	S.ReplaceInline(TEXT("_"), TEXT(""));
	S.ReplaceInline(TEXT("-"), TEXT(""));
	S.ReplaceInline(TEXT("\t"), TEXT(""));
	return S;
}

struct FPinDumpItem
{
	FString PinName;
	FString DisplayName;
};

static void ParseDumpPinsBlock(const FString& InBlock, TArray<FPinDumpItem>& OutPins)
{
	OutPins.Reset();

	// 行格式： "  - <PinName> (display=<Display>)"
	TArray<FString> Lines;
	InBlock.ParseIntoArrayLines(Lines, /*bCullEmpty=*/true);
	for (const FString& L : Lines)
	{
		FString Line = L.TrimStartAndEnd();
		if (!Line.StartsWith(TEXT("-")) && !Line.StartsWith(TEXT("  -")))
		{
			continue;
		}
		const int32 Dash = Line.Find(TEXT("-"));
		if (Dash != INDEX_NONE)
		{
			Line = Line.Mid(Dash + 1).TrimStartAndEnd();
		}

		const int32 DispIdx = Line.Find(TEXT("(display="), ESearchCase::CaseSensitive);
		if (DispIdx == INDEX_NONE)
		{
			continue;
		}
		const FString PinName = Line.Left(DispIdx).TrimStartAndEnd();

		FString Display;
		{
			int32 Start = DispIdx + FString(TEXT("(display=")).Len();
			int32 End = Line.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
			if (End == INDEX_NONE) End = Line.Len();
			Display = Line.Mid(Start, End - Start).TrimStartAndEnd();
		}

		if (!PinName.IsEmpty())
		{
			FPinDumpItem It;
			It.PinName = PinName;
			It.DisplayName = Display;
			OutPins.Add(MoveTemp(It));
		}
	}
}

static bool TryExtractMissingPinAndDumpPins(const FString& InErrorText, FString& OutMissingPin, TArray<FPinDumpItem>& OutPins)
{
	OutMissingPin.Empty();
	OutPins.Reset();

	// 只处理 pin 不存在类错误：未找到 fromPin/toPin/pinName/valueFromPin
	static const TCHAR* Prefixes[] = { TEXT("未找到 fromPin="), TEXT("未找到 toPin="), TEXT("未找到 pinName="), TEXT("SetVariable.valueFromPin=") };
	int32 PrefixPos = INDEX_NONE;
	FString PrefixHit;
	for (const TCHAR* P : Prefixes)
	{
		PrefixPos = InErrorText.Find(P, ESearchCase::CaseSensitive);
		if (PrefixPos != INDEX_NONE)
		{
			PrefixHit = P;
			break;
		}
	}
	if (PrefixPos == INDEX_NONE)
	{
		return false;
	}

	int32 Start = PrefixPos + PrefixHit.Len();
	int32 End = InErrorText.Find(TEXT("。"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
	if (End == INDEX_NONE)
	{
		End = InErrorText.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
	}
	if (End == INDEX_NONE)
	{
		End = InErrorText.Len();
	}
	OutMissingPin = InErrorText.Mid(Start, End - Start).TrimStartAndEnd();
	if (OutMissingPin.IsEmpty())
	{
		return false;
	}

	const int32 PinsIdx = InErrorText.Find(TEXT("可用 Pins:"), ESearchCase::CaseSensitive);
	if (PinsIdx == INDEX_NONE)
	{
		return false;
	}

	FString PinsBlock = InErrorText.Mid(PinsIdx);
	const int32 NL = PinsBlock.Find(TEXT("\n"));
	if (NL != INDEX_NONE)
	{
		PinsBlock = PinsBlock.Mid(NL + 1);
	}

	ParseDumpPinsBlock(PinsBlock, OutPins);
	return OutPins.Num() > 0;
}

FReply SBlueprintAIAssistantPanel::OnGeneratePinAliasSuggestionsClicked()
{
	const FString LogFile = FBlueprintAIUsageLogger::Get().GetCurrentLogFilePath();
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *LogFile))
	{
		ResponseText = FString::Printf(TEXT("读取日志失败：%s"), *LogFile);
		return FReply::Handled();
	}

	struct FSuggestion
	{
		FString Missing;
		FString Canonical;
		FString Reason;
		int32 Count = 0;
		TArray<FString> Examples;
	};
	TMap<FString, FSuggestion> KeyToSug;

	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines, /*bCullEmpty=*/true);
	for (const FString& Line : Lines)
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Line);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
		{
			continue;
		}

		FString Event;
		Obj->TryGetStringField(TEXT("event"), Event);
		if (!Event.Equals(TEXT("dsl_exec_step"), ESearchCase::IgnoreCase) &&
			!Event.Equals(TEXT("dsl_exec_batch"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		FString OkStr;
		Obj->TryGetStringField(TEXT("ok"), OkStr);
		const bool bOk = OkStr.Equals(TEXT("true"), ESearchCase::IgnoreCase);
		if (bOk)
		{
			continue;
		}

		FString Err;
		if (!Obj->TryGetStringField(TEXT("error"), Err))
		{
			Obj->TryGetStringField(TEXT("summary"), Err);
		}
		if (Err.IsEmpty())
		{
			continue;
		}

		FString Missing;
		TArray<FPinDumpItem> Pins;
		if (!TryExtractMissingPinAndDumpPins(Err, Missing, Pins))
		{
			continue;
		}

		const FString MissingNorm = NormalizePinToken(Missing);
		for (const FPinDumpItem& P : Pins)
		{
			const FString PinNorm = NormalizePinToken(P.PinName);
			const FString DispNorm = NormalizePinToken(P.DisplayName);

			const bool bMatchesDisplay = !P.DisplayName.IsEmpty() && MissingNorm == DispNorm;
			const bool bMatchesPinName = MissingNorm == PinNorm;
			if (!bMatchesDisplay && !bMatchesPinName)
			{
				continue;
			}

			const FString Canonical = P.PinName;
			const FString Key = Missing + TEXT(" -> ") + Canonical;
			FSuggestion& S = KeyToSug.FindOrAdd(Key);
			S.Missing = Missing;
			S.Canonical = Canonical;
			S.Count += 1;
			if (S.Reason.IsEmpty())
			{
				S.Reason = bMatchesDisplay ? FString::Printf(TEXT("missing 与 displayName 归一化后相等（display=%s）"), *P.DisplayName)
					: TEXT("missing 与 PinName 归一化后相等（可能为大小写/空格差异）");
			}
			if (S.Examples.Num() < 3)
			{
				S.Examples.Add(Err.Left(240));
			}
		}
	}

	if (KeyToSug.Num() == 0)
	{
		ResponseText = TEXT("未从当前日志中找到可生成别名候选的 pin 失败记录。");
		return FReply::Handled();
	}

	const FString Dir = FBlueprintAIUsageLogger::Get().GetLogDirectory();
	const FString FileName = FString::Printf(TEXT("pin-alias-suggestions-%s.md"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
	const FString OutPath = Dir / FileName;

	TArray<FSuggestion> Sugs;
	KeyToSug.GenerateValueArray(Sugs);
	Sugs.Sort([](const FSuggestion& A, const FSuggestion& B) { return A.Count > B.Count; });

	FString Md;
	Md += TEXT("# Pin 别名候选（自动提取）\n\n");
	Md += FString::Printf(TEXT("- 生成时间：%s\n"), *FDateTime::Now().ToIso8601());
	Md += FString::Printf(TEXT("- 扫描日志：`%s`\n\n"), *LogFile);
	Md += TEXT("> 说明：这是从执行失败日志中提取的候选映射（不会自动写入代码）。建议人工确认后再合入到 Pin 别名表。\n\n");
	Md += TEXT("## 候选列表（按出现次数排序）\n\n");

	for (const FSuggestion& S : Sugs)
	{
		Md += FString::Printf(TEXT("- `%s` → `%s`（%d 次）\n"), *S.Missing, *S.Canonical, S.Count);
		Md += FString::Printf(TEXT("  - 原因：%s\n"), *S.Reason);
		for (const FString& Ex : S.Examples)
		{
			Md += FString::Printf(TEXT("  - 例：%s\n"), *Ex.Replace(TEXT("\n"), TEXT(" ")));
		}
	}

	IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
	if (!FFileHelper::SaveStringToFile(Md, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		ResponseText = FString::Printf(TEXT("写入候选报告失败：%s"), *OutPath);
		return FReply::Handled();
	}

	FPlatformProcess::ExploreFolder(*Dir);
	ResponseText = FString::Printf(TEXT("已生成 Pin 别名候选报告：%s"), *OutPath);
	return FReply::Handled();
}

static bool ParsePinAliasSuggestionsMd(const FString& Md, TArray<SBlueprintAIAssistantPanel::FPinAliasCandidate>& Out, FString& OutError)
{
	Out.Reset();
	OutError.Empty();

	TArray<FString> Lines;
	Md.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);

	// 行格式：- `Missing` → `Canonical`（N 次）
	for (FString L : Lines)
	{
		L = L.TrimStartAndEnd();
		if (!L.StartsWith(TEXT("- `")))
		{
			continue;
		}

		int32 A0 = L.Find(TEXT("`"));
		int32 A1 = (A0 != INDEX_NONE) ? L.Find(TEXT("`"), ESearchCase::CaseSensitive, ESearchDir::FromStart, A0 + 1) : INDEX_NONE;
		if (A0 == INDEX_NONE || A1 == INDEX_NONE)
		{
			continue;
		}
		const FString From = L.Mid(A0 + 1, A1 - A0 - 1);

		const int32 Arrow = L.Find(TEXT("→"), ESearchCase::CaseSensitive, ESearchDir::FromStart, A1);
		if (Arrow == INDEX_NONE)
		{
			continue;
		}
		const int32 B0 = L.Find(TEXT("`"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Arrow);
		const int32 B1 = (B0 != INDEX_NONE) ? L.Find(TEXT("`"), ESearchCase::CaseSensitive, ESearchDir::FromStart, B0 + 1) : INDEX_NONE;
		if (B0 == INDEX_NONE || B1 == INDEX_NONE)
		{
			continue;
		}
		const FString To = L.Mid(B0 + 1, B1 - B0 - 1);

		int32 Count = 1;
		const int32 Lp = L.Find(TEXT("（"), ESearchCase::CaseSensitive);
		const int32 Rp = L.Find(TEXT("）"), ESearchCase::CaseSensitive);
		if (Lp != INDEX_NONE && Rp != INDEX_NONE && Rp > Lp)
		{
			const FString Mid = L.Mid(Lp + 1, Rp - Lp - 1);
			// Mid 形如 "3 次"
			TArray<FString> Parts;
			Mid.ParseIntoArrayWS(Parts);
			if (Parts.Num() > 0)
			{
				Count = FCString::Atoi(*Parts[0]);
				if (Count <= 0) Count = 1;
			}
		}

		if (From.IsEmpty() || To.IsEmpty())
		{
			continue;
		}

		SBlueprintAIAssistantPanel::FPinAliasCandidate C;
		C.From = From;
		C.To = To;
		C.Count = Count;
		C.bSelected = true;
		Out.Add(MoveTemp(C));
	}

	if (Out.Num() == 0)
	{
		OutError = TEXT("未在 markdown 中解析到候选列表行（期望形如：- `From` → `To`（N 次））。");
		return false;
	}

	return true;
}

static FString JsonEscapeForFile(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len() + 8);
	for (int32 i = 0; i < In.Len(); ++i)
	{
		const TCHAR Ch = In[i];
		switch (Ch)
		{
		case TEXT('\\'): Out += TEXT("\\\\"); break;
		case TEXT('"'):  Out += TEXT("\\\""); break;
		case TEXT('\b'): Out += TEXT("\\b"); break;
		case TEXT('\f'): Out += TEXT("\\f"); break;
		case TEXT('\n'): Out += TEXT("\\n"); break;
		case TEXT('\r'): Out += TEXT("\\r"); break;
		case TEXT('\t'): Out += TEXT("\\t"); break;
		default:
			if (Ch < 0x20)
			{
				Out += FString::Printf(TEXT("\\u%04x"), (int32)Ch);
			}
			else
			{
				Out.AppendChar(Ch);
			}
			break;
		}
	}
	return Out;
}

static FString FindLatestSuggestionsMdFile(const FString& Dir)
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("pin-alias-suggestions-*.md")), /*Files=*/true, /*Directories=*/false);
	if (Files.Num() == 0)
	{
		return FString();
	}
	Files.Sort();
	// 文件名含时间戳，字典序即可近似 newest
	return Dir / Files.Last();
}

FReply SBlueprintAIAssistantPanel::OnImportPinAliasSuggestionsClicked()
{
	const FString Dir = FBlueprintAIUsageLogger::Get().GetLogDirectory();
	const FString FilePath = FindLatestSuggestionsMdFile(Dir);
	if (FilePath.IsEmpty())
	{
		ResponseText = FString::Printf(TEXT("未找到候选报告：%s"), *(Dir / TEXT("pin-alias-suggestions-*.md")));
		return FReply::Handled();
	}

	FString Md;
	if (!FFileHelper::LoadFileToString(Md, *FilePath))
	{
		ResponseText = FString::Printf(TEXT("读取候选报告失败：%s"), *FilePath);
		return FReply::Handled();
	}

	FString Err;
	if (!ParsePinAliasSuggestionsMd(Md, PinAliasCandidates, Err))
	{
		ResponseText = FString::Printf(TEXT("解析候选报告失败：%s\n文件：%s"), *Err, *FilePath);
		return FReply::Handled();
	}

	// 重建候选列表 UI
	if (PinAliasCandidatesBox.IsValid())
	{
		PinAliasCandidatesBox->ClearChildren();
		for (int32 i = 0; i < PinAliasCandidates.Num(); ++i)
		{
			PinAliasCandidatesBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this, i]()
					{
						return (PinAliasCandidates.IsValidIndex(i) && PinAliasCandidates[i].bSelected) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this, i](ECheckBoxState St)
					{
						if (PinAliasCandidates.IsValidIndex(i))
						{
							PinAliasCandidates[i].bSelected = (St == ECheckBoxState::Checked);
						}
					})
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(6.0f, 0.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text_Lambda([this, i]()
					{
						if (!PinAliasCandidates.IsValidIndex(i))
						{
							return FText::GetEmpty();
						}
						const auto& C = PinAliasCandidates[i];
						return FText::FromString(FString::Printf(TEXT("`%s` → `%s`（%d 次）"), *C.From, *C.To, C.Count));
					})
				]
			];
		}
	}

	ResponseText = FString::Printf(TEXT("已导入 %d 条 Pin 别名候选：%s\n请勾选后点击「写入并启用别名表」。"), PinAliasCandidates.Num(), *FilePath);
	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnWritePinAliasesJsonClicked()
{
	if (PinAliasCandidates.Num() == 0)
	{
		ResponseText = TEXT("当前没有候选可写入。请先点「导入候选」。");
		return FReply::Handled();
	}

	TArray<FPinAliasCandidate> Selected;
	for (const auto& C : PinAliasCandidates)
	{
		if (C.bSelected)
		{
			Selected.Add(C);
		}
	}
	if (Selected.Num() == 0)
	{
		ResponseText = TEXT("没有勾选任何候选。");
		return FReply::Handled();
	}

	const FString RootDir = FBlueprintAIUsageLogger::Get().GetLogDirectory();
	const FString FilePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BlueprintAIAssistant") / TEXT("pin-aliases.json"));
	IFileManager::Get().MakeDirectory(*(FPaths::GetPath(FilePath)), /*Tree=*/true);

	FString Json;
	Json += TEXT("{\n  \"version\": 1,\n  \"aliases\": [\n");
	for (int32 i = 0; i < Selected.Num(); ++i)
	{
		const auto& C = Selected[i];
		Json += FString::Printf(TEXT("    {\"from\":\"%s\",\"to\":\"%s\",\"count\":%d}%s\n"),
			*JsonEscapeForFile(C.From),
			*JsonEscapeForFile(C.To),
			C.Count,
			(i + 1 < Selected.Num()) ? TEXT(",") : TEXT(""));
	}
	Json += TEXT("  ]\n}\n");

	if (!FFileHelper::SaveStringToFile(Json, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		ResponseText = FString::Printf(TEXT("写入失败：%s"), *FilePath);
		return FReply::Handled();
	}

	FBlueprintDslExecutor::ReloadPinAliasTable();
	ResponseText = FString::Printf(TEXT("已写入并启用 Pin 别名表：%s（%d 条）"), *FilePath, Selected.Num());
	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnReloadPinAliasesClicked()
{
	FBlueprintDslExecutor::ReloadPinAliasTable();
	ResponseText = TEXT("已重新加载 Pin 别名表（Saved/BlueprintAIAssistant/pin-aliases.json）。");
	return FReply::Handled();
}

FReply SBlueprintAIAssistantPanel::OnUndoLastDslClicked()
{
	if (!GEditor)
	{
		ResponseText = TEXT("GEditor 不可用，无法执行撤销。");
		return FReply::Handled();
	}

	if (TransactionsSinceDslGenerated <= 0)
	{
		ResponseText = TEXT("没有可撤销的 DSL 改动（计数为 0）。如需继续回退，请手动 Ctrl+Z。");
		return FReply::Handled();
	}

	const int32 ToUndo = TransactionsSinceDslGenerated;
	const int32 Done = UndoTransactionsSafe(ToUndo);

	TransactionsSinceDslGenerated = FMath::Max(0, TransactionsSinceDslGenerated - Done);
	ResponseText = FString::Printf(
		TEXT("已撤销 %d / %d 次 DSL 事务。%s"),
		Done, ToUndo,
		(Done < ToUndo) ? TEXT("有撤销失败的项（可能被其他编辑器操作打断）。") : TEXT(""));
	return FReply::Handled();
}
