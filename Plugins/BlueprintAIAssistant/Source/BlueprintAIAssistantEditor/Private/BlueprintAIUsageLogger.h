#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

/**
 * 记录 Blueprint AI Assistant 试用期的使用行为。
 * - 输出为 JSON Lines 格式到 <Project>/Saved/BlueprintAIAssistant/usage-YYYYMMDD.log
 * - 单例；线程安全（内部加锁）；失败不会抛异常
 *
 * 事件类型（EventType 字段）：
 *  - request_start / request_end：一次模型请求（Ask / GuidedSteps / DSL / ConnectionTest）
 *  - dsl_parsed：DSL 成功解析（包含 step 数量）
 *  - dsl_validate：validate 汇总（Error/Warning 数）
 *  - dsl_exec_batch / dsl_exec_step：批量或单步执行结果
 *  - scene_template：点击了场景快捷按钮
 *  - feedback：未来用户反馈事件（Phase 5 预留）
 */
class FBlueprintAIUsageLogger
{
public:
	static FBlueprintAIUsageLogger& Get();

	/** 开始一次请求（返回 session id，配合 LogRequestEnd 使用） */
	FString LogRequestStart(const FString& Category, const FString& ProviderKind, const FString& Model, const FString& PromptPreview);

	void LogRequestEnd(const FString& SessionId, bool bSuccess, int32 DurationMs, int32 ResponseLen, const FString& ErrorPreview);

	void LogDslParsed(const FString& SessionId, int32 StepCount, bool bOk, const FString& Error);

	void LogValidate(const FString& SessionId, int32 ErrorCount, int32 WarningCount);

	void LogDslExecBatch(const FString& SessionId, int32 TotalSteps, bool bOk, const FString& Summary);

	void LogDslExecStep(const FString& SessionId, int32 StepIndex, const FString& Action, bool bOk, const FString& Error);

	void LogSceneTemplate(const FString& TemplateTitle);

	/** 记录一次解析失败/执行失败触发的自动重试（以及结果）。 */
	void LogRetry(
		const FString& SessionId,
		const FString& Phase, /* parse_retry / exec_retry */
		bool bTriggered,
		bool bSucceeded);

	/** 记录最终失败归因（便于一周复盘聚类）。 */
	void LogFailureCategory(
		const FString& SessionId,
		const FString& Category,
		const FString& DetailPreview);

	/**
	 * HTTP 响应文本提取失败时记录可定位信息（仅用于兼容网关排查）。
	 * - preview 会被截断到 1024 字符左右
	 */
	void LogHttpExtractFailure(
		const FString& SessionId,
		const FString& ProviderKind,
		const FString& Model,
		const FString& EndpointSafeForLog,
		const FString& TopLevelKeys,
		const FString& PreviewFirst1024);

	/**
	 * 记录一次“超时建议”提示（用于统计不同 payload/模型的推荐阈值）。
	 */
	void LogTimeoutSuggestion(
		const FString& SessionId,
		const FString& ProviderKind,
		const FString& Model,
		const FString& EndpointSafeForLog,
		int32 PayloadUtf8Bytes,
		int32 TimeoutSeconds,
		int32 SuggestedTimeoutSeconds,
		const FString& Reason /* network_fail/http_408/http_504/process_request_false 等 */,
		const FString& HttpTransportDetail = FString() /* 引擎级诊断（FailureReason/状态码/收包字节等）*/);

	/** 记录一次 DSL patch 解析/应用结果（Phase 6.B，用于后续 KPI 复盘）。 */
	void LogDslPatchResult(
		const FString& SessionId,
		bool bOk,
		int32 OpCount,
		int32 AppliedCount,
		const FString& Summary,
		const FString& Error);

	/** 返回当前日志文件绝对路径（如果还没写过则先确定今天的文件路径） */
	FString GetCurrentLogFilePath();

	/** 返回日志根目录（<Project>/Saved/BlueprintAIAssistant/） */
	FString GetLogDirectory() const;

private:
	FBlueprintAIUsageLogger() = default;

	void WriteLine(const TMap<FString, FString>& Fields);

	static FString JsonEscape(const FString& In);
	static FString TakePreview(const FString& In, int32 MaxLen);

	FCriticalSection Mutex;
};
