#include "BlueprintAIUsageLogger.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

FBlueprintAIUsageLogger& FBlueprintAIUsageLogger::Get()
{
	static FBlueprintAIUsageLogger Instance;
	return Instance;
}

FString FBlueprintAIUsageLogger::GetLogDirectory() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BlueprintAIAssistant"));
}

FString FBlueprintAIUsageLogger::GetCurrentLogFilePath()
{
	const FDateTime Now = FDateTime::Now();
	const FString FileName = FString::Printf(TEXT("usage-%04d%02d%02d.log"), Now.GetYear(), Now.GetMonth(), Now.GetDay());
	return GetLogDirectory() / FileName;
}

FString FBlueprintAIUsageLogger::JsonEscape(const FString& In)
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

FString FBlueprintAIUsageLogger::TakePreview(const FString& In, int32 MaxLen)
{
	if (In.Len() <= MaxLen)
	{
		return In;
	}
	return In.Left(MaxLen) + TEXT("...");
}

void FBlueprintAIUsageLogger::WriteLine(const TMap<FString, FString>& Fields)
{
	FString Line;
	Line.Reserve(256);
	Line += TEXT("{");
	Line += FString::Printf(TEXT("\"ts\":\"%s\""), *FDateTime::Now().ToIso8601());

	for (const auto& Pair : Fields)
	{
		Line += FString::Printf(TEXT(",\"%s\":\"%s\""), *JsonEscape(Pair.Key), *JsonEscape(Pair.Value));
	}
	Line += TEXT("}\n");

	const FString FilePath = GetCurrentLogFilePath();

	FScopeLock Lock(&Mutex);

	IFileManager::Get().MakeDirectory(*GetLogDirectory(), /*Tree=*/true);

	FFileHelper::SaveStringToFile(
		Line,
		*FilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(),
		FILEWRITE_Append);
}

FString FBlueprintAIUsageLogger::LogRequestStart(const FString& Category, const FString& ProviderKind, const FString& Model, const FString& PromptPreview)
{
	const FString SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);

	TMap<FString, FString> F;
	F.Add(TEXT("event"),    TEXT("request_start"));
	F.Add(TEXT("session"),  SessionId);
	F.Add(TEXT("category"), Category);
	F.Add(TEXT("provider"), ProviderKind);
	F.Add(TEXT("model"),    Model);
	F.Add(TEXT("prompt"),   TakePreview(PromptPreview, 400));
	WriteLine(F);
	return SessionId;
}

void FBlueprintAIUsageLogger::LogRequestEnd(const FString& SessionId, bool bSuccess, int32 DurationMs, int32 ResponseLen, const FString& ErrorPreview)
{
	TMap<FString, FString> F;
	F.Add(TEXT("event"),    TEXT("request_end"));
	F.Add(TEXT("session"),  SessionId);
	F.Add(TEXT("success"),  bSuccess ? TEXT("true") : TEXT("false"));
	F.Add(TEXT("durMs"),    FString::FromInt(DurationMs));
	F.Add(TEXT("respLen"),  FString::FromInt(ResponseLen));
	if (!bSuccess)
	{
		F.Add(TEXT("error"), TakePreview(ErrorPreview, 400));
	}
	WriteLine(F);
}

void FBlueprintAIUsageLogger::LogDslParsed(const FString& SessionId, int32 StepCount, bool bOk, const FString& Error)
{
	TMap<FString, FString> F;
	F.Add(TEXT("event"),     TEXT("dsl_parsed"));
	F.Add(TEXT("session"),   SessionId);
	F.Add(TEXT("ok"),        bOk ? TEXT("true") : TEXT("false"));
	F.Add(TEXT("stepCount"), FString::FromInt(StepCount));
	if (!bOk)
	{
		F.Add(TEXT("error"), TakePreview(Error, 300));
	}
	WriteLine(F);
}

void FBlueprintAIUsageLogger::LogValidate(const FString& SessionId, int32 ErrorCount, int32 WarningCount)
{
	TMap<FString, FString> F;
	F.Add(TEXT("event"),    TEXT("dsl_validate"));
	F.Add(TEXT("session"),  SessionId);
	F.Add(TEXT("errors"),   FString::FromInt(ErrorCount));
	F.Add(TEXT("warnings"), FString::FromInt(WarningCount));
	WriteLine(F);
}

void FBlueprintAIUsageLogger::LogDslExecBatch(const FString& SessionId, int32 TotalSteps, bool bOk, const FString& Summary)
{
	TMap<FString, FString> F;
	F.Add(TEXT("event"),    TEXT("dsl_exec_batch"));
	F.Add(TEXT("session"),  SessionId);
	F.Add(TEXT("ok"),       bOk ? TEXT("true") : TEXT("false"));
	F.Add(TEXT("total"),    FString::FromInt(TotalSteps));
	F.Add(TEXT("summary"),  TakePreview(Summary, 400));
	WriteLine(F);
}

void FBlueprintAIUsageLogger::LogDslExecStep(const FString& SessionId, int32 StepIndex, const FString& Action, bool bOk, const FString& Error)
{
	TMap<FString, FString> F;
	F.Add(TEXT("event"),   TEXT("dsl_exec_step"));
	F.Add(TEXT("session"), SessionId);
	F.Add(TEXT("step"),    FString::FromInt(StepIndex));
	F.Add(TEXT("action"),  Action);
	F.Add(TEXT("ok"),      bOk ? TEXT("true") : TEXT("false"));
	if (!bOk)
	{
		F.Add(TEXT("error"), TakePreview(Error, 300));
	}
	WriteLine(F);
}

