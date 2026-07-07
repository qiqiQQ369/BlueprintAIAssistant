#pragma once

#include "BlueprintAIProviderTypes.h"

class IBlueprintAIProvider
{
public:
	virtual ~IBlueprintAIProvider() = default;
	virtual void SendRequest(const FBlueprintAIRequest& Request, TFunction<void(const FBlueprintAIResponse&)> OnCompleted) = 0;
};
