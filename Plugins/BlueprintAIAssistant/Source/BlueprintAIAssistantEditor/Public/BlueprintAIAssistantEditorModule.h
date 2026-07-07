#pragma once

#include "Modules/ModuleManager.h"

class FBlueprintAIAssistantEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	void RegisterSettings();
	void UnregisterSettings();
	void OpenAssistantTab();

private:
	static const FName AssistantTabName;
};
