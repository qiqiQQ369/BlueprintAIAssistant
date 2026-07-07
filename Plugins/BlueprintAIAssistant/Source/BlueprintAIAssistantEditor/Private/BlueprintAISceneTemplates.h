#pragma once

#include "CoreMinimal.h"

/** 一个"场景快捷按钮"对应的模板数据 */
struct FBlueprintAISceneTemplate
{
	/** 显示在按钮上的短名 */
	FString Title;

	/** 点击后填入输入框的完整 prompt（中文；带必要上下文提示） */
	FString Prompt;

	/** 推荐对应的生成模式：true=直接生成 DSL，false=只生成建议/步骤清单 */
	bool bPreferDsl = true;
};

/** 内置场景模板。后续可换为 .ini 配置 */
class FBlueprintAISceneTemplates
{
public:
	static const TArray<FBlueprintAISceneTemplate>& GetBuiltIn();
};
