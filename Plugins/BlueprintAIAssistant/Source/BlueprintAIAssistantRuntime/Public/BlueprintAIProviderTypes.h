#pragma once

#include "CoreMinimal.h"

struct FBlueprintAIRequest
{
	FString SystemPrompt;
	FString UserPrompt;

	/**
	 * 对模型输出 token 的上限；>0 时由 HTTP 层写入各厂商对应字段（如 max_tokens / maxOutputTokens / max_output_tokens），
	 * 0 表示用插件内默认值。增量 patch、执行失败重试等长 JSON 会显式拉大，避免 4k/2k 输出被截断成半截 JSON。
	 */
	int32 MaxOutputTokens = 0;
};

struct FBlueprintAIResponse
{
	bool bSuccess = false;
	FString Content;
	FString Error;
};
