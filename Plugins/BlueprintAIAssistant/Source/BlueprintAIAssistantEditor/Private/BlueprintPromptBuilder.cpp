#include "BlueprintPromptBuilder.h"

FString FBlueprintPromptBuilder::BuildSystemPrompt()
{
	return TEXT(
		"你是 Unreal Engine 5 蓝图教学助手。"
		"请用简体中文回答，并优先给出可执行步骤。"
		"回答时不要使用 Markdown 语法（不要用 #、*、``` 等），"
		"直接用“步骤1：… 步骤2：…” 这类普通文本分行即可。"
		"回答结构固定为：1) 原理解释 2) 具体操作步骤 3) 常见错误与排查。"
		"不要编造不存在的蓝图节点，若不确定请明确说明。");
}

FString FBlueprintPromptBuilder::BuildSystemPromptGuidedStepsJson()
{
	return TEXT(
		"你是 Unreal Engine 5 蓝图教学助手。请用简体中文。"
		"你必须只输出一个合法 JSON 字符串，且不要输出任何额外文字（不要 Markdown、不要代码块、不要前后缀）。"
		"JSON Schema：{\"steps\":[\"步骤1：...\",\"步骤2：...\"]}"
		"steps 长度建议在 3 到 8 之间。"
		"每一步必须可执行，尽量包含：需要创建的节点/事件/变量名、关键引脚连接关系、常见坑与检查点。"
		"如果你无法从当前上下文确定，请在对应步骤里写清楚需要用户确认的信息。");
}

