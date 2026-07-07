#pragma once

#include "CoreMinimal.h"
#include "BlueprintDslTypes.h"

class UBlueprint;
struct FBlueprintDslActionStep;

/**
 * 最小可行 DSL 执行器：CreateNode / ConnectPins / SetPinDefault / Comment。
 * 设计目标：可撤销（事务）、可失败提示、可按步骤执行。
 */
class FBlueprintDslExecutor
{
public:
	struct FValidationIssue
	{
		enum class ESeverity : uint8 { Info, Warning, Error };
		ESeverity Severity = ESeverity::Info;
		int32 StepIndex = INDEX_NONE;
		FString Message;
	};

	static void ValidateSteps(const TArray<FBlueprintDslActionStep>& Steps, UBlueprint* Blueprint, TArray<FValidationIssue>& OutIssues);

	/**
	 * 执行单步（内部会定位当前蓝图 EventGraph/当前图）。
	 * OutFailure 包含精确的失败分类与字段信息；失败时 OutError 仍输出可读文本。
	 */
	static bool ExecuteStep(const FBlueprintDslActionStep& Step, UBlueprint* Blueprint, FString& OutError,
		FDslStepFailure* OutFailure = nullptr);

	/**
	 * 批量执行（建议外部包裹事务）。
	 * OutFailures 包含每个失败步骤的结构化上下文（不含成功步骤）。
	 */
	static bool ExecuteSteps(const TArray<FBlueprintDslActionStep>& Steps, UBlueprint* Blueprint,
		FString& OutSummary, TArray<FDslStepFailure>* OutFailures = nullptr);

	/** 对本次步骤涉及的节点做轻量自动排版（仅移动节点，不改连线）。 */
	static void AutoLayoutSteps(const TArray<FBlueprintDslActionStep>& Steps, UBlueprint* Blueprint);

	/** 热更新：重新加载 Saved/BlueprintAIAssistant/pin-aliases.json（若存在）。 */
	static void ReloadPinAliasTable();
};

