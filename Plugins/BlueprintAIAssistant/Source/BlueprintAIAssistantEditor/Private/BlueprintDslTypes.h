#pragma once

#include "CoreMinimal.h"

enum class EBlueprintDslActionType : uint8
{
	Unknown = 0,
	CreateNode,
	ConnectPins,
	SetPinDefault,
	Comment,
	GetVariable,
	SetVariable,
	CreateMemberVariable,
	CreateFunctionGraph,
};

/** pins_simple：用 Blueprint 常见名描述类型（A 方案）。 */
struct FBlueprintDslSimplePinType
{
	/** A 风格：bool/int/float/name/string/text/vector/rotator/transform/actor/object/... */
	FString Type;

	/** 引用类型（可选）：value/object/class/softObject/softClass/struct/enum */
	FString Ref;

	/** 容器（可选）："" / "Array" / "Map" */
	FString Container;

	/** Map 专用：键/值类型 */
	TSharedPtr<FBlueprintDslSimplePinType> KeyType;
	TSharedPtr<FBlueprintDslSimplePinType> ValueType;

	/** 可选：为 Object/Class/Struct/Enum 提供精确资产或 ScriptPath（如 /Script/Engine.Actor） */
	FString TypePath;
};

struct FBlueprintDslSimplePinDecl
{
	FString Name;
	FBlueprintDslSimplePinType Type;
};

/** 用于描述一次可执行的蓝图图操作（最小集）。 */
struct FBlueprintDslActionStep
{
	EBlueprintDslActionType ActionType = EBlueprintDslActionType::Unknown;

	/** Schema v2 */
	int32 Version = 1;
	FString StepId;
	bool bRequiresConfirmation = false;
	FString TargetGraph; // "EventGraph" / "Function:<Name>"
	/** 可选：若填写则修改该资产路径对应的 Blueprint，而非当前打开的 Blueprint。 */
	FString TargetBlueprintAssetPath; // "/Game/Path/BP_X.BP_X"

	/** 人类可读说明（面板预览显示）。 */
	FString Description;

	/** CreateNode */
	FString NodeId;
	FString NodeType;       // e.g. "Branch" / "Delay" / "CallFunction" / "Cast" / "IsValid" / "MakeVector" / ...
	FString FunctionName;   // e.g. "PrintString" (当 NodeType == CallFunction)

	/** CreateNode (Cast / SpawnActorFromClass 等用得到的"目标类"字段) */
	FString TargetClass;    // 蓝图短名（如 BP_Door_01）或完整 ScriptPath

	/** CreateNode (Switch / Select 等需要预先生成多个分支 pin 的场景) */
	TArray<FString> CaseValues; // e.g. ["0","1","2"] for SwitchOnInt; ["Idle","Run"] for SwitchOnString; Select 可选用作 option 标签

	/** ConnectPins */
	FString FromNodeId;
	FString FromPin;
	FString ToNodeId;
	FString ToPin;

	/** SetPinDefault */
	FString PinName;
	FString DefaultValue;

	/** Comment */
	FString CommentText;
	TArray<FString> AttachToNodeIds;

	/** GetVariable / SetVariable */
	FString VarName;
	FString ValueFromNodeId;
	FString ValueFromPin;

	// -----------------------------------------------------------------------
	// CreateMemberVariable（类变量）
	// -----------------------------------------------------------------------
	FBlueprintDslSimplePinType MemberVarType;
	/** 可选：变量暴露/可编辑性；默认 private。暂仅解析/透传，执行器按默认策略创建。 */
	FString MemberVarExposure;

	// -----------------------------------------------------------------------
	// CreateFunctionGraph（类函数/事件/宏）
	// -----------------------------------------------------------------------
	FString FunctionGraphName;
	/** Callable/Pure/Event/Macro */
	FString FunctionGraphKind;
	TArray<FBlueprintDslSimplePinDecl> FunctionParams;
	TArray<FBlueprintDslSimplePinDecl> FunctionReturns;
	/** 子 steps：在新建的函数/宏图内继续执行（会强制 targetGraph 到该图）。 */
	TArray<FBlueprintDslActionStep> FunctionBodySteps;
};

// ---------------------------------------------------------------------------
// 结构化失败上下文（ExecuteStep / ExecuteSteps 产出，不再靠字符串猜测）
// ---------------------------------------------------------------------------