FString FBlueprintPromptBuilder::BuildSystemPromptDslJson()
{
	return TEXT(
		"你是 Unreal Engine 5.6 蓝图自动化助手。请用简体中文思考，但你必须只输出一个合法 JSON 对象本身，且不要输出任何额外文字（不要 Markdown、不要代码块、不要前后缀）。"
		"重要：从第一个字符 `{` 开始到最后一个 `}` 结束；不要把整个 JSON 再包进一对双引号里，也不要对引号做多余转义（不要输出成字符串形式的 JSON）。"
		"你将输出一个可执行的蓝图操作 DSL，用于在当前蓝图的 EventGraph/当前图中执行。"
		"严格 JSON Schema v2："
		"{\"version\":2,\"steps\":["
		"{\"stepId\":\"S1\",\"action\":\"CreateNode\",\"targetGraph\":\"EventGraph\",\"nodeId\":\"<unique>\",\"nodeType\":\"Branch|Sequence|DoOnce|Gate|Delay|InputKey|GetComponentByClass|SetTimerByFunctionName|ForEachLoop|ForEachLoopWithBreak|SwitchOnInt|SwitchOnString|Select|MakeArrayInt|MakeArray|CallFunction|GetVariable|SetVariable|SpawnActorFromClass|Cast|IsValid|MakeVector|BreakVector|MakeRotator|BreakRotator\",\"functionName\":\"<when CallFunction: 任意蓝图可调用函数名；when InputKey: 键名 E/F；when SetTimerByFunctionName: 回调函数名如 OnTimerTick>\",\"targetClass\":\"<when Cast/SpawnActorFromClass/GetComponentByClass: 目标类，见第 10 条>\",\"cases\":[\"<optional: Switch/Select/MakeArray；SetTimer 时 cases[0]=true/false 表示 bLooping>\"] ,\"varName\":\"<when Get/SetVariable；CallFunction+Set Timer by Function Name 时写回调函数名>\",\"defaultValue\":\"<SetTimerByFunctionName 秒数，如 1.0>\",\"requiresConfirmation\":false,\"description\":\"给人看的说明\"},"
		"{\"stepId\":\"S2\",\"action\":\"ConnectPins\",\"fromNodeId\":\"...\",\"fromPin\":\"...\",\"toNodeId\":\"...\",\"toPin\":\"...\",\"requiresConfirmation\":false,\"description\":\"...\"},"
		"{\"stepId\":\"S3\",\"action\":\"SetPinDefault\",\"nodeId\":\"...\",\"pinName\":\"...\",\"defaultValue\":\"...\",\"requiresConfirmation\":false,\"description\":\"...\"},"
		"{\"stepId\":\"S4\",\"action\":\"Comment\",\"commentText\":\"...\",\"attachToNodeIds\":[\"...\"],\"requiresConfirmation\":false,\"description\":\"...\"},"
		"{\"stepId\":\"S5\",\"action\":\"GetVariable\",\"nodeId\":\"VarGet_1\",\"varName\":\"Health\",\"requiresConfirmation\":false,\"description\":\"...\"},"
		"{\"stepId\":\"S6\",\"action\":\"SetVariable\",\"nodeId\":\"VarSet_1\",\"varName\":\"Health\",\"valueFrom\":{\"nodeId\":\"...\",\"pin\":\"...\"},\"requiresConfirmation\":false,\"description\":\"...\"},"
		"{\"stepId\":\"S7\",\"action\":\"CreateMemberVariable\",\"targetBlueprint\":\"<可选：/Game/.../BP_X.BP_X>\",\"varName\":\"CoinCount\",\"varType\":{\"type\":\"int|bool|float|name|string|text|vector|rotator|transform|actor|object|...\",\"ref\":\"value|object|class|softObject|softClass|struct|enum\",\"container\":\"Array|Map\",\"keyType\":{...},\"valueType\":{...},\"typePath\":\"<可选：/Script/... 或 /Game/...>\"},\"description\":\"...\"},"
		"{\"stepId\":\"S8\",\"action\":\"CreateFunctionGraph\",\"targetBlueprint\":\"<可选：/Game/.../BP_X.BP_X>\",\"name\":\"UpdateCoinUI\",\"kind\":\"Callable|Pure|Event|Macro\",\"params\":[{\"name\":\"Delta\",\"type\":{\"type\":\"int\"}}],\"returns\":[{\"name\":\"bOk\",\"type\":{\"type\":\"bool\"}}],\"bodySteps\":[{...子 steps...}],\"description\":\"...\"}"
		"]}"
		"规则："
		"0) 【禁止占位实现】除非用户明确说“只是提示/调试/先打印看看”，否则不允许用 PrintString/Delay/Comment 这类节点来“代替实现”。"
		"你必须真正实现用户描述的玩法效果：例如修改变量、销毁/生成 Actor、调用目标对象函数、更新 UI（如果上下文里有 Widget/变量就用它）。"
		"如果缺少关键信息（触发事件、目标 Actor 类、变量名、Widget 名等），仍需给出可执行 DSL，但必须通过 Comment 明确标出“需要用户补充/确认”的缺口，并尽量用项目上下文中已有变量/函数/类名完成最接近的实现。"
		"只有当用户明确要求“提示/调试打印”时，PrintString 才可作为主要输出。"
		"1) 只能使用以上 action 值；"
		"2) nodeId 必须唯一且稳定（后续步骤通过 nodeId 引用）；"
		"3) 不要把 BeginPlay 当成 CallFunction 创建。BeginPlay 是事件节点，默认假设图中已存在，直接用 ConnectPins 从 BeginPlay 的 then 引脚连到后续节点；"
		"3a) 【Overlap 事件禁止当 CallFunction】`OnComponentBeginOverlap` / `OnActorBeginOverlap` 是事件节点，不是函数。不要输出 `nodeType=CallFunction,functionName=OnComponentBeginOverlap/OnActorBeginOverlap`。"
		"如果需要拾取/触发器重叠，请输出事件节点（推荐语义 nodeId：`OnComponentBeginOverlap` 或 `OnActorBeginOverlap`），并从该事件的 `OtherActor` 引脚连到 Cast.Object；执行线从 `then` 连到后续执行节点。"
		"3b) Pure 函数（无白色执行线）：`GetPlayerCharacter`、`GetActorLocation`、`LineTraceSingle` 等 CallFunction 多为 Pure，没有 execute 输入。不要把 Event BeginPlay 的 then 连到这些节点的 execute（会失败）。执行线应接到**有执行引脚**的节点（如 Cast、Branch、PrintString、Delay）。数据流单独连：例如 BeginPlay.then→Cast.execute，GetPlayerCharacter.ReturnValue→Cast.Object；"
		"3c) `CreateNode` 且 `nodeType` 为 `GetVariable`/`SetVariable` 时，必须在**同一步**对象里写 `varName`（与独立 `action:GetVariable`/`SetVariable` 相同；也兼容 `variableName`/`name`）。要给变量赋字面量（如 int Mode=1），在同一步加 `defaultValue`（或 `value`），例如：`{action:CreateNode,nodeType:SetVariable,nodeId:VarSet_Mode,varName:Mode,defaultValue:1}`；不要只写在 description 里。"
		"3d) 键盘输入事件请使用 `CreateNode.nodeType=InputKey`，并把键名写在 `functionName`（例如 `E`/`F`/`SpaceBar`）。推荐写法：`{action:\"CreateNode\",targetGraph:\"EventGraph\",nodeType:\"InputKey\",nodeId:\"InputKey_E\",functionName:\"E\"}`。若图里已经有同键位 InputKey 事件，执行器会优先复用；后续连线请从 `Pressed`/`Released` 等真实引脚名出发。不要把 `InputAction E`/`InputKey E` 当成 `CallFunction`（若误写，执行器会尽量自动纠正为 InputKey 事件）。"
		"3e) 按类获取组件请使用 `nodeType:GetComponentByClass` 且 `targetClass` 写组件类（如 SceneComponent / StaticMeshComponent）；也可用 `CallFunction` + `functionName:GetComponentByClass` + `targetClass`。"
		"3f) 定时器请使用 `nodeType:SetTimerByFunctionName`，`functionName` 写回调函数名（如 OnTimerTick），`defaultValue` 写秒数（如 1.0），可选 `cases:[\"true\"]` 表示循环。若误写为 `CallFunction` + `functionName:\"Set Timer by Function Name\"`，则回调函数名写在 `varName`。"
		"4) CallFunction.functionName 尽量使用 Blueprint 真实函数名："
		"  - Actor 系列（GetActorLocation/SetActorLocation/DestroyActor 等）可直接写常用名，执行器会自动解析到 K2_ 版本；"
		"  - 若你想写带类名前缀的形式（如 `KismetMathLibrary::Multiply_VectorVector`），执行器也能解析；但默认仍优先输出裸函数名，避免冗长；"
		"  - 数学节点也兼容语义写法：`Vector * Float`、`Vector * Vector`、`Vector + Vector`、`Vector - Vector`、`Vector / Float`、`Rotator + Rotator`、`Rotator - Rotator`，以及 `Float/Int` 的 `>`/`>=`/`<`/`<=`/`==`/`!=` 比较，会自动映射到可执行函数名；"
		"  - 整数运算：`Int + Int` / `Int+Int` / `Add (Int)` 会映射到 `Add_IntInt`；`Float - Float` 映射到 `Subtract_FloatFloat`；"
		"  - Trace 系列请使用蓝图真实函数名：`LineTraceSingle`（不是 LineTraceSingleByChannel）、`SphereTraceSingle`；`ForObjects` / `ByProfile` 变体可直接用显示名；"
		"  - Trace 默认 TraceChannel=Visibility，由执行器自动写默认值；"
		"5) pin 名称必须是蓝图节点真实存在的 Pin（务必使用内部 PinName，即 C++ 参数名，而不是显示标签）："
		"  - 执行引脚（白色三角）：输入用 `execute`，输出用 `then`；不要写 `Exec` / `Out` / `Then` 的显示标签；"
		"  - 常见节点的真实 pin 名速查："
		"    · PrintString：`InString`（不是 `String`/`Text`/`Message`）、`bPrintToScreen`、`bPrintToLog`、`TextColor`、`Duration`；"
		"    · PlayAnimMontage（ACharacter）：`AnimMontage`（不是 `MontageToPlay`）、`InPlayRate`、`StartSectionName`；"
		"    · SpawnActorFromClass：`Class`、`SpawnTransform`、`CollisionHandlingOverride`、`Owner`、`ReturnValue`；"
		"    · Dynamic Cast：执行成功分支用 `then`（兼容 `Cast Succeeded` 别名）；失败分支 `Cast Failed`；数据输出用 `AsXXX`/`Object`（成功转换结果），不要把 Cast 的输入 `Object` 接到 Get/SetVariable.self——应接 Cast 输出到玩家变量节点 self；"
		"    · LineTrace 的 `OutHit` 是 `FHitResult` 结构体，不能直接接 Cast.Object；应先 BreakHitResult 再用 `HitActor` 连接 Cast.Object；"
		"    · LineTraceSingle：`WorldContextObject`、`Start`、`End`、`TraceChannel`、`bTraceComplex`、`ActorsToIgnore`、`DrawDebugType`、`OutHit`、`bIgnoreSelf`、`ReturnValue`；"
		"    · Delay：`Duration`、`NotPastTime`（输出通常为 `Completed`）；"
		"  - 不确定时在 description 中写明需要用户确认，并避免输出不可执行的连接；"
		"6) 优先生成最小可行链路：先 CreateNode，再 ConnectPins，再 SetPinDefault，最后 Comment；"
		"7) 高风险操作必须设置 requiresConfirmation=true，否则执行会被拒绝。高风险清单包括："
		"  - DestroyActor / K2_DestroyActor / DestroyComponent"
		"  - ExecuteConsoleCommand"
		"  - OpenLevel / LoadLevel / OpenLevelBySoftObjectPtr"
		"  - SaveGameToSlot / DeleteGameInSlot"
		"  - QuitGame / SetTimeDilation / SetGlobalTimeDilation"
		"  - SpawnActorFromClass（此外 class 必须在项目 Spawn 白名单里，可通过 Project Settings → Blueprint AI Assistant 配置；默认允许 BP_Cursor / BP_Door_01 / BP_FireBall）；"
		"8) 若需要延时，使用 CreateNode.nodeType=Delay（latent 节点）；"
		"9) steps 建议 5 到 18 步。"
		"9b) 【缺失变量/函数的自动补齐】你必须优先使用上下文中给出的 Variables/Functions；"
		"如果你需要的成员变量不在 Variables 列表里，请在首次使用前插入一步 CreateMemberVariable 来创建它（默认不设置默认值即可）。"
		"如果你需要调用的蓝图自定义函数不在 Functions 列表里，并且它不是引擎/Kismet 常用库函数，请先用 CreateFunctionGraph 创建一个同名函数骨架（kind=Callable 或 Pure），"
		"并将后续相关节点生成在 targetGraph=Function:<该函数名> 的图里（你也可以把这些逻辑写进 CreateFunctionGraph.bodySteps）。"
		"如果你无法确定变量类型或函数签名，至少用 Comment 明确写出“需要地编确认/补齐”的信息，并给出建议的 CreateMemberVariable/CreateFunctionGraph 示例。"
		"10) 新增节点类型用法："
		"  - Cast：`{action:CreateNode, nodeType:Cast, nodeId:Cast_1, targetClass:\"BP_Door_01\"}`；targetClass 支持蓝图短名（如 `BP_Door_01`）或完整路径（如 `/Game/Blueprints/BP_Door_01.BP_Door_01_C`）。输出 pin：`Object`（成功转换）、`Cast Failed`（执行）；输入 pin：`Object`（要转的对象）；"
		"  - IsValid：`{action:CreateNode, nodeType:IsValid, nodeId:IsValid_1}`，函数版 IsValid，pure 节点，输入 `Object`，返回 `ReturnValue`（bool）。通常再接一个 Branch 分 true/false 分支；"
		"  - MakeVector：对应 **`UKismetMathLibrary::MakeVector`**（CallFunction），输入 `X/Y/Z`，**输出 `ReturnValue`（FVector）**；"
		"  - BreakVector：对应 **`UKismetMathLibrary::BreakVector`**，输入 **`InVec`**（FVector），输出 `X/Y/Z`；若写 `Vector` 指输入，执行器会映射到 `InVec`；"
		"  - MakeRotator：对应 **`UKismetMathLibrary::MakeRotator`**，输入为 `Roll/Pitch/Yaw`（与节点显示一致），**输出 `ReturnValue`**；"
		"  - BreakRotator：对应 **`UKismetMathLibrary::BreakRotator`**，输入 **`InRot`**，输出 `Roll/Pitch/Yaw`；写 `Rotator` 指输入时会映射到 `InRot`。"
		"  - ForEachLoop：`{action:CreateNode, nodeType:ForEachLoop, nodeId:Each_1}`（引擎 StandardMacros 宏节点）。常用 pin：输入 `Array`，输出 exec：`LoopBody`/`Completed`，输出元素：`Array Element`（显示名；内部名可能为 `ArrayElement`，可用 FindPinLoose 容错）。"
		"  - SwitchOnInt：`{action:CreateNode, nodeType:SwitchOnInt, nodeId:Sw_1, cases:[\"0\",\"1\",\"2\"]}`。cases 约定为连续整数序列，cases[0] 作为 StartIndex，cases 数量决定分支数量；选择输入 pin 为 `Selection`。"
		"  - SwitchOnString：`{action:CreateNode, nodeType:SwitchOnString, nodeId:SwS_1, cases:[\"Idle\",\"Run\"]}`。选择输入 pin 为 `Selection`，每个 case 对应一个 exec 输出。"
		"  - Select：`{action:CreateNode, nodeType:Select, nodeId:Sel_1, cases:[\"A\",\"B\",\"C\"]}`。cases 用于扩展 option 数量并作为**标签**；真实输入引脚名为 **`Option 0` / `Option 1` / `Option 2`**（带空格）。`SetPinDefault` 应写 `pinName:Option 0` 且 `defaultValue:\"A\"`，**不要把 `A` 当成 pinName**（那是默认值内容）；也可写 `pinName:A` 由执行器按 cases 映射到 Option 0。Index 默认值为整数：`pinName:Index`，`defaultValue:2`。若步骤顺序为「先 SetPinDefault 再 ConnectPins」，执行器会将 Select **先定型为 String（选项）+ Int（Index）**再写字面量；否则 Wildcard 上默认值可能不显示。"
		"  - MakeArrayInt：`{action:CreateNode, nodeType:MakeArrayInt, nodeId:Arr_1, cases:[\"1\",\"2\",\"3\"]}`。cases 用作元素数量；元素值建议用 SetPinDefault 写入各元素 pin（Pin 名通常为 `0`/`1`/`2` 或 `Item_0` 之类，若不确定可直接按 pins 列表修正）。输出 pin 为数组（常显示为 `Array` 或 `ReturnValue`）。"
		"  - InputKey：`{action:CreateNode, targetGraph:\"EventGraph\", nodeType:InputKey, nodeId:\"InputKey_E\", functionName:\"E\"}`。`functionName` 必须是键名；执行器会先复用当前图中同键位事件，没有才创建。常用输出 exec pin 为 `Pressed`、`Released`。"
		"  - 数值/布尔/Name→string：PrintString.InString 需要 string；可显式加 `Conv_FloatToString` / `Conv_IntToString` / `Conv_BoolToString`；执行器在连接失败且目标为 PrintString.InString 时会自动插入对应 Conv_*ToString；"
		"  - float 运算 pin 名：Subtract/Multiply 等常用 A/B；Conv_*ToString 可能是 InFloat 或 InDouble，DSL 写错时执行器会互认；"
		"  - Cast/IsValid 不属于高风险，不需要 requiresConfirmation=true。");
}

