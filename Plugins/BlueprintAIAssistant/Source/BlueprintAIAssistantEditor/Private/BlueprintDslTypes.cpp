#include "BlueprintDslTypes.h"

#include "BlueprintAIAssistantSettings.h"
#include "Dom/JsonObject.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static FString ActionTypeToString(EBlueprintDslActionType Type)
{
	switch (Type)
	{
	case EBlueprintDslActionType::CreateNode: return TEXT("CreateNode");
	case EBlueprintDslActionType::ConnectPins: return TEXT("ConnectPins");
	case EBlueprintDslActionType::SetPinDefault: return TEXT("SetPinDefault");
	case EBlueprintDslActionType::Comment: return TEXT("Comment");
	case EBlueprintDslActionType::GetVariable: return TEXT("GetVariable");
	case EBlueprintDslActionType::SetVariable: return TEXT("SetVariable");
	case EBlueprintDslActionType::CreateMemberVariable: return TEXT("CreateMemberVariable");
	case EBlueprintDslActionType::CreateFunctionGraph: return TEXT("CreateFunctionGraph");
	default: return TEXT("Unknown");
	}
}

static void SerializeSimpleType(const FBlueprintDslSimplePinType& T, TSharedRef<FJsonObject> OutObj)
{
	if (!T.Type.IsEmpty()) OutObj->SetStringField(TEXT("type"), T.Type);
	if (!T.Ref.IsEmpty()) OutObj->SetStringField(TEXT("ref"), T.Ref);
	if (!T.Container.IsEmpty()) OutObj->SetStringField(TEXT("container"), T.Container);
	if (!T.TypePath.IsEmpty()) OutObj->SetStringField(TEXT("typePath"), T.TypePath);
	if (T.KeyType.IsValid())
	{
		TSharedRef<FJsonObject> KeyObj = MakeShared<FJsonObject>();
		SerializeSimpleType(*T.KeyType, KeyObj);
		OutObj->SetObjectField(TEXT("keyType"), KeyObj);
	}
	if (T.ValueType.IsValid())
	{
		TSharedRef<FJsonObject> ValObj = MakeShared<FJsonObject>();
		SerializeSimpleType(*T.ValueType, ValObj);
		OutObj->SetObjectField(TEXT("valueType"), ValObj);
	}
}

static void SerializeSimplePinDecl(const FBlueprintDslSimplePinDecl& P, TSharedRef<FJsonObject> OutObj)
{
	OutObj->SetStringField(TEXT("name"), P.Name);
	TSharedRef<FJsonObject> TObj = MakeShared<FJsonObject>();
	SerializeSimpleType(P.Type, TObj);
	OutObj->SetObjectField(TEXT("type"), TObj);
}

FString SerializeDslStepsToJson(const TArray<FBlueprintDslActionStep>& Steps, int32 Version)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), Version <= 0 ? 2 : Version);

	TArray<TSharedPtr<FJsonValue>> Arr;
	Arr.Reserve(Steps.Num());

	for (const FBlueprintDslActionStep& Step : Steps)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("action"), ActionTypeToString(Step.ActionType));
		if (!Step.StepId.IsEmpty()) Obj->SetStringField(TEXT("stepId"), Step.StepId);
		if (!Step.Description.IsEmpty()) Obj->SetStringField(TEXT("description"), Step.Description);
		if (!Step.TargetGraph.IsEmpty()) Obj->SetStringField(TEXT("targetGraph"), Step.TargetGraph);
		if (!Step.TargetBlueprintAssetPath.IsEmpty()) Obj->SetStringField(TEXT("targetBlueprint"), Step.TargetBlueprintAssetPath);
		Obj->SetBoolField(TEXT("requiresConfirmation"), Step.bRequiresConfirmation);

		switch (Step.ActionType)
		{
		case EBlueprintDslActionType::CreateNode:
			Obj->SetStringField(TEXT("nodeId"), Step.NodeId);
			Obj->SetStringField(TEXT("nodeType"), Step.NodeType);
			if (!Step.FunctionName.IsEmpty()) Obj->SetStringField(TEXT("functionName"), Step.FunctionName);
			if (!Step.TargetClass.IsEmpty()) Obj->SetStringField(TEXT("targetClass"), Step.TargetClass);
			if (!Step.VarName.IsEmpty()) Obj->SetStringField(TEXT("varName"), Step.VarName);
			if (!Step.ValueFromNodeId.IsEmpty() || !Step.ValueFromPin.IsEmpty())
			{
				TSharedRef<FJsonObject> VF = MakeShared<FJsonObject>();
				if (!Step.ValueFromNodeId.IsEmpty()) VF->SetStringField(TEXT("nodeId"), Step.ValueFromNodeId);
				if (!Step.ValueFromPin.IsEmpty()) VF->SetStringField(TEXT("pin"), Step.ValueFromPin);
				Obj->SetObjectField(TEXT("valueFrom"), VF);
			}
			if (!Step.DefaultValue.IsEmpty()) Obj->SetStringField(TEXT("defaultValue"), Step.DefaultValue);
			if (Step.CaseValues.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Cases;
				for (const FString& C : Step.CaseValues)
				{
					Cases.Add(MakeShared<FJsonValueString>(C));
				}
				Obj->SetArrayField(TEXT("cases"), Cases);
			}
			break;
		case EBlueprintDslActionType::ConnectPins:
			Obj->SetStringField(TEXT("fromNodeId"), Step.FromNodeId);
			Obj->SetStringField(TEXT("fromPin"), Step.FromPin);
			Obj->SetStringField(TEXT("toNodeId"), Step.ToNodeId);
			Obj->SetStringField(TEXT("toPin"), Step.ToPin);
			break;
		case EBlueprintDslActionType::SetPinDefault:
			Obj->SetStringField(TEXT("nodeId"), Step.NodeId);
			Obj->SetStringField(TEXT("pinName"), Step.PinName);
			Obj->SetStringField(TEXT("defaultValue"), Step.DefaultValue);
			break;
		case EBlueprintDslActionType::Comment:
			Obj->SetStringField(TEXT("commentText"), Step.CommentText);
			if (Step.AttachToNodeIds.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Attach;
				for (const FString& Id : Step.AttachToNodeIds)
				{
					Attach.Add(MakeShared<FJsonValueString>(Id));
				}
				Obj->SetArrayField(TEXT("attachToNodeIds"), Attach);
			}
			break;
		case EBlueprintDslActionType::GetVariable:
			Obj->SetStringField(TEXT("nodeId"), Step.NodeId);
			Obj->SetStringField(TEXT("varName"), Step.VarName);
			break;
		case EBlueprintDslActionType::SetVariable:
			Obj->SetStringField(TEXT("nodeId"), Step.NodeId);
			Obj->SetStringField(TEXT("varName"), Step.VarName);
			if (!Step.ValueFromNodeId.IsEmpty() || !Step.ValueFromPin.IsEmpty())
			{
				TSharedRef<FJsonObject> VF = MakeShared<FJsonObject>();
				if (!Step.ValueFromNodeId.IsEmpty()) VF->SetStringField(TEXT("nodeId"), Step.ValueFromNodeId);
				if (!Step.ValueFromPin.IsEmpty()) VF->SetStringField(TEXT("pin"), Step.ValueFromPin);
				Obj->SetObjectField(TEXT("valueFrom"), VF);
			}
			if (!Step.DefaultValue.IsEmpty()) Obj->SetStringField(TEXT("defaultValue"), Step.DefaultValue);
			break;
		case EBlueprintDslActionType::CreateMemberVariable:
		{
			Obj->SetStringField(TEXT("varName"), Step.VarName);
			TSharedRef<FJsonObject> TypeObj = MakeShared<FJsonObject>();
			SerializeSimpleType(Step.MemberVarType, TypeObj);
			Obj->SetObjectField(TEXT("varType"), TypeObj);
			if (!Step.MemberVarExposure.IsEmpty()) Obj->SetStringField(TEXT("exposure"), Step.MemberVarExposure);
			break;
		}
		case EBlueprintDslActionType::CreateFunctionGraph:
		{
			Obj->SetStringField(TEXT("name"), Step.FunctionGraphName);
			Obj->SetStringField(TEXT("kind"), Step.FunctionGraphKind);
			if (Step.FunctionParams.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Pins;
				for (const auto& P : Step.FunctionParams)
				{
					TSharedRef<FJsonObject> PObj = MakeShared<FJsonObject>();
					SerializeSimplePinDecl(P, PObj);
					Pins.Add(MakeShared<FJsonValueObject>(PObj));
				}
				Obj->SetArrayField(TEXT("params"), Pins);
			}
			if (Step.FunctionReturns.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Pins;
				for (const auto& P : Step.FunctionReturns)
				{
					TSharedRef<FJsonObject> PObj = MakeShared<FJsonObject>();
					SerializeSimplePinDecl(P, PObj);
					Pins.Add(MakeShared<FJsonValueObject>(PObj));
				}
				Obj->SetArrayField(TEXT("returns"), Pins);
			}
			if (Step.FunctionBodySteps.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Sub;
				for (const auto& S : Step.FunctionBodySteps)
				{
					// 子 step 直接序列化为对象
					TArray<FBlueprintDslActionStep> One;
					One.Add(S);
					const FString OneJson = SerializeDslStepsToJson(One, Step.Version);
					TSharedPtr<FJsonObject> Root2;
					const TSharedRef<TJsonReader<TCHAR>> R = TJsonReaderFactory<TCHAR>::Create(OneJson);
					if (FJsonSerializer::Deserialize(R, Root2) && Root2.IsValid())
					{
						const TArray<TSharedPtr<FJsonValue>>* Arr2 = nullptr;
						if (Root2->TryGetArrayField(TEXT("steps"), Arr2) && Arr2 && Arr2->Num() == 1)
						{
							Sub.Add((*Arr2)[0]);
						}
					}
				}
				if (Sub.Num() > 0)
				{
					Obj->SetArrayField(TEXT("bodySteps"), Sub);
				}
			}
			break;
		}
		default:
			break;
		}

		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	Root->SetArrayField(TEXT("steps"), Arr);

	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

