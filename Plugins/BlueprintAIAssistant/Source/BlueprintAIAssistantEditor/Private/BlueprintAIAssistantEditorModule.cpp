#include "BlueprintAIAssistantEditorModule.h"

#include "BlueprintAIAssistantSettings.h"
#include "ISettingsModule.h"
#include "LevelEditor.h"
#include "SBlueprintAIAssistantPanel.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FBlueprintAIAssistantEditorModule"

const FName FBlueprintAIAssistantEditorModule::AssistantTabName(TEXT("BlueprintAIAssistant.MainTab"));

void FBlueprintAIAssistantEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		AssistantTabName,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				[
					SNew(SBlueprintAIAssistantPanel)
				];
		}))
		.SetDisplayName(LOCTEXT("AssistantTabTitle", "Blueprint AI Assistant"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBlueprintAIAssistantEditorModule::RegisterMenus));

	RegisterSettings();
}

void FBlueprintAIAssistantEditorModule::ShutdownModule()
{
	if (UToolMenus* ToolMenus = UToolMenus::TryGet())
	{
		ToolMenus->UnregisterOwner(this);
	}

	UnregisterSettings();
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AssistantTabName);
}

void FBlueprintAIAssistantEditorModule::RegisterMenus()
{
	if (UToolMenus* ToolMenus = UToolMenus::TryGet())
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = ToolMenus->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("BlueprintAIAssistant"));

		Section.AddMenuEntry(
			TEXT("OpenBlueprintAIAssistant"),
			LOCTEXT("OpenAssistantLabel", "Blueprint AI Assistant"),
			LOCTEXT("OpenAssistantTooltip", "Open Blueprint AI Assistant panel."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FBlueprintAIAssistantEditorModule::OpenAssistantTab)));
	}
}

void FBlueprintAIAssistantEditorModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings(
			"Project",
			"Plugins",
			"BlueprintAIAssistant",
			LOCTEXT("SettingsName", "Blueprint AI Assistant"),
			LOCTEXT("SettingsDescription", "Settings for Blueprint AI Assistant provider."),
			GetMutableDefault<UBlueprintAIAssistantSettings>());
	}
}

void FBlueprintAIAssistantEditorModule::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "BlueprintAIAssistant");
	}
}

void FBlueprintAIAssistantEditorModule::OpenAssistantTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(AssistantTabName);
}

IMPLEMENT_MODULE(FBlueprintAIAssistantEditorModule, BlueprintAIAssistantEditor)

#undef LOCTEXT_NAMESPACE