/** 失败类别枚举 */
enum class EDslFailureCategory : uint8
{
	None = 0,         // 无失败（成功）
	GraphResolve,     // 找不到目标 Graph
	NodeResolve,      // 找不到 fromNodeId / toNodeId / nodeId 对应节点
	PinResolve,       // 找不到 fromPin / toPin / pinName
	ConnectPrecheck,  // 连线预检失败（类型/方向不兼容）
	ConnectFail,      // TryCreateConnection 返回 false
	SetPinFail,       // SetPinDefault 失败
	FunctionNotFound, // CallFunction / functionName 无法解析
	ClassNotFound,    // Cast / Spawn 的 targetClass 无法解析
	SchemaError,      // DSL JSON 解析 / schema 不合法
	Http,             // HTTP 请求失败
	Unknown,          // 其它 / 未分类
};

/** 单步失败上下文（精确到 step/pin 级别） */
struct FDslStepFailure
{
	EDslFailureCategory Category = EDslFailureCategory::None;
	int32   StepIndex = INDEX_NONE; // 0-based，INDEX_NONE 表示非具体步骤
	FString StepId;                 // DSL step 的 stepId 字段
	FString NodeId;                 // 涉及的 nodeId（可能是 from 或 to）
	FString PinName;                // 涉及的 pin 名（可能是 from/to/set 等）
	FString RawError;               // 完整的错误描述文本
	FString FromNodeId;             // ConnectPins 专用
	FString FromPin;                // ConnectPins 专用
	FString ToNodeId;               // ConnectPins 专用
	FString ToPin;                  // ConnectPins 专用

	bool IsEmpty() const { return Category == EDslFailureCategory::None && RawError.IsEmpty(); }

	/** 快捷构造：从错误文本反向识别 category（仅作保底，推荐由执行器直接填充） */
	static EDslFailureCategory CategorizeFromText(const FString& Err)
	{
		if (Err.Contains(TEXT("未找到 fromNodeId=")) || Err.Contains(TEXT("未找到 toNodeId=")) ||
			Err.Contains(TEXT("未找到 nodeId=")))
		{
			return EDslFailureCategory::NodeResolve;
		}
		if (Err.Contains(TEXT("未找到 fromPin=")) || Err.Contains(TEXT("未找到 toPin=")) ||
			Err.Contains(TEXT("未找到 pinName=")) || Err.Contains(TEXT("valueFromPin=")))
		{
			return EDslFailureCategory::PinResolve;
		}
		if (Err.Contains(TEXT("连线预检失败")))
		{
			return EDslFailureCategory::ConnectPrecheck;
		}
		if (Err.Contains(TEXT("无法解析")) || Err.Contains(TEXT("缺少")) || Err.Contains(TEXT("schema")))
		{
			return EDslFailureCategory::SchemaError;
		}
		if (Err.Contains(TEXT("请求失败")) || Err.Contains(TEXT("HTTP")) || Err.Contains(TEXT("网络")))
		{
			return EDslFailureCategory::Http;
		}
		if (Err.Contains(TEXT("未找到函数")) || Err.Contains(TEXT("functionName")))
		{
			return EDslFailureCategory::FunctionNotFound;
		}
		if (Err.Contains(TEXT("targetClass")) || Err.Contains(TEXT("无法解析到 UClass")))
		{
			return EDslFailureCategory::ClassNotFound;
		}
		return EDslFailureCategory::Unknown;
	}

	static FString CategoryToDisplayString(EDslFailureCategory Cat)
	{
		switch (Cat)
		{
		case EDslFailureCategory::GraphResolve:     return TEXT("找不到目标图");
		case EDslFailureCategory::NodeResolve:      return TEXT("找不到节点");
		case EDslFailureCategory::PinResolve:       return TEXT("找不到引脚");
		case EDslFailureCategory::ConnectPrecheck:  return TEXT("连线类型不兼容");
		case EDslFailureCategory::ConnectFail:      return TEXT("连线失败");
		case EDslFailureCategory::SetPinFail:       return TEXT("设置引脚失败");
		case EDslFailureCategory::FunctionNotFound: return TEXT("找不到函数");
		case EDslFailureCategory::ClassNotFound:    return TEXT("找不到目标类");
		case EDslFailureCategory::SchemaError:      return TEXT("DSL 格式错误");
		case EDslFailureCategory::Http:             return TEXT("网络请求失败");
		case EDslFailureCategory::Unknown:          return TEXT("执行失败");
		default:                                    return TEXT("未知");
		}
	}