static FString TrimJsonEnvelopeIfPresent(const FString& In)
{
	// DSL Prompt 要求严格 JSON，但模型偶尔会加前后缀或 Markdown。
	// 这里扫描第一个完整 JSON 对象，避免用“第一个 { 到最后一个 }”误吃掉额外文本。
	const FString S = In.TrimStartAndEnd();
	int32 Start = INDEX_NONE;
	int32 Depth = 0;
	bool bInString = false;
	bool bEscape = false;
	for (int32 i = 0; i < S.Len(); ++i)
	{
		const TCHAR C = S[i];
		if (bInString)
		{
			if (bEscape)
			{
				bEscape = false;
			}
			else if (C == TEXT('\\'))
			{
				bEscape = true;
			}
			else if (C == TEXT('"'))
			{
				bInString = false;
			}
			continue;
		}

		if (C == TEXT('"'))
		{
			bInString = true;
			continue;
		}
		if (C == TEXT('{'))
		{
			if (Depth == 0)
			{
				Start = i;
			}
			++Depth;
			continue;
		}
		if (C == TEXT('}') && Depth > 0)
		{
			--Depth;
			if (Depth == 0 && Start != INDEX_NONE)
			{
				return S.Mid(Start, i - Start + 1);
			}
		}
	}
	return S;
}

