#include "BlueprintAIAssistantSettings.h"

#include "GenericPlatform/GenericPlatformHttp.h"

void UBlueprintAIAssistantSettings::ResolveCurrentProvider(FString& OutEndpointUrl, FString& OutModel, FString& OutApiKey) const
{
	switch (ProviderKind)
	{
	case EBlueprintAIProviderKind::DeepSeek:
		OutEndpointUrl = DeepSeekEndpointUrl;
		OutModel = DeepSeekModel;
		OutApiKey = DeepSeekApiKey;
		break;
	case EBlueprintAIProviderKind::Doubao:
		OutEndpointUrl = DoubaoEndpointUrl;
		OutModel = DoubaoModel;
		OutApiKey = DoubaoApiKey;
		break;
	case EBlueprintAIProviderKind::Gemini:
		OutEndpointUrl = BuildGeminiRequestUrl();
		OutModel = GeminiModel;
		OutApiKey.Empty();
		break;
	case EBlueprintAIProviderKind::OpenAICompatible:
	default:
		OutEndpointUrl = OpenAIEndpointUrl;
		OutModel = OpenAIModel;
		OutApiKey = OpenAIApiKey;
		break;
	}
}

void UBlueprintAIAssistantSettings::GetProviderDisplayInfo(
	FString& OutKindLabel,
	FString& OutModelDisplay,
	FString& OutEndpointDisplay) const
{
	switch (ProviderKind)
	{
	case EBlueprintAIProviderKind::DeepSeek:
		OutKindLabel = TEXT("DeepSeek");
		OutModelDisplay = DeepSeekModel;
		OutEndpointDisplay = DeepSeekEndpointUrl;
		break;
	case EBlueprintAIProviderKind::Doubao:
		OutKindLabel = TEXT("Doubao");
		OutModelDisplay = DoubaoModel;
		OutEndpointDisplay = DoubaoEndpointUrl;
		break;
	case EBlueprintAIProviderKind::Gemini:
	{
		OutKindLabel = TEXT("Gemini");
		FString ModelId = GeminiModel.TrimStartAndEnd();
		ModelId.ReplaceInline(TEXT("models/"), TEXT(""));
		OutModelDisplay = ModelId;
		FString Base = GeminiApiBaseUrl.TrimStartAndEnd();
		if (!Base.IsEmpty() && Base.EndsWith(TEXT("/")))
		{
			Base = Base.Left(Base.Len() - 1);
		}
		OutEndpointDisplay = FString::Printf(TEXT("%s/models/%s:generateContent?key=(hidden)"), *Base, *ModelId);
		break;
	}
	case EBlueprintAIProviderKind::OpenAICompatible:
	default:
		OutKindLabel = TEXT("OpenAICompatible");
		OutModelDisplay = OpenAIModel;
		OutEndpointDisplay = OpenAIEndpointUrl;
		break;
	}
}

FString UBlueprintAIAssistantSettings::BuildGeminiRequestUrl() const
{
	FString Base = GeminiApiBaseUrl.TrimStartAndEnd();
	if (!Base.IsEmpty() && Base.EndsWith(TEXT("/")))
	{
		Base = Base.Left(Base.Len() - 1);
	}
	if (Base.IsEmpty())
	{
		Base = TEXT("https://generativelanguage.googleapis.com/v1beta");
	}

	FString ModelId = GeminiModel.TrimStartAndEnd();
	ModelId.ReplaceInline(TEXT("models/"), TEXT(""));

	const FString EncodedKey = FGenericPlatformHttp::UrlEncode(GeminiApiKey);

	return FString::Printf(TEXT("%s/models/%s:generateContent?key=%s"), *Base, *ModelId, *EncodedKey);
}