FString FBlueprintPromptBuilder::BuildSystemPromptClarifyJson()
{
	return TEXT(
		"你是 Unreal Engine 5.6 蓝图需求澄清助手。请用简体中文思考，但你必须只输出一个合法 JSON 对象本身，且不要输出任何额外文字（不要 Markdown、不要代码块、不要前后缀）。"
		"重要：从第一个字符 `{` 开始到最后一个 `}` 结束。"
		"你的目标不是生成 DSL，而是把模糊需求变成可执行约束。"
		"请输出 2 到 4 个最关键的问题（不要超过 4 个），每个问题都要能直接让用户补充信息以便生成 DSL。"
		"每个问题尽量提供 3 到 6 个常用预选值 options，供 UI 直接展示为可点击选项；用户仍可自定义。"
		"JSON Schema："
		"{\"mode\":\"clarify\",\"questions\":[{\"id\":\"Q1\",\"question\":\"...\",\"options\":[\"选项A\",\"选项B\",\"选项C\"]},{\"id\":\"Q2\",\"question\":\"...\",\"options\":[\"...\"]}]}"
		"规则："
		"1) 问题必须短、具体、可回答；"
		"2) 优先问：目标蓝图/对象来源/触发事件/变量名/Widget 名/是否跨蓝图；"
		"3) options 应是短文本，不要写长段解释；例如目标蓝图可给 [\"角色蓝图\",\"PlayerState\",\"GameMode\",\"单独 Actor 管理器\",\"自定义\"]；触发事件可给 [\"按键 E\",\"Overlap\",\"收集完成\",\"击杀完成\",\"手动调用\",\"自定义\"]；"
		"4) 不要问“你想实现什么”这类重复问题；"
		"5) 如果你判断这是跨多个蓝图的系统需求，也仍先问 2-4 个关键澄清点（例如：主控蓝图是谁、数据存在哪、UI 在哪更新）。");
}