/** 去掉 ``` / ```json 代码块外壳（模型常包一层 Markdown）。 */
static FString StripMarkdownCodeFence(FString In)
{
	FString S = In.TrimStartAndEnd();
	if (!S.StartsWith(TEXT("```")))
	{
		return S;
	}
	const int32 LineEnd = S.Find(TEXT("\n"));
	if (LineEnd == INDEX_NONE)
	{
		return S;
	}
	S = S.Mid(LineEnd + 1);
	const int32 Close = S.Find(TEXT("```"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (Close != INDEX_NONE)
	{
		S = S.Left(Close);
	}
	return S.TrimStartAndEnd();
}

/**
 * 按 JSON 字符串规则反转义一段内容（用于剥掉最外层引号后的 body）。
 * 处理 \" \\ \/ \b \f \n \r \t \uXXXX。
 */
static FString UnescapeJsonStringBody(const FString& Body)
{
	FString Out;
	Out.Reserve(Body.Len());
	const int32 Len = Body.Len();
	for (int32 i = 0; i < Len; ++i)
	{
		const TCHAR C = Body[i];
		if (C != TEXT('\\') || i + 1 >= Len)
		{
			Out.AppendChar(C);
			continue;
		}
		const TCHAR N = Body[++i];
		switch (N)
		{
		case TEXT('"'): Out.AppendChar(TEXT('"')); break;
		case TEXT('\\'): Out.AppendChar(TEXT('\\')); break;
		case TEXT('/'): Out.AppendChar(TEXT('/')); break;
		case TEXT('b'): Out.AppendChar(TEXT('\b')); break;
		case TEXT('f'): Out.AppendChar(TEXT('\f')); break;
		case TEXT('n'): Out.AppendChar(TEXT('\n')); break;
		case TEXT('r'): Out.AppendChar(TEXT('\r')); break;
		case TEXT('t'): Out.AppendChar(TEXT('\t')); break;
		case TEXT('u'):
			if (i + 4 < Len)
			{
				const FString Hex = Body.Mid(i + 1, 4);
				i += 4;
				const uint32 U = FParse::HexNumber(FStringView(*Hex, Hex.Len()));
				Out.AppendChar(static_cast<TCHAR>(U));
			}
			break;
		default:
			Out.AppendChar(N);
			break;
		}
	}
	return Out;
}

static bool TryDeserializeRootObject(const FString& JsonText, TSharedPtr<FJsonObject>& OutObj)
{
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonText);
	return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
}

/**
 * 模型/代理有时返回「整段 JSON 被当成字符串」：`"{\"version\":2,...}"`，或键名被写成 {\"key\"。
 * UE 的 FJsonSerializer 无法直接解析根为 String 的文档，这里做多层剥离与反转义后再解析。
 */
static bool NormalizeAndDeserializeDslRoot(const FString& InRaw, TSharedPtr<FJsonObject>& OutRootObj)
{
	FString S = StripMarkdownCodeFence(InRaw).TrimStartAndEnd();
	// 部分模型/网关会把引号变成中文全角/花引号，导致 JSON 解析失败；这里先归一化。
	S.ReplaceInline(TEXT("“"), TEXT("\""));
	S.ReplaceInline(TEXT("”"), TEXT("\""));
	S.ReplaceInline(TEXT("‘"), TEXT("'"));
	S.ReplaceInline(TEXT("’"), TEXT("'"));
	// 少数网关会在开头混入 BOM/零宽字符/NULL 或其它控制字符，UE 的 JSON 解析会直接失败。
	// 这里做一次轻量清洗：去掉 UTF-8 BOM(0xFEFF)、NULL，以及除 \t\r\n 外的 ASCII 控制字符。
	{
		FString Clean;
		Clean.Reserve(S.Len());
		for (int32 i = 0; i < S.Len(); ++i)
		{
			const TCHAR C = S[i];
			if (C == 0 || C == 0xFEFF)
			{
				continue;
			}
			if (C < 32 && C != TEXT('\t') && C != TEXT('\r') && C != TEXT('\n'))
			{
				continue;
			}
			Clean.AppendChar(C);
		}
		S = Clean.TrimStartAndEnd();
	}

	for (int32 Pass = 0; Pass < 8; ++Pass)
	{
		// 1) 直接对象
		if (TryDeserializeRootObject(S, OutRootObj))
		{
			return true;
		}

		// 2) 只取 { ... } 子串（忽略前后说明文字）
		const FString Braced = TrimJsonEnvelopeIfPresent(S);
		if (!Braced.Equals(S) && TryDeserializeRootObject(Braced, OutRootObj))
		{
			return true;
		}
		if (!Braced.Equals(S))
		{
			S = Braced;
			continue;
		}

		// 3) 整段是带引号的 JSON 字符串字面量：剥一层 + 反转义
		if (S.Len() >= 2 && S[0] == TEXT('"') && S[S.Len() - 1] == TEXT('"'))
		{
			S = UnescapeJsonStringBody(S.Mid(1, S.Len() - 2));
			continue;
		}

		// 4) 形如 {\"version\": ...}（非法 JSON，键引脚被转义）：整段当字符串体反转义
		if (S.Len() >= 3 && S[0] == TEXT('{') && S[1] == TEXT('\\') && S[2] == TEXT('"'))
		{
			S = UnescapeJsonStringBody(S);
			continue;
		}

		// 5) 代理多包一层反斜杠：`\\"` -> `\"`（最多剥几层）
		FString Collapsed = S;
		Collapsed.ReplaceInline(TEXT("\\\\\""), TEXT("\\\""), ESearchCase::CaseSensitive);
		if (!Collapsed.Equals(S))
		{
			S = Collapsed;
			continue;
		}

		break;
	}

	return false;
}

EBlueprintDslActionType BlueprintDslActionTypeFromString(const FString& InAction)
{
	const FString A = InAction.TrimStartAndEnd();
	if (A.Equals(TEXT("CreateNode"), ESearchCase::IgnoreCase)) return EBlueprintDslActionType::CreateNode;
	if (A.Equals(TEXT("ConnectPins"), ESearchCase::IgnoreCase)) return EBlueprintDslActionType::ConnectPins;
	if (A.Equals(TEXT("SetPinDefault"), ESearchCase::IgnoreCase)) return EBlueprintDslActionType::SetPinDefault;
	if (A.Equals(TEXT("Comment"), ESearchCase::IgnoreCase)) return EBlueprintDslActionType::Comment;
	if (A.Equals(TEXT("GetVariable"), ESearchCase::IgnoreCase)) return EBlueprintDslActionType::GetVariable;
	if (A.Equals(TEXT("SetVariable"), ESearchCase::IgnoreCase)) return EBlueprintDslActionType::SetVariable;
	if (A.Equals(TEXT("CreateMemberVariable"), ESearchCase::IgnoreCase)) return EBlueprintDslActionType::CreateMemberVariable;
	if (A.Equals(TEXT("CreateFunctionGraph"), ESearchCase::IgnoreCase)) return EBlueprintDslActionType::CreateFunctionGraph;
	return EBlueprintDslActionType::Unknown;
}

static bool ParseSimpleTypeObj(const TSharedPtr<FJsonObject>& Obj, FBlueprintDslSimplePinType& Out)
{
	if (!Obj.IsValid())
	{
		return false;
	}
	Obj->TryGetStringField(TEXT("type"), Out.Type);
	Obj->TryGetStringField(TEXT("ref"), Out.Ref);
	Obj->TryGetStringField(TEXT("container"), Out.Container);
	Obj->TryGetStringField(TEXT("typePath"), Out.TypePath);

	const TSharedPtr<FJsonObject>* KeyObj = nullptr;
	if (Obj->TryGetObjectField(TEXT("keyType"), KeyObj) && KeyObj && KeyObj->IsValid())
	{
		Out.KeyType = MakeShared<FBlueprintDslSimplePinType>();
		ParseSimpleTypeObj(*KeyObj, *Out.KeyType);
	}
	const TSharedPtr<FJsonObject>* ValObj = nullptr;
	if (Obj->TryGetObjectField(TEXT("valueType"), ValObj) && ValObj && ValObj->IsValid())
	{
		Out.ValueType = MakeShared<FBlueprintDslSimplePinType>();
		ParseSimpleTypeObj(*ValObj, *Out.ValueType);
	}
	return !Out.Type.IsEmpty() || !Out.Container.IsEmpty();
}

static void ParsePinsArray(const TSharedPtr<FJsonObject>& StepObj, const FString& Key, TArray<FBlueprintDslSimplePinDecl>& OutPins)
{
	OutPins.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!StepObj.IsValid() || !StepObj->TryGetArrayField(Key, Arr) || !Arr)
	{
		return;
	}
	for (const auto& V : *Arr)
	{
		const TSharedPtr<FJsonObject>* PObj = nullptr;
		if (!V.IsValid() || !V->TryGetObject(PObj) || !PObj || !PObj->IsValid())
		{
			continue;
		}
		FBlueprintDslSimplePinDecl P;
		(*PObj)->TryGetStringField(TEXT("name"), P.Name);
		const TSharedPtr<FJsonObject>* TObj = nullptr;
		if ((*PObj)->TryGetObjectField(TEXT("type"), TObj) && TObj && TObj->IsValid())
		{
			ParseSimpleTypeObj(*TObj, P.Type);
		}
		if (!P.Name.IsEmpty())
		{
			OutPins.Add(MoveTemp(P));
		}
	}
}

static bool TryGetString(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, FString& Out)
{
	Out.Empty();
	return Obj.IsValid() && Obj->TryGetStringField(Field, Out);
}

static bool TryGetBoolLoose(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, bool& Out)
{
	Out = false;
	if (!Obj.IsValid())
	{
		return false;
	}
	if (Obj->TryGetBoolField(Field, Out))
	{
		return true;
	}
	FString S;
	if (Obj->TryGetStringField(Field, S))
	{
		if (S.Equals(TEXT("true"), ESearchCase::IgnoreCase) || S.Equals(TEXT("1")))
		{
			Out = true;
			return true;
		}
		if (S.Equals(TEXT("false"), ESearchCase::IgnoreCase) || S.Equals(TEXT("0")))
		{
			Out = false;
			return true;
		}
	}
	return false;
}

static void ParseVarNameAliases(const TSharedPtr<FJsonObject>& StepObj, FString& OutVarName);

