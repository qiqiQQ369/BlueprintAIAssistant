using UnrealBuildTool;

public class BlueprintAIAssistantEditor : ModuleRules
{
	public BlueprintAIAssistantEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"UnrealEd",
				"ToolMenus",
				"WorkspaceMenuStructure",
				"Projects",
				"HTTP",
				"Json",
				"JsonUtilities",
				"BlueprintGraph",
				"Kismet",
				"GraphEditor",
				"Settings",
				"BlueprintAIAssistantRuntime"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"AssetRegistry"
			}
		);
	}
}