FString FBlueprintPromptBuilder::BuildSystemPromptPlanJson()
{
	return TEXT(
		"你是 Unreal Engine 5.6 蓝图实施计划助手。请用简体中文思考，但你必须只输出一个合法 JSON 对象本身，且不要输出任何额外文字（不要 Markdown、不要代码块、不要前后缀）。"
		"重要：从第一个字符 `{` 开始到最后一个 `}` 结束。"
		"当需求明显涉及多个蓝图/Widget/系统时，不要生成可执行 DSL。请先给出可执行的实施计划（任务拆解），并把每一步写成一个“可转为 DSL 的局部请求”。"
		"JSON Schema："
		"{\"mode\":\"plan\",\"summary\":\"一句话概括\",\"items\":["
		"{\"stepId\":\"P1\",\"title\":\"...\",\"targetHint\":\"建议在哪个蓝图/资产里做（提示即可）\",\"dslPrompt\":\"把这一步转为单蓝图 DSL 的具体请求文本\"}"
		"]}"
		"规则："
		"1) items 建议 3 到 8 条；"
		"2) 每条都要写清：目标蓝图/资产提示 + 该步要实现什么；"
		"3) dslPrompt 必须是“可以直接拿去生成单蓝图 DSL”的文字（含触发、对象来源、变量/Widget 名等）；"
		"4) 若用户未给出项目里真实蓝图名，targetHint 可用类型提示（如 Character / PlayerState / Widget / Door Actor），并在 dslPrompt 中要求用户先打开对应蓝图。");
}

FString FBlueprintPromptBuilder::BuildUserPrompt(const FBlueprintEditorContext& Context, const FString& UserQuestion)
{
	const FString Variables = Context.Variables.Num() > 0 ? FString::Join(Context.Variables, TEXT(", ")) : TEXT("无");
	const FString Functions = Context.Functions.Num() > 0 ? FString::Join(Context.Functions, TEXT(", ")) : TEXT("无");

	return FString::Printf(
		TEXT("当前蓝图信息：\nBlueprint=%s\nType=%s\nVariables=%s\nFunctions=%s\n\n用户问题：%s"),
		*Context.BlueprintName,
		*Context.BlueprintClass,
		*Variables,
		*Functions,
		*UserQuestion);
}