static FString NormalizeActionString(FString In)
{
	FString A = In.TrimStartAndEnd();
	A.ReplaceInline(TEXT("-"), TEXT("_"));
	A.ReplaceInline(TEXT(" "), TEXT("_"));
	A.ReplaceInline(TEXT("__"), TEXT("_"));

	// 常见变体：create_node / connect_pins / set_pin_default / set_default / comment / get_variable / set_variable
	if (A.Equals(TEXT("create_node"), ESearchCase::IgnoreCase)) return TEXT("CreateNode");
	if (A.Equals(TEXT("createnode"), ESearchCase::IgnoreCase)) return TEXT("CreateNode");
	if (A.Equals(TEXT("connect_pins"), ESearchCase::IgnoreCase)) return TEXT("ConnectPins");
	if (A.Equals(TEXT("connectpins"), ESearchCase::IgnoreCase)) return TEXT("ConnectPins");
	if (A.Equals(TEXT("set_pin_default"), ESearchCase::IgnoreCase)) return TEXT("SetPinDefault");
	if (A.Equals(TEXT("setdefault"), ESearchCase::IgnoreCase)) return TEXT("SetPinDefault");
	if (A.Equals(TEXT("set_default"), ESearchCase::IgnoreCase)) return TEXT("SetPinDefault");
	if (A.Equals(TEXT("comment"), ESearchCase::IgnoreCase)) return TEXT("Comment");
	if (A.Equals(TEXT("get_variable"), ESearchCase::IgnoreCase)) return TEXT("GetVariable");
	if (A.Equals(TEXT("getvariable"), ESearchCase::IgnoreCase)) return TEXT("GetVariable");
	if (A.Equals(TEXT("set_variable"), ESearchCase::IgnoreCase)) return TEXT("SetVariable");
	if (A.Equals(TEXT("setvariable"), ESearchCase::IgnoreCase)) return TEXT("SetVariable");
	if (A.Equals(TEXT("create_member_variable"), ESearchCase::IgnoreCase)) return TEXT("CreateMemberVariable");
	if (A.Equals(TEXT("createmembervariable"), ESearchCase::IgnoreCase)) return TEXT("CreateMemberVariable");
	if (A.Equals(TEXT("create_function_graph"), ESearchCase::IgnoreCase)) return TEXT("CreateFunctionGraph");
	if (A.Equals(TEXT("createfunctiongraph"), ESearchCase::IgnoreCase)) return TEXT("CreateFunctionGraph");

	// 兜底：如果本来就是 CreateNode/ConnectPins 等，保持原样
	return In.TrimStartAndEnd();
}

static FString NormalizeSimpleTypeName(FString In)
{
	FString T = In.TrimStartAndEnd();
	T.ReplaceInline(TEXT(" "), TEXT(""));
	T.ReplaceInline(TEXT("_"), TEXT(""));
	if (T.IsEmpty())
	{
		return TEXT("");
	}

	const FString Lower = T.ToLower();
	if (Lower.Equals(TEXT("float")) || Lower.Equals(TEXT("double")) || Lower.Equals(TEXT("real")) || Lower.Equals(TEXT("number")))
	{
		return TEXT("float");
	}
	if (Lower.Equals(TEXT("int")) || Lower.Equals(TEXT("integer")) || Lower.Equals(TEXT("int32")))
	{
		return TEXT("int");
	}
	if (Lower.Equals(TEXT("bool")) || Lower.Equals(TEXT("boolean")))
	{
		return TEXT("bool");
	}
	if (Lower.Equals(TEXT("string")) || Lower.Equals(TEXT("str")) || Lower.Equals(TEXT("fstring")))
	{
		return TEXT("string");
	}
	if (Lower.Equals(TEXT("text")) || Lower.Equals(TEXT("ftext")))
	{
		return TEXT("text");
	}
	if (Lower.Equals(TEXT("name")) || Lower.Equals(TEXT("fname")))
	{
		return TEXT("name");
	}
	if (Lower.Equals(TEXT("vector")) || Lower.Equals(TEXT("fvector")))
	{
		return TEXT("vector");
	}
	if (Lower.Equals(TEXT("rotator")) || Lower.Equals(TEXT("frotator")))
	{
		return TEXT("rotator");
	}
	if (Lower.Equals(TEXT("transform")) || Lower.Equals(TEXT("ftransform")))
	{
		return TEXT("transform");
	}
	if (Lower.Equals(TEXT("actor")) || Lower.Equals(TEXT("object")))
	{
		return Lower;
	}
	if (Lower.Equals(TEXT("widget")) || Lower.Equals(TEXT("userwidget")))
	{
		return TEXT("UserWidget");
	}

	return TEXT("");
}

static FString InferSimpleTypeFromVarName(const FString& InVarName)
{
	const FString Name = InVarName.TrimStartAndEnd().ToLower();
	if (Name.IsEmpty())
	{
		return TEXT("");
	}

	if (Name.Contains(TEXT("widget")))
	{
		return TEXT("UserWidget");
	}
	if (Name.Contains(TEXT("health")) || Name.Contains(TEXT("hp")) || Name.Contains(TEXT("damage")) ||
		Name.Contains(TEXT("speed")) || Name.Contains(TEXT("rate")) || Name.Contains(TEXT("time")))
	{
		return TEXT("float");
	}
	if (Name.StartsWith(TEXT("is")) || Name.StartsWith(TEXT("has")) || Name.StartsWith(TEXT("can")) || Name.StartsWith(TEXT("should")) ||
		Name.Contains(TEXT("enabled")) || Name.Contains(TEXT("active")) || Name.Contains(TEXT("valid")))
	{
		return TEXT("bool");
	}
	if (Name.Contains(TEXT("count")) || Name.Contains(TEXT("index")) || Name.Contains(TEXT("num")) || Name.Contains(TEXT("score")))
	{
		return TEXT("int");
	}
	if (Name.Contains(TEXT("name")))
	{
		return TEXT("name");
	}

	return TEXT("");
}

static FString InferSimpleTypeRefFromVarName(const FString& InVarName)
{
	const FString Name = InVarName.TrimStartAndEnd().ToLower();
	if (Name.Contains(TEXT("class")))
	{
		return TEXT("class");
	}
	if (Name.Contains(TEXT("ref")) || Name.Contains(TEXT("reference")) || Name.Contains(TEXT("instance")) || Name.Contains(TEXT("widget")))
	{
		return TEXT("object");
	}
	return TEXT("");
}

static FString InferSimpleTypeFromDefaultLiteral(const FString& InLiteral)
{
	FString S = InLiteral.TrimStartAndEnd();
	if (S.IsEmpty())
	{
		return TEXT("");
	}

	if (S.Equals(TEXT("true"), ESearchCase::IgnoreCase) || S.Equals(TEXT("false"), ESearchCase::IgnoreCase))
	{
		return TEXT("bool");
	}
	if (S.Len() >= 2 && ((S[0] == TEXT('"') && S[S.Len() - 1] == TEXT('"')) || (S[0] == TEXT('\'') && S[S.Len() - 1] == TEXT('\''))))
	{
		return TEXT("string");
	}

	bool bHasDigit = false;
	bool bHasDot = false;
	bool bNumeric = true;
	for (int32 Index = 0; Index < S.Len(); ++Index)
	{
		const TCHAR C = S[Index];
		if (C >= TEXT('0') && C <= TEXT('9'))
		{
			bHasDigit = true;
			continue;
		}
		if ((C == TEXT('-') || C == TEXT('+')) && Index == 0)
		{
			continue;
		}
		if (C == TEXT('.') && !bHasDot)
		{
			bHasDot = true;
			continue;
		}

		bNumeric = false;
		break;
	}

	if (bNumeric && bHasDigit)
	{
		return bHasDot ? TEXT("float") : TEXT("int");
	}

	return TEXT("");
}

