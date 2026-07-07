#include "BlueprintContextCollector.h"

#include "Engine/Blueprint.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"

UBlueprint* FBlueprintContextCollector::FindActiveBlueprint()
{
	if (!GEditor)
	{
		return nullptr;
	}

	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
		for (UObject* Asset : EditedAssets)
		{
			if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
			{
				return Blueprint;
			}
		}
	}

	return nullptr;
}

bool FBlueprintContextCollector::TryCollectCurrentContext(FBlueprintEditorContext& OutContext, FString& OutError)
{
	UBlueprint* Blueprint = FindActiveBlueprint();
	if (!Blueprint)
	{
		OutError = TEXT("未找到正在编辑的蓝图。请先打开一个 Blueprint 资源。");
		return false;
	}

	OutContext.BlueprintName = Blueprint->GetName();
	OutContext.BlueprintClass = Blueprint->BlueprintType == BPTYPE_Interface ? TEXT("Interface") : TEXT("Blueprint");
	OutContext.Variables.Reset();
	OutContext.Functions.Reset();

	for (const FBPVariableDescription& VarDesc : Blueprint->NewVariables)
	{
		OutContext.Variables.Add(FString::Printf(TEXT("%s : %s"), *VarDesc.VarName.ToString(), *VarDesc.VarType.PinCategory.ToString()));
	}

	TArray<UEdGraph*> FunctionGraphs;
	Blueprint->GetAllGraphs(FunctionGraphs);
	for (UEdGraph* Graph : FunctionGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		if (Graph->GetFName() != UEdGraphSchema_K2::FN_UserConstructionScript)
		{
			OutContext.Functions.Add(Graph->GetName());
		}
	}

	return true;
}

UBlueprint* FBlueprintContextCollector::GetActiveBlueprint()
{
	return FindActiveBlueprint();
}