void FBlueprintAIUsageLogger::LogSceneTemplate(const FString& TemplateTitle)
{
	TMap<FString, FString> F;
	F.Add(TEXT("event"), TEXT("scene_template"));
	F.Add(TEXT("title"), TemplateTitle);
	WriteLine(F);
}

void FBlueprintAIUsageLogger::LogRetry(const FString& SessionId, const FString& Phase, bool bTriggered, bool bSucceeded)
{
	TMap<FString, FString> F;
	F.Add(TEXT("event"), TEXT("retry"));
	F.Add(TEXT("session"), SessionId);
	F.Add(TEXT("phase"), Phase);
	F.Add(TEXT("triggered"), bTriggered ? TEXT("true") : TEXT("false"));
	F.Add(TEXT("succeeded"), bSucceeded ? TEXT("true") : TEXT("false"));
	WriteLine(F);
}

void FBlueprintAIUsageLogger::LogFailureCategory(const FString& SessionId, const FString& Category, const FString& DetailPreview)
{
	TMap<FString, FString> F;
	F.Add(TEXT("event"), TEXT("failure_category"));
	F.Add(TEXT("session"), SessionId);
	F.Add(TEXT("category"), Category);
	if (!DetailPreview.IsEmpty())
	{
		F.Add(TEXT("detail"), TakePreview(DetailPreview, 400));
	}
	WriteLine(F);
}

void FBlueprintAIUsageLogger::LogHttpExtractFailure(
	const FString& SessionId,
	const FString& ProviderKind,
	const FString& Model,
	const FString& EndpointSafeForLog,
	const FString& TopLevelKeys,
	const FString& PreviewFirst1024)
{
	TMap<FString, FString> F;
	F.Add(TEXT("event"), TEXT("http_extract_fail"));
	if (!SessionId.IsEmpty())       F.Add(TEXT("session"), SessionId);
	if (!ProviderKind.IsEmpty())    F.Add(TEXT("provider"), ProviderKind);
	if (!Model.IsEmpty())           F.Add(TEXT("model"), Model);
	if (!EndpointSafeForLog.IsEmpty()) F.Add(TEXT("endpoint"), TakePreview(EndpointSafeForLog, 200));
	if (!TopLevelKeys.IsEmpty())    F.Add(TEXT("topKeys"), TakePreview(TopLevelKeys, 200));
	if (!PreviewFirst1024.IsEmpty())F.Add(TEXT("preview"), TakePreview(PreviewFirst1024, 1024));
	WriteLine(F);
}

void FBlueprintAIUsageLogger::LogTimeoutSuggestion(
	const FString& SessionId,
	const FString& ProviderKind,
	const FString& Model,
	const FString& EndpointSafeForLog,
	int32 PayloadUtf8Bytes,
	int32 TimeoutSeconds,
	int32 SuggestedTimeoutSeconds,
	const FString& Reason,
	const FString& HttpTransportDetail)
{
	TMap<FString, FString> F;
	F.Add(TEXT("event"), TEXT("timeout_suggestion"));
	if (!SessionId.IsEmpty())          F.Add(TEXT("session"), SessionId);
	if (!ProviderKind.IsEmpty())       F.Add(TEXT("provider"), ProviderKind);
	if (!Model.IsEmpty())              F.Add(TEXT("model"), Model);
	if (!EndpointSafeForLog.IsEmpty()) F.Add(TEXT("endpoint"), TakePreview(EndpointSafeForLog, 200));
	F.Add(TEXT("payloadBytes"), FString::FromInt(FMath::Max(0, PayloadUtf8Bytes)));
	F.Add(TEXT("timeout"), FString::FromInt(FMath::Max(0, TimeoutSeconds)));
	F.Add(TEXT("suggestedTimeout"), FString::FromInt(FMath::Max(0, SuggestedTimeoutSeconds)));
	if (!Reason.IsEmpty())             F.Add(TEXT("reason"), TakePreview(Reason, 120));
	if (!HttpTransportDetail.IsEmpty()) F.Add(TEXT("httpDetail"), TakePreview(HttpTransportDetail, 500));
	WriteLine(F);
}

void FBlueprintAIUsageLogger::LogDslPatchResult(
	const FString& SessionId,
	bool bOk,
	int32 OpCount,
	int32 AppliedCount,
	const FString& Summary,
	const FString& Error)
{
	TMap<FString, FString> F;
	F.Add(TEXT("event"), TEXT("dsl_patch_result"));
	if (!SessionId.IsEmpty()) F.Add(TEXT("session"), SessionId);
	F.Add(TEXT("ok"), bOk ? TEXT("true") : TEXT("false"));
	F.Add(TEXT("opCount"), FString::FromInt(FMath::Max(0, OpCount)));
	F.Add(TEXT("appliedCount"), FString::FromInt(FMath::Max(0, AppliedCount)));
	if (!Summary.IsEmpty()) F.Add(TEXT("summary"), TakePreview(Summary, 500));
	if (!Error.IsEmpty()) F.Add(TEXT("error"), TakePreview(Error, 400));
	WriteLine(F);
}