static void CanonicalizeCreateMemberVariableType(const TSharedPtr<FJsonObject>& StepObj)
{
	if (!StepObj.IsValid())
	{
		return;
	}

	const TSharedPtr<FJsonObject>* ExistingTypeObj = nullptr;
	if (StepObj->TryGetObjectField(TEXT("varType"), ExistingTypeObj) && ExistingTypeObj && ExistingTypeObj->IsValid())
	{
		FString ExistingType;
		if ((*ExistingTypeObj)->TryGetStringField(TEXT("type"), ExistingType) && !ExistingType.TrimStartAndEnd().IsEmpty())
		{
			return;
		}
	}

	FString TypeText;
	FString RefText;
	FString Candidate;
	if (StepObj->TryGetStringField(TEXT("varType"), Candidate))
	{
		TypeText = NormalizeSimpleTypeName(Candidate);
	}
	if (TypeText.IsEmpty() && StepObj->TryGetStringField(TEXT("nodeType"), Candidate))
	{
		TypeText = NormalizeSimpleTypeName(Candidate);
	}
	if (TypeText.IsEmpty() && StepObj->TryGetStringField(TEXT("type"), Candidate))
	{
		TypeText = NormalizeSimpleTypeName(Candidate);
	}
	if (TypeText.IsEmpty())
	{
		FString VarName;
		ParseVarNameAliases(StepObj, VarName);
		TypeText = InferSimpleTypeFromVarName(VarName);
		RefText = InferSimpleTypeRefFromVarName(VarName);
	}
	if (TypeText.IsEmpty() && (StepObj->TryGetStringField(TEXT("defaultValue"), Candidate) || StepObj->TryGetStringField(TEXT("value"), Candidate)))
	{
		TypeText = InferSimpleTypeFromDefaultLiteral(Candidate);
	}
	if (TypeText.IsEmpty())
	{
		return;
	}

	TSharedRef<FJsonObject> TypeObj = MakeShared<FJsonObject>();
	TypeObj->SetStringField(TEXT("type"), TypeText);
	if (!RefText.IsEmpty())
	{
		TypeObj->SetStringField(TEXT("ref"), RefText);
	}
	StepObj->SetObjectField(TEXT("varType"), TypeObj);
}

static FString NormalizeFunctionGraphKind(FString In)
{
	FString K = In.TrimStartAndEnd();
	K.ReplaceInline(TEXT(" "), TEXT(""));
	K.ReplaceInline(TEXT("_"), TEXT(""));
	const FString Lower = K.ToLower();
	if (Lower.Equals(TEXT("callable")) || Lower.Equals(TEXT("function")))
	{
		return TEXT("Callable");
	}
	if (Lower.Equals(TEXT("pure")) || Lower.Equals(TEXT("purefunction")))
	{
		return TEXT("Pure");
	}
	if (Lower.Equals(TEXT("event")) || Lower.Equals(TEXT("customevent")) || Lower.Equals(TEXT("custom_event")))
	{
		return TEXT("Event");
	}
	if (Lower.Equals(TEXT("macro")))
	{
		return TEXT("Macro");
	}
	return TEXT("");
}

static void CanonicalizeCreateFunctionGraphFields(const TSharedPtr<FJsonObject>& StepObj)
{
	if (!StepObj.IsValid())
	{
		return;
	}

	FString Name;
	if (!StepObj->TryGetStringField(TEXT("name"), Name) || Name.TrimStartAndEnd().IsEmpty())
	{
		FString Candidate;
		if (StepObj->TryGetStringField(TEXT("functionName"), Candidate) ||
			StepObj->TryGetStringField(TEXT("graphName"), Candidate) ||
			StepObj->TryGetStringField(TEXT("function"), Candidate) ||
			StepObj->TryGetStringField(TEXT("eventName"), Candidate) ||
			StepObj->TryGetStringField(TEXT("macroName"), Candidate))
		{
			if (!Candidate.TrimStartAndEnd().IsEmpty())
			{
				StepObj->SetStringField(TEXT("name"), Candidate.TrimStartAndEnd());
			}
		}
	}

	FString Kind;
	if (!StepObj->TryGetStringField(TEXT("kind"), Kind) || Kind.TrimStartAndEnd().IsEmpty())
	{
		FString Candidate;
		if (StepObj->TryGetStringField(TEXT("graphKind"), Candidate) ||
			StepObj->TryGetStringField(TEXT("functionKind"), Candidate) ||
			StepObj->TryGetStringField(TEXT("type"), Candidate))
		{
			Kind = NormalizeFunctionGraphKind(Candidate);
		}

		bool bPure = false;
		if (Kind.IsEmpty() && TryGetBoolLoose(StepObj, TEXT("pure"), bPure) && bPure)
		{
			Kind = TEXT("Pure");
		}
		if (Kind.IsEmpty() && StepObj->HasField(TEXT("eventName")))
		{
			Kind = TEXT("Event");
		}
		if (Kind.IsEmpty() && StepObj->HasField(TEXT("macroName")))
		{
			Kind = TEXT("Macro");
		}
		if (Kind.IsEmpty())
		{
			Kind = TEXT("Callable");
		}

		StepObj->SetStringField(TEXT("kind"), Kind);
	}
	else
	{
		const FString Normalized = NormalizeFunctionGraphKind(Kind);
		if (!Normalized.IsEmpty() && !Normalized.Equals(Kind))
		{
			StepObj->SetStringField(TEXT("kind"), Normalized);
		}
	}
}

