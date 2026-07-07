#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BlueprintDslExecutor.h"
#include "BlueprintDslTypes.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace BlueprintAIAssistantSmoke
{
	static FString GetPluginSmokeDslDir()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlueprintAIAssistant"));
		if (!Plugin.IsValid())
		{
			return FString();
		}
		return FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("SmokeDsl"));
	}

	static bool LoadJsonObjectFromFile(const FString& Path, TSharedPtr<FJsonObject>& OutObj, FString& OutError)
	{
		OutObj.Reset();
		OutError.Empty();

		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *Path))
		{
			OutError = FString::Printf(TEXT("无法读取文件：%s"), *Path);
			return false;
		}

		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Content);
		if (!FJsonSerializer::Deserialize(Reader, OutObj) || !OutObj.IsValid())
		{
			OutError = FString::Printf(TEXT("无法解析为 JSON：%s"), *Path);
			return false;
		}
		return true;
	}

	static FString SerializeJsonObject(const TSharedPtr<FJsonObject>& Obj)
	{
		if (!Obj.IsValid())
		{
			return FString();
		}
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR>> Writer = TJsonWriterFactory<TCHAR>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	static UBlueprint* CreateTempActorBlueprint(FString& OutAssetPath)
	{
		OutAssetPath.Empty();

		const FString PkgPath = TEXT("/Game/BlueprintAIAssistantSmoke");
		const FString AssetName = FString::Printf(TEXT("BP_Smoke_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		const FString FullObjectPath = PkgPath / AssetName + TEXT(".") + AssetName;
		OutAssetPath = FullObjectPath;

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UObject* ParentClass = AActor::StaticClass();

		UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
			CastChecked<UClass>(ParentClass),
			CreatePackage(*FString::Printf(TEXT("%s/%s"), *PkgPath, *AssetName)),
			FName(*AssetName),
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			FName(TEXT("BlueprintAIAssistantSmoke")));

		if (BP)
		{
			FAssetRegistryModule::AssetCreated(BP);
			BP->MarkPackageDirty();
		}
		return BP;
	}

	static void CleanupTempAssets(const TArray<FString>& ObjectPaths)
	{
		TArray<UObject*> Assets;
		for (const FString& P : ObjectPaths)
		{
			if (UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *P))
			{
				Assets.Add(Obj);
			}
		}
		if (Assets.Num() > 0)
		{
			ObjectTools::DeleteObjects(Assets, /*bShowConfirmation=*/false);
		}
	}

	static int32 CountErrors(const TArray<FBlueprintDslExecutor::FValidationIssue>& Issues)
	{
		int32 C = 0;
		for (const auto& I : Issues)
		{
			if (I.Severity == FBlueprintDslExecutor::FValidationIssue::ESeverity::Error)
			{
				++C;
			}
		}
		return C;
	}

	static FString ClipOneLine(const FString& In, int32 MaxLen)
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintAIAssistantSmokeTest,
	"BlueprintAIAssistant.Smoke.RunAllFixtures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintAIAssistantSmokeTest::RunTest(const FString& Parameters)
{
	using namespace BlueprintAIAssistantSmoke;

	const FString Dir = GetPluginSmokeDslDir();
	if (Dir.IsEmpty() || !IFileManager::Get().DirectoryExists(*Dir))
	{
		AddError(FString::Printf(TEXT("SmokeDsl 目录不存在：%s"), *Dir));
		return false;
	}

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.json")), /*Files=*/true, /*Directories=*/false);
	Files.Sort();
	if (Files.Num() == 0)
	{
		AddError(FString::Printf(TEXT("未找到 fixture：%s"), *(Dir / TEXT("*.json"))));
		return false;
	}

	for (const FString& FileName : Files)
	{
		const FString Path = Dir / FileName;

		TSharedPtr<FJsonObject> Root;
		FString LoadErr;
		if (!LoadJsonObjectFromFile(Path, Root, LoadErr))
		{
			AddError(LoadErr);
			continue;
		}

		FString CaseName;
		Root->TryGetStringField(TEXT("name"), CaseName);
		if (CaseName.IsEmpty())
		{
			CaseName = FileName;
		}

		FString Mode;
		Root->TryGetStringField(TEXT("mode"), Mode);
		Mode = Mode.TrimStartAndEnd().ToLower();

		bool bExpectParseOk = true;
		Root->TryGetBoolField(TEXT("expectParseOk"), bExpectParseOk);
		int32 ExpectValidateErrorsMax = 0;
		Root->TryGetNumberField(TEXT("expectValidateErrorsMax"), ExpectValidateErrorsMax);
		bool bExpectExecuteOk = (Mode == TEXT("execute"));
		Root->TryGetBoolField(TEXT("expectExecuteOk"), bExpectExecuteOk);

		const TSharedPtr<FJsonObject>* DslObjPtr = nullptr;
		if (!Root->TryGetObjectField(TEXT("dsl"), DslObjPtr) || !DslObjPtr || !DslObjPtr->IsValid())
		{
			AddError(FString::Printf(TEXT("[%s] 缺少 dsl 对象：%s"), *CaseName, *Path));
			continue;
		}

		const FString DslJson = SerializeJsonObject(*DslObjPtr);
		TArray<FBlueprintDslActionStep> Steps;
		FString ParseErr;
		const bool bParsed = ParseDslStepsFromJson(DslJson, Steps, ParseErr);
		if (bParsed != bExpectParseOk)
		{
			AddError(FString::Printf(TEXT("[%s] Parse 期望=%s 实际=%s，err=%s"),
				*CaseName,
				bExpectParseOk ? TEXT("true") : TEXT("false"),
				bParsed ? TEXT("true") : TEXT("false"),
				*ClipOneLine(ParseErr, 260)));
			continue;
		}
		if (!bParsed)
		{
			// 预期 parse 失败就到此为止
			continue;
		}

		if (Mode == TEXT("parse"))
		{
			continue;
		}

		FString TempBpPath;
		UBlueprint* BP = CreateTempActorBlueprint(TempBpPath);
		if (!BP)
		{
			AddError(FString::Printf(TEXT("[%s] 无法创建临时 Blueprint"), *CaseName));
			continue;
		}

		// Validate
		TArray<FBlueprintDslExecutor::FValidationIssue> Issues;
		FBlueprintDslExecutor::ValidateSteps(Steps, BP, Issues);
		const int32 ErrorCount = CountErrors(Issues);
		if (ErrorCount > ExpectValidateErrorsMax)
		{
			AddError(FString::Printf(TEXT("[%s] Validate errors=%d > %d"), *CaseName, ErrorCount, ExpectValidateErrorsMax));
			CleanupTempAssets({ TempBpPath });
			continue;
		}

		if (Mode == TEXT("validate"))
		{
			CleanupTempAssets({ TempBpPath });
			continue;
		}

		// Execute
		FString Summary;
		TArray<FDslStepFailure> Failures;
		const bool bExecOk = FBlueprintDslExecutor::ExecuteSteps(Steps, BP, Summary, &Failures);
		if (bExecOk != bExpectExecuteOk)
		{
			AddError(FString::Printf(TEXT("[%s] Execute 期望=%s 实际=%s，summary=%s"),
				*CaseName,
				bExpectExecuteOk ? TEXT("true") : TEXT("false"),
				bExecOk ? TEXT("true") : TEXT("false"),
				*ClipOneLine(Summary, 320)));
		}

		// Compile（用于早发现图损坏）
		FKismetEditorUtilities::CompileBlueprint(BP);

		CleanupTempAssets({ TempBpPath });
	}

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS

