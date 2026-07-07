#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"

#include "BlueprintAIAssistantSettings.generated.h"

UENUM()
enum class EBlueprintAIProviderKind : uint8
{
	OpenAICompatible	UMETA(DisplayName = "OpenAI 兼容"),
	DeepSeek			UMETA(DisplayName = "DeepSeek"),
	Doubao				UMETA(DisplayName = "豆包 / 火山引擎"),
	Gemini				UMETA(DisplayName = "Google Gemini")
};

UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig)
class BLUEPRINTAIASSISTANTRUNTIME_API UBlueprintAIAssistantSettings : public UObject
{
	GENERATED_BODY()

public:
	/** 当前使用哪一家模型提供商 */
	UPROPERTY(Config, EditAnywhere, Category = "Provider")
	EBlueprintAIProviderKind ProviderKind = EBlueprintAIProviderKind::DeepSeek;

	/** 通用超时配置（秒）：复杂 DSL / 多轮修复建议可适当调大（例如 90~180）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Provider", meta = (ClampMin = "5", ClampMax = "600"))
	int32 TimeoutSeconds = 90;

	/**
	 * 对使用 OpenAI 兼容体（/v1/chat/completions）的 Provider 启用 SSE 流式响应（请求体中 stream=true）。
	 * 持续有 token/数据到达，可显著降低「中间层长时间无字节」的 idle 掐断；若网关/代理不支持，请关闭以退回整段 JSON。
	 * 注：不作用于 Gemini、也不作用于已切到 /responses 的调用。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Provider", meta = (DisplayName = "流式（SSE）拉取 /chat/completions 响应"))
	bool bStreamOpenAIChatCompletions = true;

	/** OpenAI 兼容：例如官方 OpenAI / 第三方兼容网关 */
	UPROPERTY(Config, EditAnywhere, Category = "OpenAI-Compatible")
	FString OpenAIEndpointUrl = TEXT("https://api.openai.com/v1/chat/completions");

	UPROPERTY(Config, EditAnywhere, Category = "OpenAI-Compatible")
	FString OpenAIModel = TEXT("gpt-4o-mini");

	UPROPERTY(Config, EditAnywhere, Category = "OpenAI-Compatible", meta = (DisplayName = "API Key"))
	FString OpenAIApiKey;

	/** DeepSeek 预设 */
	UPROPERTY(Config, EditAnywhere, Category = "DeepSeek")
	FString DeepSeekEndpointUrl = TEXT("https://api.deepseek.com/v1/chat/completions");

	UPROPERTY(Config, EditAnywhere, Category = "DeepSeek")
	FString DeepSeekModel = TEXT("deepseek-chat");

	UPROPERTY(Config, EditAnywhere, Category = "DeepSeek", meta = (DisplayName = "API Key"))
	FString DeepSeekApiKey;

	/** 豆包 / 火山 Ark：与常见 Node/OpenAI 兼容调用一致，使用 chat/completions。若要用 Responses API，请把 URL 改为 .../api/v3/responses */
	UPROPERTY(Config, EditAnywhere, Category = "Doubao")
	FString DoubaoEndpointUrl = TEXT("https://ark.cn-beijing.volces.com/api/v3/chat/completions");

	UPROPERTY(Config, EditAnywhere, Category = "Doubao")
	FString DoubaoModel = TEXT("doubao-seed-2-0-pro-260215");

	UPROPERTY(Config, EditAnywhere, Category = "Doubao", meta = (DisplayName = "API Key"))
	FString DoubaoApiKey;

	/**
	 * Google Gemini（Google AI Studio / generativelanguage API）
	 * 最终请求：POST {GeminiApiBaseUrl}/models/{GeminiModel}:generateContent?key=...
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Gemini", meta = (DisplayName = "API Base URL"))
	FString GeminiApiBaseUrl = TEXT("https://generativelanguage.googleapis.com/v1beta");

	UPROPERTY(Config, EditAnywhere, Category = "Gemini")
	FString GeminiModel = TEXT("gemini-2.0-flash");

	UPROPERTY(Config, EditAnywhere, Category = "Gemini", meta = (DisplayName = "API Key"))
	FString GeminiApiKey;

	/**
	 * Spawn 白名单 - L1：显式类名（蓝图短名，不带 _C；或完整 ScriptPath）。
	 * 默认包含 BP_Cursor / BP_Door_01 / BP_FireBall；可在项目设置里增删。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DSL | SpawnActor 白名单")
	TArray<FString> SpawnClassWhitelist = { TEXT("BP_Cursor"), TEXT("BP_Door_01"), TEXT("BP_FireBall") };

	/**
	 * Spawn 白名单 - L2：路径前缀。蓝图资产路径以该前缀开头即放行，例如
	 *   "/Game/Spawnables/"、"/Game/Gameplay/AI_Allowed/"。
	 * 默认空，建议项目里约定一个"AI 允许 Spawn"的目录，把可 Spawn 蓝图全丢进去。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DSL | SpawnActor 白名单")
	TArray<FString> SpawnPathPrefixes;

	/**
	 * Spawn 白名单 - L3：基类白名单。候选蓝图继承自该类（或实现该接口）时放行。
	 * 默认空。用于"只允许继承自 ASpawnableActor 的蓝图"之类的约束。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DSL | SpawnActor 白名单", meta = (AllowAbstract = "true"))
	TArray<FSoftClassPath> SpawnBaseClasses;

	/**
	 * Spawn 白名单 - L4：开发模式一键全放行（默认关闭）。
	 * 打开后 L1/L2/L3 被忽略，任何 BP/UClass 都能 Spawn —— 只给内部高级用户使用。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DSL | SpawnActor 白名单")
	bool bAllowAllSpawnClassesForDev = false;

	/**
	 * 自动排版 - 总开关：DSL 执行完成后是否对本次涉及节点做一次轻量排版。
	 * 关闭后 §4.3 整个排版流水线（包含 Straighten）都会跳过。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DSL | 自动排版")
	bool bAutoLayoutAfterExecute = true;

	/**
	 * 自动排版 - 碰撞避让（v2）：排版结果与图内已有非本次节点的 AABB 重叠时，
	 * 把被占位的既有节点整体下移一行（按 Y 从小到大级联，上限 5 次）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DSL | 自动排版", meta = (EditCondition = "bAutoLayoutAfterExecute"))
	bool bLayoutAvoidCollisions = true;

	/**
	 * 自动排版 - 长连线插 Knot（v3）：当执行线跨列较远且疑似穿越节点时，自动插入 `UK2Node_Knot` 作为中继，
	 * 以减少“连线压在节点上”的情况。仅影响连线视觉（不改变业务节点拓扑）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DSL | 自动排版", meta = (EditCondition = "bAutoLayoutAfterExecute"))
	bool bLayoutInsertKnotsForLongWires = true;

	/**
	 * 自动排版 - 数据节点卫星（v3）：对无 Exec pin 的纯数据节点，识别其主要消费者节点并贴到消费者左侧附近，
	 * 以减少数据线横穿 Exec 主干。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DSL | 自动排版", meta = (EditCondition = "bAutoLayoutAfterExecute"))
	bool bLayoutAttachDataNodesAsSatellites = true;

	/**
	 * 自动排版 - Comment 跟随（v3）：对本次 DSL 生成的 comment 框，在排版后重算包围盒并自动包住本次簇节点。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DSL | 自动排版", meta = (EditCondition = "bAutoLayoutAfterExecute"))
	bool bLayoutAutoResizeCommentBoxes = true;

	/**
	 * DSL - 解析失败自动重试一次（Phase 5）：当模型输出不满足 DSL JSON Schema（无法解析/缺字段/steps 非法）时，
	 * 会把错误摘要回灌给模型并要求它只输出修正后的 JSON，最多重试 1 次。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DSL | 可靠性")
	bool bAutoRetryOnceOnDslParseFailure = true;

	/**
	 * DSL - 执行失败自动重试一次（Phase 5）：当批量执行失败时，会先 Undo 回滚本次执行事务，
	 * 再把失败摘要 + 原 steps 回灌给模型要求修正 DSL，并最多重试执行 1 次。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DSL | 可靠性")
	bool bAutoRetryOnceOnDslExecFailure = true;

	/**
	 * DSL - Schema 清洗/纠错（Phase 5）：允许解析器对“轻微不规范但意图明确”的 DSL 做归一化处理，
	 * 例如：`Actions` 代替 `steps`、snake_case 的 action、requires_confirmation 字段、action 缺失时按字段推断类型等。
	 * 关闭后解析更严格（更接近 Prompt 约束），但会更容易触发“解析失败→自动重试”。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DSL | 可靠性")
	bool bEnableDslSchemaAutoSanitize = true;

	/**
	 * DSL - 失败聚合执行（Phase 5）：默认「遇错即停」；开启后会尝试执行完所有可执行的 steps，最后汇总失败列表。
	 * 注意：后续 steps 可能依赖前面失败的结果，仍可能继续失败（这属于预期行为）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DSL | 可靠性")
	bool bAggregateDslExecutionFailures = false;

	/**
	 * 多轮对话（Phase 6.A）：在「生成建议 / 步骤清单 / DSL」时，将最近几轮 user/助手输出与（可选）DSL/失败摘要注入 UserPrompt。
	 * 默认 N=6 个完整轮次、附加块约 4k 字符，可在项目设置中调整；仍受各 Provider 的总长度限制（随后统一截断）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "对话 | Phase 6.A (多轮)", meta = (DisplayName = "启用途内多轮上下文（注入到 UserPrompt）"))
	bool bEnableMultiTurnContext = true;

	UPROPERTY(Config, EditAnywhere, Category = "对话 | Phase 6.A (多轮)", meta = (ClampMin = "1", ClampMax = "20", EditCondition = "bEnableMultiTurnContext"))
	int32 MultiTurnMaxRecentExchanges = 6;

	UPROPERTY(Config, EditAnywhere, Category = "对话 | Phase 6.A (多轮)", meta = (ClampMin = "1024", ClampMax = "48000", EditCondition = "bEnableMultiTurnContext"))
	int32 MultiTurnMaxContextChars = 4000;

	UPROPERTY(Config, EditAnywhere, Category = "对话 | Phase 6.A (多轮)", meta = (ClampMin = "200", ClampMax = "8000", EditCondition = "bEnableMultiTurnContext"))
	int32 MultiTurnMaxMessageChars = 1500;

	/** 便于 Provider 统一读取当前使用的 Endpoint/Model/Key（Gemini 的 Key 在 URL 查询参数中，OutApiKey 为空） */
	void ResolveCurrentProvider(FString& OutEndpointUrl, FString& OutModel, FString& OutApiKey) const;

	/** 面向 UI 的摘要信息（Gemini 不展示真实 key） */
	void GetProviderDisplayInfo(FString& OutKindLabel, FString& OutModelDisplay, FString& OutEndpointDisplay) const;

private:
	FString BuildGeminiRequestUrl() const;
};