static EBlueprintDslActionType InferActionTypeFromFields(const TSharedPtr<FJsonObject>& StepObj)
{
	if (!StepObj.IsValid())
	{
		return EBlueprintDslActionType::Unknown;
	}

	// 连接线特征
	if (StepObj->HasField(TEXT("fromNodeId")) || StepObj->HasField(TEXT("fromPin")) || StepObj->HasField(TEXT("toNodeId")) || StepObj->HasField(TEXT("toPin")) ||
		StepObj->HasField(TEXT("fromNode")) || StepObj->HasField(TEXT("from")) || StepObj->HasField(TEXT("toNode")) || StepObj->HasField(TEXT("to")))
	{
		return EBlueprintDslActionType::ConnectPins;
	}
	if ((StepObj->HasField(TEXT("varName")) || StepObj->HasField(TEXT("variableName"))) &&
		(StepObj->HasField(TEXT("varType")) || StepObj->HasField(TEXT("nodeType")) || StepObj->HasField(TEXT("type")) ||
			(StepObj->HasField(TEXT("defaultValue")) && !StepObj->HasField(TEXT("pinName")) && !StepObj->HasField(TEXT("nodeId")))))
	{
		return EBlueprintDslActionType::CreateMemberVariable;
	}
	// 默认值特征
	if (StepObj->HasField(TEXT("pinName")) || StepObj->HasField(TEXT("defaultValue")))
	{
		return EBlueprintDslActionType::SetPinDefault;
	}
	// 注释特征
	if (StepObj->HasField(TEXT("commentText")) || StepObj->HasField(TEXT("attachToNodeIds")))
	{
		return EBlueprintDslActionType::Comment;
	}
	if ((StepObj->HasField(TEXT("name")) || StepObj->HasField(TEXT("functionName")) || StepObj->HasField(TEXT("graphName")) ||
		StepObj->HasField(TEXT("eventName")) || StepObj->HasField(TEXT("macroName"))) &&
		(StepObj->HasField(TEXT("kind")) || StepObj->HasField(TEXT("graphKind")) || StepObj->HasField(TEXT("functionKind")) ||
			StepObj->HasField(TEXT("bodySteps")) || StepObj->HasField(TEXT("params")) || StepObj->HasField(TEXT("returns"))))
	{
		return EBlueprintDslActionType::CreateFunctionGraph;
	}
	// 函数/宏创建特征
	if (StepObj->HasField(TEXT("kind")) && (StepObj->HasField(TEXT("name")) || StepObj->HasField(TEXT("params")) || StepObj->HasField(TEXT("returns"))))
	{
		return EBlueprintDslActionType::CreateFunctionGraph;
	}
	// 类变量创建特征
	if (StepObj->HasField(TEXT("varType")) && (StepObj->HasField(TEXT("varName")) || StepObj->HasField(TEXT("variableName"))))
	{
		return EBlueprintDslActionType::CreateMemberVariable;
	}
	if ((StepObj->HasField(TEXT("varName")) || StepObj->HasField(TEXT("variableName"))) &&
		(StepObj->HasField(TEXT("nodeType")) || StepObj->HasField(TEXT("type")) || StepObj->HasField(TEXT("defaultValue"))))
	{
		return EBlueprintDslActionType::CreateMemberVariable;
	}
	// 变量特征
	if (StepObj->HasField(TEXT("varName")) || StepObj->HasField(TEXT("variableName")) || StepObj->HasField(TEXT("valueFrom")) || StepObj->HasField(TEXT("valueFromNodeId")))
	{
		// 没有明确 action 时优先当 SetVariable（更常见）；如果明确写了 get=true 再处理
		return EBlueprintDslActionType::SetVariable;
	}
	// CreateNode 特征
	if (StepObj->HasField(TEXT("nodeType")) || StepObj->HasField(TEXT("functionName")) || StepObj->HasField(TEXT("targetClass")) || StepObj->HasField(TEXT("cases")))
	{
		return EBlueprintDslActionType::CreateNode;
	}
	return EBlueprintDslActionType::Unknown;
}

static void CanonicalizeStepObject(const TSharedPtr<FJsonObject>& StepObj)
{
	if (!StepObj.IsValid())
	{
		return;
	}

	// action：缺失时尽量推断；存在时做归一化
	FString Action;
	if (StepObj->TryGetStringField(TEXT("action"), Action))
	{
		const FString Norm = NormalizeActionString(Action);
		if (!Norm.Equals(Action))
		{
			StepObj->SetStringField(TEXT("action"), Norm);
		}
	}
	else
	{
		const EBlueprintDslActionType T = InferActionTypeFromFields(StepObj);
		switch (T)
		{
		case EBlueprintDslActionType::CreateNode: StepObj->SetStringField(TEXT("action"), TEXT("CreateNode")); break;
		case EBlueprintDslActionType::ConnectPins: StepObj->SetStringField(TEXT("action"), TEXT("ConnectPins")); break;
		case EBlueprintDslActionType::SetPinDefault: StepObj->SetStringField(TEXT("action"), TEXT("SetPinDefault")); break;
		case EBlueprintDslActionType::Comment: StepObj->SetStringField(TEXT("action"), TEXT("Comment")); break;
		case EBlueprintDslActionType::GetVariable: StepObj->SetStringField(TEXT("action"), TEXT("GetVariable")); break;
		case EBlueprintDslActionType::SetVariable: StepObj->SetStringField(TEXT("action"), TEXT("SetVariable")); break;
		case EBlueprintDslActionType::CreateMemberVariable: StepObj->SetStringField(TEXT("action"), TEXT("CreateMemberVariable")); break;
		case EBlueprintDslActionType::CreateFunctionGraph: StepObj->SetStringField(TEXT("action"), TEXT("CreateFunctionGraph")); break;
		default: break;
		}
	}

	if (StepObj->HasField(TEXT("action")))
	{
		FString A;
		StepObj->TryGetStringField(TEXT("action"), A);
		if (A.Equals(TEXT("CreateMemberVariable"), ESearchCase::IgnoreCase))
		{
			CanonicalizeCreateMemberVariableType(StepObj);
		}
		else if (A.Equals(TEXT("CreateFunctionGraph"), ESearchCase::IgnoreCase))
		{
			CanonicalizeCreateFunctionGraphFields(StepObj);
		}
	}

	// requiresConfirmation：允许别名 requires_confirmation / requireConfirmation
	if (!StepObj->HasField(TEXT("requiresConfirmation")))
	{
		bool B = false;
		if (TryGetBoolLoose(StepObj, TEXT("requires_confirmation"), B) || TryGetBoolLoose(StepObj, TEXT("requireConfirmation"), B))
		{
			StepObj->SetBoolField(TEXT("requiresConfirmation"), B);
		}
	}

	// targetGraph：允许 graph / target_graph
	if (!StepObj->HasField(TEXT("targetGraph")))
	{
		FString G;
		if (StepObj->TryGetStringField(TEXT("graph"), G) || StepObj->TryGetStringField(TEXT("target_graph"), G))
		{
			if (!G.IsEmpty())
			{
				StepObj->SetStringField(TEXT("targetGraph"), G);
			}
		}
	}

	// ConnectPins：允许 fromNode/fromPin/toNode/toPin
	if (StepObj->HasField(TEXT("action")))
	{
		FString A;
		StepObj->TryGetStringField(TEXT("action"), A);
		if (A.Equals(TEXT("ConnectPins"), ESearchCase::IgnoreCase))
		{
			FString S;
			if (!StepObj->HasField(TEXT("fromNodeId")) && StepObj->TryGetStringField(TEXT("fromNode"), S)) StepObj->SetStringField(TEXT("fromNodeId"), S);
			if (!StepObj->HasField(TEXT("toNodeId")) && StepObj->TryGetStringField(TEXT("toNode"), S)) StepObj->SetStringField(TEXT("toNodeId"), S);
			if (!StepObj->HasField(TEXT("fromPin")) && StepObj->TryGetStringField(TEXT("from_pin"), S)) StepObj->SetStringField(TEXT("fromPin"), S);
			if (!StepObj->HasField(TEXT("toPin")) && StepObj->TryGetStringField(TEXT("to_pin"), S)) StepObj->SetStringField(TEXT("toPin"), S);
		}
	}
}

