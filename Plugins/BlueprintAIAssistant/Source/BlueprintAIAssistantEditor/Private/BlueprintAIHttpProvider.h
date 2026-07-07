#pragma once

#include "BlueprintAIProviderInterface.h"

class FBlueprintAIHttpProvider : public IBlueprintAIProvider
{
public:
	virtual void SendRequest(const FBlueprintAIRequest& Request, TFunction<void(const FBlueprintAIResponse&)> OnCompleted) override;

	/** 导出最近一次 HTTP 响应（已脱敏）到 Saved/BlueprintAIAssistant/http-dumps/，成功返回文件路径。 */
	static bool ExportLastHttpResponseDump(FString& OutFilePath, FString& OutError);
};