	/** 根据失败类别生成可供地编阅读的手动推进建议文本 */
	static FString BuildManualGuidance(const FDslStepFailure& F)
	{
		const FString StepDesc = (F.StepIndex != INDEX_NONE)
			? FString::Printf(TEXT("（第 %d 步）"), F.StepIndex + 1)
			: FString();

		switch (F.Category)
		{
		case EDslFailureCategory::NodeResolve:
			return FString::Printf(
				TEXT("手动推进建议%s：\n")
				TEXT("1) 在蓝图 EventGraph 空白处右键，搜索节点名「%s」手动创建；\n")
				TEXT("2) 事件节点（BeginPlay/Overlap/InputKey）应已存在于图顶部，检查是否被禁用；\n")
				TEXT("3) 创建后再点该步骤的「执行」按钮重试。"),
				*StepDesc, *F.NodeId);

		case EDslFailureCategory::PinResolve:
			return FString::Printf(
				TEXT("手动推进建议%s：\n")
				TEXT("1) 选中节点，在 Details 或 Pin 上悬停查看真实引脚名（与 DSL 中的 「%s」比对）；\n")
				TEXT("2) FHitResult 结构体需先「Break Hit Result」再用 HitActor；\n")
				TEXT("3) Cast 的 Object 是输入引脚，执行线应接 Cast Succeeded / Cast Failed；\n")
				TEXT("4) 修正后可点「请求增量修复(6.B)」让 AI 只改这一步。"),
				*StepDesc, *F.PinName);

		case EDslFailureCategory::ConnectPrecheck:
			return FString::Printf(
				TEXT("手动推进建议%s：\n")
				TEXT("1) 两端引脚类型不兼容：%s→%s；\n")
				TEXT("2) 需要类型转换时，在中间插入 Cast / Break / Conv 节点；\n")
				TEXT("3) 白色执行线（Exec）只能接执行引脚，不能接数据引脚；\n")
				TEXT("4) 可点「复制修复提示词」发给 AI 让它修正这段连线。"),
				*StepDesc,
				F.FromPin.IsEmpty() ? TEXT("来源引脚") : *F.FromPin,
				F.ToPin.IsEmpty()   ? TEXT("目标引脚") : *F.ToPin);

		case EDslFailureCategory::FunctionNotFound:
			return FString::Printf(
				TEXT("手动推进建议%s：\n")
				TEXT("1) 函数「%s」在当前蓝图/上下文中找不到；\n")
				TEXT("2) 检查蓝图是否继承自正确的父类（如 ACharacter / AActor）；\n")
				TEXT("3) 可用「KismetMathLibrary::函数名」或「Class::函数名」精确指定；\n")
				TEXT("4) 若是自定义函数，确认它已在蓝图中定义，然后点「请求增量修复(6.B)」。"),
				*StepDesc, *F.NodeId);

		case EDslFailureCategory::ClassNotFound:
			return FString::Printf(
				TEXT("手动推进建议%s：\n")
				TEXT("1) 目标类「%s」无法找到，请检查拼写或使用完整路径 /Game/.../BP_Xxx.BP_Xxx_C；\n")
				TEXT("2) 若是新建蓝图，需先保存（Ctrl+S）使 Asset Registry 能索引到它；\n")
				TEXT("3) 修正后点「按建议重生 DSL」重新生成即可。"),
				*StepDesc, *F.NodeId);

		case EDslFailureCategory::Http:
			return TEXT(
				"手动推进建议：\n"
				"1) 先点「生成步骤清单」获取人工操作步骤；\n"
				"2) 检查 Project Settings → Blueprint AI Assistant 中的 Endpoint / Key / Timeout；\n"
				"3) 点「导出 HTTP（脱敏）」生成 dump 发给开发同学排查网关。");

		case EDslFailureCategory::SchemaError:
			return TEXT(
				"手动推进建议：\n"
				"1) AI 输出格式有误；点「按建议重生 DSL」让 AI 重新生成；\n"
				"2) 或点「生成步骤清单」获取纯文字操作指引；\n"
				"3) 如问题重复出现，点「反馈本次问题」发给开发同学。");

		default:
			return FString::Printf(
				TEXT("手动推进建议%s：\n")
				TEXT("1) 点「生成步骤清单」获取人工操作步骤；\n")
				TEXT("2) 也可点「请求增量修复(6.B)」让 AI 只修复这一步；\n")
				TEXT("3) 实在无法解决，点「反馈本次问题」发给开发同学。"),
				*StepDesc);
		}
	}
};

/** 解析模型输出的 DSL JSON（严格 JSON；schema: {"steps":[{...}, ...]}）。 */
bool ParseDslStepsFromJson(const FString& InContent, TArray<FBlueprintDslActionStep>& OutSteps, FString& OutError);

/** 将当前 DSL steps 序列化为严格 JSON（便于失败后自动重问/反馈）。 */
FString SerializeDslStepsToJson(const TArray<FBlueprintDslActionStep>& Steps, int32 Version = 2);

/** 将 action 字符串映射为枚举。 */
EBlueprintDslActionType BlueprintDslActionTypeFromString(const FString& InAction);