static bool TryGetStepsArrayLoose(const TSharedPtr<FJsonObject>& RootObj, const TArray<TSharedPtr<FJsonValue>>*& OutStepsArray)
{
	OutStepsArray = nullptr;
	if (!RootObj.IsValid())
	{
		return false;
	}

	// 常见 key 别名
	static const TCHAR* Keys[] = { TEXT("steps"), TEXT("Steps"), TEXT("actions"), TEXT("Actions"), TEXT("dslSteps"), TEXT("dsl_steps") };
	for (const TCHAR* K : Keys)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (RootObj->TryGetArrayField(K, Arr) && Arr)
		{
			OutStepsArray = Arr;
			return true;
		}
	}

	// steps 是单个对象：包装成数组
	const TSharedPtr<FJsonObject>* StepObj = nullptr;
	if (RootObj->TryGetObjectField(TEXT("step"), StepObj) && StepObj && StepObj->IsValid())
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueObject>(*StepObj));
		RootObj->SetArrayField(TEXT("steps"), Arr);
		return RootObj->TryGetArrayField(TEXT("steps"), OutStepsArray) && OutStepsArray;
	}

	// steps 被当作字符串：尝试解析内部 JSON（对象或数组）
	FString StepsAsString;
	if (RootObj->TryGetStringField(TEXT("steps"), StepsAsString) && !StepsAsString.TrimStartAndEnd().IsEmpty())
	{
		TSharedPtr<FJsonObject> InnerObj;
		if (NormalizeAndDeserializeDslRoot(StepsAsString, InnerObj) && InnerObj.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* InnerArr = nullptr;
			if (InnerObj->TryGetArrayField(TEXT("steps"), InnerArr) && InnerArr)
			{
				RootObj->SetArrayField(TEXT("steps"), *InnerArr);
				return RootObj->TryGetArrayField(TEXT("steps"), OutStepsArray) && OutStepsArray;
			}
		}
	}

	return false;
}

/** 模型常把变量名写在 variableName / name 等字段，这里与 varName 一并兼容。 */
static void ParseVarNameAliases(const TSharedPtr<FJsonObject>& StepObj, FString& OutVarName)
{
	OutVarName.Empty();
	if (!StepObj.IsValid())
	{
		return;
	}
	if (TryGetString(StepObj, TEXT("varName"), OutVarName) && !OutVarName.IsEmpty())
	{
		return;
	}
	if (TryGetString(StepObj, TEXT("variableName"), OutVarName) && !OutVarName.IsEmpty())
	{
		return;
	}
	if (TryGetString(StepObj, TEXT("variable"), OutVarName) && !OutVarName.IsEmpty())
	{
		return;
	}
	TryGetString(StepObj, TEXT("name"), OutVarName);
}

static void ParseOptionalValueFromObject(const TSharedPtr<FJsonObject>& StepObj, FBlueprintDslActionStep& Step)
{
	if (!StepObj.IsValid())
	{
		return;
	}
	const TSharedPtr<FJsonObject>* ValueFromObj = nullptr;
	if (StepObj->TryGetObjectField(TEXT("valueFrom"), ValueFromObj) && ValueFromObj && ValueFromObj->IsValid())
	{
		(*ValueFromObj)->TryGetStringField(TEXT("nodeId"), Step.ValueFromNodeId);
		(*ValueFromObj)->TryGetStringField(TEXT("pin"), Step.ValueFromPin);
	}
	else
	{
		StepObj->TryGetStringField(TEXT("valueFromNodeId"), Step.ValueFromNodeId);
		StepObj->TryGetStringField(TEXT("valueFromPin"), Step.ValueFromPin);
	}
}

