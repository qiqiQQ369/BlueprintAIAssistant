#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "BlueprintDslTypes.h"

class SMultiLineEditableTextBox;
class FBlueprintAIHttpProvider;
class SVerticalBox;
class SWrapBox;

class SBlueprintAIAssistantPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBlueprintAIAssistantPanel) {}
	SLATE_END_ARGS()

	struct FPinAliasCandidate
	{
		FString From;
		FString To;
		int32 Count = 0;
		bool bSelected = true;
	};

	/** Phase 6：模糊需求澄清问题 */
	struct FClarifyQuestion
	{
		FString Id;
		FString Question;
		TArray<FString> Options;
		FString Answer;
	};

	/** Phase 6：跨蓝图计划（先出计划，再单步落到 DSL） */
	struct FPlanItem
	{
		FString StepId;
		FString Title;
		FString TargetHint;
		FString DslPrompt;
	};

	void Construct(const FArguments& InArgs);

private:
	FReply OnAskClicked();
	FReply OnGenerateStepsClicked();
	FReply OnGenerateDslClicked();
	FReply OnGenerateDslFromClarifyClicked();
	FReply OnExecuteSelectedDslClicked();
	FReply OnExecuteOneDslStepClicked(int32 StepIndex);
	FReply OnTestConnectionClicked();
	FReply OnCopyClicked();
	FReply OnInsertCommentClicked();
	FReply OnSceneTemplateClicked(int32 TemplateIndex);
	FReply OnOpenLogFolderClicked();
	FReply OnGenerateKpiReportClicked();
	FReply OnSubmitFeedbackClicked();
	FReply OnExportLastHttpDumpClicked();
	FReply OnGeneratePinAliasSuggestionsClicked();
	FReply OnImportPinAliasSuggestionsClicked();
	FReply OnWritePinAliasesJsonClicked();
	FReply OnReloadPinAliasesClicked();
	FReply OnUndoLastDslClicked();
	FReply OnClearMultiTurnHistoryClicked();
	FReply OnRegenerateDslFromFailureClicked();
	FReply OnCopyFailureFixPromptClicked();
	FReply OnRequestDslPatchFixClicked();
	FReply OnAutoAddPrereqStepsClicked();
	FText GetResponseText() const;
	FText GetMultiTurnStatusText() const;
	void RebuildDslPreview();
	void UpdateFailureGuidanceFromResponseText();
	void ClearFailureGuidanceState();
	FString BuildFailureFixPromptText() const;

	TSharedRef<SWidget> BuildSceneTemplateBar();

private:
	TSharedPtr<SMultiLineEditableTextBox> InputTextBox;
	FString ResponseText;
	TSharedPtr<FBlueprintAIHttpProvider> Provider;

	TArray<FBlueprintDslActionStep> CurrentDslSteps;
	TArray<bool> CurrentDslStepSelected;
	TSharedPtr<SVerticalBox> DslStepsBox;

	/** 根据当前蓝图与 DSL 预检得到的“可自动补齐”的前置 steps（缺变量/缺自定义函数）。 */
	TArray<FBlueprintDslActionStep> PendingAutoPrereqSteps;
	FString PendingAutoPrereqSummary;

	/** 最近一次 DSL 生成对应的日志 session id（用于埋点关联） */
	FString CurrentDslSessionId;

	/** 最近一次发给模型的用户问题原文（用于反馈按钮归档） */
	FString LastUserPrompt;

	/** 最近一次请求的 provider/model 标签（用于反馈按钮归档） */
	FString LastProviderKind;
	FString LastProviderModel;

	/** 最近一次请求类别：ask / guided_steps / dsl / ping */
	FString LastCategory;

	/** 自 DSL 生成以来累积执行过的事务次数（批量+1，单步+1；用于"撤销本次"按钮） */
	int32 TransactionsSinceDslGenerated = 0;

	TArray<FPinAliasCandidate> PinAliasCandidates;
	TSharedPtr<SVerticalBox> PinAliasCandidatesBox;

	TArray<FClarifyQuestion> PendingClarifyQuestions;
	FString PendingClarifyOriginalQuestion;
	TSharedPtr<SVerticalBox> ClarifyBox;

	TArray<FPlanItem> PendingPlanItems;
	TSharedPtr<SVerticalBox> PlanItemsBox;

	/** Phase 6.A 多轮：已完成的 user↔助手轮次（不含当前正在发送的输入） */
	struct FChatExchange
	{
		FString User;
		FString Assistant;
	};
	TArray<FChatExchange> MultiTurnHistory;

	/** 供下一轮生成参考：最近一条执行/解析类失败短摘要 */
	FString LastFailureHintForNextTurn;

	/** Phase 6：失败引导卡片（用于地编在 DSL 失败时继续推进） */
	FString LastFailureCategoryForGuidance;
	FString LastFailureDetailForGuidance;
	/** 最近一次 ExecuteSteps 产出的结构化失败列表（精确到步骤/引脚级别）。 */
	TArray<FDslStepFailure> LastStepFailures;
	TSharedPtr<SVerticalBox> FailureGuidanceBox;

	/** 将结构化失败列表转换为引导卡片所需的 Category + Detail 字符串。 */
	void ApplyStructuredFailures(const TArray<FDslStepFailure>& Failures);
	FString BuildStructuredFailureSummaryForPatchPrompt() const;

	FString AugmentUserPromptWithMultiTurn(
		const FString& BaseUserPrompt,
		const TArray<FBlueprintDslActionStep>& DslForContextSummary) const;
	void RegisterMultiTurnExchange(const FString& UserQ, const FString& AssistantText);
	void NoteFailureHintForNextTurn(const FString& ShortHint);
	void ClearMultiTurnHistory();
	FString BuildDslSummaryForMultiturn(const TArray<FBlueprintDslActionStep>& Steps) const;
	void InvalidateMultiturnWidgets();

	/** Phase 6：通用入口：以指定问题发起 DSL 生成（跳过分诊） */
	void StartGenerateDslWithQuestion(const FString& EffectiveQuestion);

	/** Phase 6：生成澄清问题并展示 */
	void RequestClarifyForQuestion(const FString& UserQuestion);

	/** Phase 6：生成跨蓝图实施计划并展示 */
	void RequestPlanForQuestion(const FString& UserQuestion);

	/** 最近一次失败的关键定位（供 6.B patch prompt 精确修复）。 */
	int32 LastFailureStepIndex = INDEX_NONE;
	FString LastFailureNodeId;
	FString LastFailurePinName;
	FString LastFailureOriginalDslStep;

	/** 有 LLM 请求进行中：在通过校验后、发起 HTTP 前即置 true，用于立刻禁用相关按钮，减少连点重复提交。 */
	bool bLlmRequestInFlight = false;

	void SetLlmRequestInFlight(bool bInFlight);

	/** 用于 AsyncTask 回调内：除「链式重试已接手」外，在析构时结束 in-flight 状态。 */
	struct FOptionalLlmRequestClear
	{
		SBlueprintAIAssistantPanel* Self = nullptr;
		bool bAbandon = false;
		~FOptionalLlmRequestClear()
		{
			if (Self && !bAbandon)
			{
				Self->SetLlmRequestInFlight(false);
			}
		}
		void Abandon() { bAbandon = true; }
	};
};
