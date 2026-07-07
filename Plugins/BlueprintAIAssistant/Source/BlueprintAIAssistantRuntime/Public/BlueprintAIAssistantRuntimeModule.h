#pragma once

#include "Modules/ModuleManager.h"

class FBlueprintAIAssistantRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
