#pragma once

#include "CoreMinimal.h"

class UBlueprint;

struct FBlueprintEditorContext
{
	FString BlueprintName;
	FString BlueprintClass;
	TArray<FString> Variables;
	TArray<FString> Functions;
};

class FBlueprintContextCollector
{
public:
	static bool TryCollectCurrentContext(FBlueprintEditorContext& OutContext, FString& OutError);
	static UBlueprint* GetActiveBlueprint();

private:
	static UBlueprint* FindActiveBlueprint();
};
