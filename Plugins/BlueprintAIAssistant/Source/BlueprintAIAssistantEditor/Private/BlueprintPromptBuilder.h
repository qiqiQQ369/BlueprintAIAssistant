#pragma once

#include "CoreMinimal.h"
#include "BlueprintContextCollector.h"

class FBlueprintPromptBuilder
{
public:
	static FString BuildSystemPrompt();
	static FString BuildSystemPromptGuidedStepsJson();
	static FString BuildSystemPromptDslJson();
	static FString BuildSystemPromptClarifyJson();
	static FString BuildSystemPromptPlanJson();
	static FString BuildUserPrompt(const FBlueprintEditorContext& Context, const FString& UserQuestion);
};