bool ParseDslStepsFromJson(const FString& InContent, TArray<FBlueprintDslActionStep>& OutSteps, FString& OutError)
{
	OutSteps.Reset();
	OutError.Empty();

	if (InContent.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("DSL 输出为空。");
		return false;
	}

	TSharedPtr<FJsonObject> RootObj;
	if (!NormalizeAndDeserializeDslRoot(InContent, RootObj) || !RootObj.IsValid())
	{
		OutError = TEXT("无法解析 DSL 输出为 JSON 对象。");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* StepsArray = nullptr;
	const UBlueprintAIAssistantSettings* Settings = GetDefault<UBlueprintAIAssistantSettings>();
	const bool bAutoSanitize = !Settings || Settings->bEnableDslSchemaAutoSanitize;

	if (bAutoSanitize)
	{
		if (!TryGetStepsArrayLoose(RootObj, StepsArray) || !StepsArray)
		{
			OutError = TEXT("DSL JSON 缺少 steps 数组字段（或字段类型不正确）。");
			return false;
		}
	}
	else
	{
		if (!RootObj->TryGetArrayField(TEXT("steps"), StepsArray) || !StepsArray)
		{
			OutError = TEXT("DSL JSON 缺少 steps 数组字段。");
			return false;
		}
	}

	int32 Version = 1;
	RootObj->TryGetNumberField(TEXT("version"), Version);
	if (Version <= 0) Version = 1;

	for (int32 i = 0; i < StepsArray->Num(); ++i)
	{
		const TSharedPtr<FJsonValue>& StepVal = (*StepsArray)[i];
		const TSharedPtr<FJsonObject>* StepObjPtr = nullptr;
		if (!StepVal.IsValid() || !StepVal->TryGetObject(StepObjPtr) || !StepObjPtr || !StepObjPtr->IsValid())
		{
			OutError = FString::Printf(TEXT("steps[%d] 不是合法对象。"), i);
			return false;
		}

		const TSharedPtr<FJsonObject> StepObj = *StepObjPtr;
		if (bAutoSanitize)
		{
			CanonicalizeStepObject(StepObj);
		}

		FString Action;
		if (!TryGetString(StepObj, TEXT("action"), Action))
		{
			OutError = FString::Printf(TEXT("steps[%d] 缺少 action 字段。"), i);
			return false;
		}
		if (bAutoSanitize)
		{
			Action = NormalizeActionString(Action);
		}

		FBlueprintDslActionStep Step;
		Step.Version = Version;
		Step.ActionType = BlueprintDslActionTypeFromString(Action);
		StepObj->TryGetStringField(TEXT("description"), Step.Description);
		StepObj->TryGetStringField(TEXT("stepId"), Step.StepId);
		TryGetBoolLoose(StepObj, TEXT("requiresConfirmation"), Step.bRequiresConfirmation);
		StepObj->TryGetStringField(TEXT("targetGraph"), Step.TargetGraph);
		StepObj->TryGetStringField(TEXT("targetBlueprint"), Step.TargetBlueprintAssetPath);

		if (Step.ActionType == EBlueprintDslActionType::CreateNode)
		{
			StepObj->TryGetStringField(TEXT("nodeId"), Step.NodeId);
			StepObj->TryGetStringField(TEXT("nodeType"), Step.NodeType);
			StepObj->TryGetStringField(TEXT("functionName"), Step.FunctionName);
			StepObj->TryGetStringField(TEXT("targetClass"), Step.TargetClass);
			// GetVariable / SetVariable：schema 写在同一对象上，必须与独立 action 一致地解析（否则 varName 永远为空）
			ParseVarNameAliases(StepObj, Step.VarName);
			ParseOptionalValueFromObject(StepObj, Step);
			StepObj->TryGetStringField(TEXT("defaultValue"), Step.DefaultValue);
			if (Step.DefaultValue.IsEmpty())
			{
				// 部分模型用 value 表示 Set 的字面量
				StepObj->TryGetStringField(TEXT("value"), Step.DefaultValue);
			}
			{
				const TArray<TSharedPtr<FJsonValue>>* CasesArray = nullptr;
				if (StepObj->TryGetArrayField(TEXT("cases"), CasesArray) && CasesArray)
				{
					for (const TSharedPtr<FJsonValue>& V : *CasesArray)
					{
						if (!V.IsValid()) continue;
						const FString S = V->AsString();
						if (!S.IsEmpty())
						{
							Step.CaseValues.Add(S);
						}
					}
				}
			}

			if (Step.NodeId.IsEmpty() || Step.NodeType.IsEmpty())
			{
				OutError = FString::Printf(TEXT("steps[%d] CreateNode 需要 nodeId 与 nodeType。"), i);
				return false;
			}
		}
		else if (Step.ActionType == EBlueprintDslActionType::ConnectPins)
		{
			StepObj->TryGetStringField(TEXT("fromNodeId"), Step.FromNodeId);
			StepObj->TryGetStringField(TEXT("fromPin"), Step.FromPin);
			StepObj->TryGetStringField(TEXT("toNodeId"), Step.ToNodeId);
			StepObj->TryGetStringField(TEXT("toPin"), Step.ToPin);

			if (Step.FromNodeId.IsEmpty() || Step.FromPin.IsEmpty() || Step.ToNodeId.IsEmpty() || Step.ToPin.IsEmpty())
			{
				OutError = FString::Printf(TEXT("steps[%d] ConnectPins 需要 fromNodeId/fromPin/toNodeId/toPin。"), i);
				return false;
			}
		}
		else if (Step.ActionType == EBlueprintDslActionType::SetPinDefault)
		{
			StepObj->TryGetStringField(TEXT("nodeId"), Step.NodeId);
			StepObj->TryGetStringField(TEXT("pinName"), Step.PinName);
			StepObj->TryGetStringField(TEXT("defaultValue"), Step.DefaultValue);

			if (Step.NodeId.IsEmpty() || Step.PinName.IsEmpty())
			{
				OutError = FString::Printf(TEXT("steps[%d] SetPinDefault 需要 nodeId 与 pinName。"), i);
				return false;
			}
		}
		else if (Step.ActionType == EBlueprintDslActionType::Comment)
		{
			StepObj->TryGetStringField(TEXT("commentText"), Step.CommentText);

			const TArray<TSharedPtr<FJsonValue>>* AttachArray = nullptr;
			if (StepObj->TryGetArrayField(TEXT("attachToNodeIds"), AttachArray) && AttachArray)
			{
				for (const TSharedPtr<FJsonValue>& V : *AttachArray)
				{
					if (V.IsValid())
					{
						const FString S = V->AsString();
						if (!S.IsEmpty())
						{
							Step.AttachToNodeIds.Add(S);
						}
					}
				}
			}

			if (Step.CommentText.IsEmpty())
			{
				OutError = FString::Printf(TEXT("steps[%d] Comment 需要 commentText。"), i);
				return false;
			}
		}
		else if (Step.ActionType == EBlueprintDslActionType::GetVariable)
		{
			StepObj->TryGetStringField(TEXT("nodeId"), Step.NodeId);
			ParseVarNameAliases(StepObj, Step.VarName);
			if (Step.NodeId.IsEmpty() || Step.VarName.IsEmpty())
			{
				OutError = FString::Printf(TEXT("steps[%d] GetVariable 需要 nodeId 与 varName。"), i);
				return false;
			}
		}
		else if (Step.ActionType == EBlueprintDslActionType::SetVariable)
		{
			StepObj->TryGetStringField(TEXT("nodeId"), Step.NodeId);
			ParseVarNameAliases(StepObj, Step.VarName);
			if (Step.NodeId.IsEmpty() || Step.VarName.IsEmpty())
			{
				OutError = FString::Printf(TEXT("steps[%d] SetVariable 需要 nodeId 与 varName。"), i);
				return false;
			}

			ParseOptionalValueFromObject(StepObj, Step);
			StepObj->TryGetStringField(TEXT("defaultValue"), Step.DefaultValue);
			if (Step.DefaultValue.IsEmpty())
			{
				StepObj->TryGetStringField(TEXT("value"), Step.DefaultValue);
			}
		}
		else if (Step.ActionType == EBlueprintDslActionType::CreateMemberVariable)
		{
			ParseVarNameAliases(StepObj, Step.VarName);
			const TSharedPtr<FJsonObject>* TypeObj = nullptr;
			if (StepObj->TryGetObjectField(TEXT("varType"), TypeObj) && TypeObj && TypeObj->IsValid())
			{
				ParseSimpleTypeObj(*TypeObj, Step.MemberVarType);
			}
			StepObj->TryGetStringField(TEXT("exposure"), Step.MemberVarExposure);
			if (Step.VarName.IsEmpty() || Step.MemberVarType.Type.TrimStartAndEnd().IsEmpty())
			{
				OutError = FString::Printf(TEXT("steps[%d] CreateMemberVariable 需要 varName 与 varType.type。"), i);
				return false;
			}
		}
		else if (Step.ActionType == EBlueprintDslActionType::CreateFunctionGraph)
		{
			StepObj->TryGetStringField(TEXT("name"), Step.FunctionGraphName);
			StepObj->TryGetStringField(TEXT("kind"), Step.FunctionGraphKind);
			ParsePinsArray(StepObj, TEXT("params"), Step.FunctionParams);
			ParsePinsArray(StepObj, TEXT("returns"), Step.FunctionReturns);

			const TArray<TSharedPtr<FJsonValue>>* BodyArr = nullptr;
			if (StepObj->TryGetArrayField(TEXT("bodySteps"), BodyArr) && BodyArr)
			{
				for (const auto& BV : *BodyArr)
				{
					const TSharedPtr<FJsonObject>* BO = nullptr;
					if (!BV.IsValid() || !BV->TryGetObject(BO) || !BO || !BO->IsValid())
					{
						continue;
					}
					TSharedRef<FJsonObject> R2 = MakeShared<FJsonObject>();
					R2->SetNumberField(TEXT("version"), Version);
					TArray<TSharedPtr<FJsonValue>> A2;
					A2.Add(MakeShared<FJsonValueObject>(*BO));
					R2->SetArrayField(TEXT("steps"), A2);
					FString Tmp;
					const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W2 = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Tmp);
					FJsonSerializer::Serialize(R2, W2);
					TArray<FBlueprintDslActionStep> Parsed;
					FString Err2;
					if (ParseDslStepsFromJson(Tmp, Parsed, Err2) && Parsed.Num() == 1)
					{
						Step.FunctionBodySteps.Add(Parsed[0]);
					}
				}
			}

			if (Step.FunctionGraphName.TrimStartAndEnd().IsEmpty() || Step.FunctionGraphKind.TrimStartAndEnd().IsEmpty())
			{
				OutError = FString::Printf(TEXT("steps[%d] CreateFunctionGraph 需要 name 与 kind。"), i);
				return false;
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("steps[%d] action=%s 不受支持。"), i, *Action);
			return false;
		}

		OutSteps.Add(MoveTemp(Step));
	}

	if (OutSteps.Num() == 0)
	{
		OutError = TEXT("steps 为空。");
		return false;
	}

	return true;
}
