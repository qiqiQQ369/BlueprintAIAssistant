#include "BlueprintAISceneTemplates.h"

const TArray<FBlueprintAISceneTemplate>& FBlueprintAISceneTemplates::GetBuiltIn()
{
	static const TArray<FBlueprintAISceneTemplate> Templates = {
		{
			TEXT("按键交互开门"),
			TEXT("我想做一个按键交互开门的蓝图：\n")
			TEXT("- 玩家按下 E 键时（如果图里已有 E 键输入事件就复用，没有再创建），对玩家前方做一次 LineTrace（长度 300，Visibility 通道）；\n")
			TEXT("- 如果命中到 BP_Door_01 类型的 Actor，就调用它身上的 Open 逻辑（或打印 \"Open Door\"）；\n")
			TEXT("- 未命中则打印 \"No Door\"。\n")
			TEXT("请把节点放到 EventGraph，并给每一步加清晰注释。"),
			true
		},
		{
			TEXT("触发器进入提示"),
			TEXT("我想做一个触发器体积的交互蓝图：\n")
			TEXT("- 当有 Pawn 进入触发体时：使用/创建 **ActorBeginOverlap（事件节点）**（不要把它当作函数去调用），在屏幕打印 \"Enter Trigger\"；\n")
			TEXT("- 当 Pawn 离开触发体时：使用/创建 **ActorEndOverlap（事件节点）**（不要把它当作函数去调用），打印 \"Leave Trigger\"；\n")
			TEXT("- 为进入/离开各加一个注释块。\n")
			TEXT("请使用 EventGraph。"),
			true
		},
		{
			TEXT("延时 Spawn 火球"),
			TEXT("我想做一个延时生成火球的蓝图：\n")
			TEXT("- BeginPlay 后延时 1 秒；\n")
			TEXT("- 在当前 Actor 的位置 + 前方 200 处 Spawn 一个 BP_FireBall；\n")
			TEXT("- 生成成功后打印 \"FireBall Spawned\"。\n")
			TEXT("请使用 EventGraph，注意使用 SpawnActorFromClass。"),
			true
		},
		{
			TEXT("播放蒙太奇"),
			TEXT("我想做一个播放蒙太奇动画的蓝图：\n")
			TEXT("- BeginPlay 时，调用 PlayAnimMontage 播放一个 Montage；\n")
			TEXT("- 播完后打印 \"Played\"。\n")
			TEXT("请使用 EventGraph；Montage 资产路径我稍后手动改。"),
			true
		},
		{
			TEXT("受伤掉血"),
			TEXT("我想给角色加一个简单的受伤逻辑：\n")
			TEXT("- 在当前蓝图新增一个 float 变量 Health（默认值 100，假设已经添加）；\n")
			TEXT("- 当收到 AnyDamage 事件时：Health = Health - Damage；\n")
			TEXT("- 如果 Health <= 0，打印 \"Dead\"，否则打印当前血量。\n")
			TEXT("请使用 EventGraph。"),
			true
		},
		{
			TEXT("金币拾取"),
			TEXT("我想做一个金币拾取（收集）玩法，请直接生成可执行 DSL（不要用 PrintString 代替实现）：\n")
			TEXT("- 当前蓝图是金币 Actor（带碰撞的拾取物）；**不要**把 Overlap 写成 CallFunction。优先在 ConnectPins 里引用语义 nodeId `OnComponentBeginOverlap`（执行器会自动创建/回退）；从 `OtherActor` 连到 Cast.Object，执行线从 `then` 出发。\n")
			TEXT("- Cast 节点请写 `nodeType:Cast, nodeId:Cast_ToCharacter, targetClass:Character`（或你的玩家蓝图类名）；Cast 成功分支用 `then`（不要写 Cast Succeeded）。\n")
			TEXT("- 当 OtherActor 是玩家时：用 `GetVariable`/`SetVariable` 读写玩家上的 int 变量 CoinCount；整数加 1 请写 `functionName:\"Int + Int\"` 或 `Add_IntInt`。\n")
			TEXT("- 然后销毁金币自己（DestroyActor，requiresConfirmation=true）；\n")
			TEXT("- 可选：调用玩家上的 UpdateCoinUI（description 里写「可选」——函数不存在时执行器会自动跳过并桥接执行线）。\n")
			TEXT("要求：节点放到 EventGraph，每一步加清晰注释；不要只打印提示。"),
			true
		},
		{
			TEXT("屏幕提示 PrintString"),
			TEXT("我想测试一下最小 DSL：\n")
			TEXT("- BeginPlay 时打印 \"Hello from AI\" 到屏幕；\n")
			TEXT("- 持续 3 秒。\n")
			TEXT("请使用 EventGraph。"),
			true
		},
		{
			TEXT("前方 LineTrace"),
			TEXT("我想做一个简单的前方检测：\n")
			TEXT("- 按下 F 键时（如果图里已有 F 键输入事件就复用，没有再创建），从当前 Actor 位置沿前方做一次 LineTrace（Visibility，长度 500）；\n")
			TEXT("- 命中就打印命中 Actor 名，没命中就打印 \"Miss\"。\n")
			TEXT("请使用 EventGraph。"),
			true
		}
	};
	return Templates;
}
