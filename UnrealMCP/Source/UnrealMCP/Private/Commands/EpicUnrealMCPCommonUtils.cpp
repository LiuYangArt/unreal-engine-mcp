#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_InputAction.h"
#include "K2Node_Self.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Selection.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabase.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

namespace
{
UClass* FindLoadedClassByName(const FString& Candidate)
{
    const FString TrimmedCandidate = Candidate.TrimStartAndEnd();
    if (TrimmedCandidate.IsEmpty())
    {
        return nullptr;
    }

    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* LoadedClass = *It;
        if (!LoadedClass)
        {
            continue;
        }

        if (LoadedClass->GetName().Equals(TrimmedCandidate, ESearchCase::IgnoreCase) ||
            LoadedClass->GetPathName().Equals(TrimmedCandidate, ESearchCase::IgnoreCase) ||
            LoadedClass->GetClassPathName().ToString().Equals(TrimmedCandidate, ESearchCase::IgnoreCase))
        {
            return LoadedClass;
        }
    }

    return nullptr;
}

UObject* ResolveObjectReference(UClass* ExpectedClass, const FString& Input, FString& OutResolutionLog)
{
    const FString TrimmedInput = Input.TrimStartAndEnd();
    if (TrimmedInput.IsEmpty() || TrimmedInput.Equals(TEXT("None"), ESearchCase::IgnoreCase) || TrimmedInput.Equals(TEXT("null"), ESearchCase::IgnoreCase))
    {
        OutResolutionLog = TEXT("null");
        return nullptr;
    }

    auto IsCompatibleObject = [ExpectedClass](UObject* CandidateObject)
    {
        return CandidateObject && (!ExpectedClass || CandidateObject->IsA(ExpectedClass));
    };

    TArray<FString> Candidates;
    auto AddCandidate = [&Candidates](const FString& Candidate)
    {
        if (!Candidate.IsEmpty())
        {
            Candidates.AddUnique(Candidate);
        }
    };

    AddCandidate(TrimmedInput);
    if (TrimmedInput.StartsWith(TEXT("/")) && !TrimmedInput.Contains(TEXT(".")))
    {
        AddCandidate(TrimmedInput + TEXT(".") + FPaths::GetBaseFilename(TrimmedInput));
    }

    for (const FString& Candidate : Candidates)
    {
        if (UObject* LoadedObject = LoadObject<UObject>(nullptr, *Candidate))
        {
            if (IsCompatibleObject(LoadedObject))
            {
                OutResolutionLog = Candidate;
                return LoadedObject;
            }
        }

        if (Candidate.StartsWith(TEXT("/")))
        {
            if (UObject* Asset = UEditorAssetLibrary::LoadAsset(Candidate))
            {
                if (IsCompatibleObject(Asset))
                {
                    OutResolutionLog = Candidate;
                    return Asset;
                }
            }
        }

        if (UObject* FoundObject = FindObject<UObject>(nullptr, *Candidate))
        {
            if (IsCompatibleObject(FoundObject))
            {
                OutResolutionLog = Candidate;
                return FoundObject;
            }
        }
    }

    for (TObjectIterator<UObject> It; It; ++It)
    {
        UObject* LoadedObject = *It;
        if (!IsCompatibleObject(LoadedObject))
        {
            continue;
        }

        if (LoadedObject->GetName().Equals(TrimmedInput, ESearchCase::IgnoreCase) ||
            LoadedObject->GetPathName().Equals(TrimmedInput, ESearchCase::IgnoreCase))
        {
            OutResolutionLog = TrimmedInput;
            return LoadedObject;
        }
    }

    OutResolutionLog = FString::Join(Candidates, TEXT(", "));
    return nullptr;
}

UClass* ResolveClassReference(UClass* ExpectedBaseClass, const FString& Input, FString& OutResolutionLog)
{
    const FString TrimmedInput = Input.TrimStartAndEnd();
    if (TrimmedInput.IsEmpty() || TrimmedInput.Equals(TEXT("None"), ESearchCase::IgnoreCase) || TrimmedInput.Equals(TEXT("null"), ESearchCase::IgnoreCase))
    {
        OutResolutionLog = TEXT("null");
        return nullptr;
    }

    auto IsCompatibleClass = [ExpectedBaseClass](UClass* CandidateClass)
    {
        return CandidateClass && (!ExpectedBaseClass || CandidateClass->IsChildOf(ExpectedBaseClass));
    };

    auto TryBlueprintAsset = [&IsCompatibleClass, &OutResolutionLog](const FString& AssetPath) -> UClass*
    {
        if (UClass* BlueprintClass = UEditorAssetLibrary::LoadBlueprintClass(AssetPath))
        {
            if (IsCompatibleClass(BlueprintClass))
            {
                OutResolutionLog = AssetPath;
                return BlueprintClass;
            }
        }

        if (UBlueprint* BlueprintAsset = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AssetPath)))
        {
            if (IsCompatibleClass(BlueprintAsset->GeneratedClass))
            {
                OutResolutionLog = AssetPath;
                return BlueprintAsset->GeneratedClass;
            }
        }

        return nullptr;
    };

    TArray<FString> Candidates;
    auto AddCandidate = [&Candidates](const FString& Candidate)
    {
        if (!Candidate.IsEmpty())
        {
            Candidates.AddUnique(Candidate);
        }
    };

    AddCandidate(TrimmedInput);

    if (!TrimmedInput.EndsWith(TEXT("_C")))
    {
        AddCandidate(TrimmedInput + TEXT("_C"));
    }

    if (TrimmedInput.StartsWith(TEXT("/")) && !TrimmedInput.Contains(TEXT(".")))
    {
        const FString AssetName = FPaths::GetBaseFilename(TrimmedInput);
        AddCandidate(TrimmedInput + TEXT(".") + AssetName + TEXT("_C"));
        AddCandidate(TrimmedInput + TEXT("_C"));
    }
    else if (TrimmedInput.Contains(TEXT(".")) && !TrimmedInput.EndsWith(TEXT("_C")) && !TrimmedInput.StartsWith(TEXT("/Script/")))
    {
        FString LeftPart;
        FString RightPart;
        if (TrimmedInput.Split(TEXT("."), &LeftPart, &RightPart, ESearchCase::IgnoreCase, ESearchDir::FromStart))
        {
            AddCandidate(LeftPart + TEXT(".") + RightPart + TEXT("_C"));
        }
    }

    for (const FString& Candidate : Candidates)
    {
        if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *Candidate))
        {
            if (IsCompatibleClass(LoadedClass))
            {
                OutResolutionLog = Candidate;
                return LoadedClass;
            }
        }

        if (UClass* FoundClass = FindObject<UClass>(nullptr, *Candidate))
        {
            if (IsCompatibleClass(FoundClass))
            {
                OutResolutionLog = Candidate;
                return FoundClass;
            }
        }

        if (UClass* LoadedClass = FindLoadedClassByName(Candidate))
        {
            if (IsCompatibleClass(LoadedClass))
            {
                OutResolutionLog = Candidate;
                return LoadedClass;
            }
        }
    }

    if (TrimmedInput.StartsWith(TEXT("/")))
    {
        if (UClass* BlueprintClass = TryBlueprintAsset(TrimmedInput))
        {
            return BlueprintClass;
        }
    }
    else
    {
        FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        FARFilter Filter;
        Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
        Filter.PackagePaths.Add(TEXT("/Game"));
        Filter.PackagePaths.Add(TEXT("/Engine"));
        Filter.bRecursivePaths = true;

        TArray<FAssetData> BlueprintAssets;
        AssetRegistryModule.Get().GetAssets(Filter, BlueprintAssets);
        for (const FAssetData& AssetData : BlueprintAssets)
        {
            if (!AssetData.AssetName.ToString().Equals(TrimmedInput, ESearchCase::IgnoreCase))
            {
                continue;
            }

            if (UClass* BlueprintClass = TryBlueprintAsset(AssetData.PackageName.ToString()))
            {
                return BlueprintClass;
            }
        }
    }

    OutResolutionLog = FString::Join(Candidates, TEXT(", "));
    return nullptr;
}

bool TryReadVectorValue(const TSharedPtr<FJsonValue>& Value, FVector& OutVector)
{
    if (!Value.IsValid())
    {
        return false;
    }

    if (Value->Type == EJson::Array)
    {
        const TArray<TSharedPtr<FJsonValue>>& ArrayValue = Value->AsArray();
        if (ArrayValue.Num() >= 3)
        {
            OutVector.X = static_cast<float>(ArrayValue[0]->AsNumber());
            OutVector.Y = static_cast<float>(ArrayValue[1]->AsNumber());
            OutVector.Z = static_cast<float>(ArrayValue[2]->AsNumber());
            return true;
        }
    }
    else if (Value->Type == EJson::Object)
    {
        const TSharedPtr<FJsonObject> ObjectValue = Value->AsObject();
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
        if ((ObjectValue->TryGetNumberField(TEXT("x"), X) || ObjectValue->TryGetNumberField(TEXT("X"), X)) &&
            (ObjectValue->TryGetNumberField(TEXT("y"), Y) || ObjectValue->TryGetNumberField(TEXT("Y"), Y)) &&
            (ObjectValue->TryGetNumberField(TEXT("z"), Z) || ObjectValue->TryGetNumberField(TEXT("Z"), Z)))
        {
            OutVector = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
            return true;
        }
    }

    return false;
}

bool TryReadVector2DValue(const TSharedPtr<FJsonValue>& Value, FVector2D& OutVector)
{
    if (!Value.IsValid())
    {
        return false;
    }

    if (Value->Type == EJson::Array)
    {
        const TArray<TSharedPtr<FJsonValue>>& ArrayValue = Value->AsArray();
        if (ArrayValue.Num() >= 2)
        {
            OutVector.X = static_cast<float>(ArrayValue[0]->AsNumber());
            OutVector.Y = static_cast<float>(ArrayValue[1]->AsNumber());
            return true;
        }
    }
    else if (Value->Type == EJson::Object)
    {
        const TSharedPtr<FJsonObject> ObjectValue = Value->AsObject();
        double X = 0.0;
        double Y = 0.0;
        if ((ObjectValue->TryGetNumberField(TEXT("x"), X) || ObjectValue->TryGetNumberField(TEXT("X"), X)) &&
            (ObjectValue->TryGetNumberField(TEXT("y"), Y) || ObjectValue->TryGetNumberField(TEXT("Y"), Y)))
        {
            OutVector = FVector2D(static_cast<float>(X), static_cast<float>(Y));
            return true;
        }
    }

    return false;
}

bool TryReadRotatorValue(const TSharedPtr<FJsonValue>& Value, FRotator& OutRotator)
{
    if (!Value.IsValid())
    {
        return false;
    }

    if (Value->Type == EJson::Array)
    {
        const TArray<TSharedPtr<FJsonValue>>& ArrayValue = Value->AsArray();
        if (ArrayValue.Num() >= 3)
        {
            OutRotator.Pitch = static_cast<float>(ArrayValue[0]->AsNumber());
            OutRotator.Yaw = static_cast<float>(ArrayValue[1]->AsNumber());
            OutRotator.Roll = static_cast<float>(ArrayValue[2]->AsNumber());
            return true;
        }
    }
    else if (Value->Type == EJson::Object)
    {
        const TSharedPtr<FJsonObject> ObjectValue = Value->AsObject();
        double Pitch = 0.0;
        double Yaw = 0.0;
        double Roll = 0.0;
        if ((ObjectValue->TryGetNumberField(TEXT("pitch"), Pitch) || ObjectValue->TryGetNumberField(TEXT("Pitch"), Pitch)) &&
            (ObjectValue->TryGetNumberField(TEXT("yaw"), Yaw) || ObjectValue->TryGetNumberField(TEXT("Yaw"), Yaw)) &&
            (ObjectValue->TryGetNumberField(TEXT("roll"), Roll) || ObjectValue->TryGetNumberField(TEXT("Roll"), Roll)))
        {
            OutRotator = FRotator(static_cast<float>(Pitch), static_cast<float>(Yaw), static_cast<float>(Roll));
            return true;
        }
    }

    return false;
}
}
// JSON Utilities
TSharedPtr<FJsonObject> FEpicUnrealMCPCommonUtils::CreateErrorResponse(const FString& Message)
{
    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetBoolField(TEXT("success"), false);
    ResponseObject->SetStringField(TEXT("error"), Message);
    return ResponseObject;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPCommonUtils::CreateSuccessResponse(const TSharedPtr<FJsonObject>& Data)
{
    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetBoolField(TEXT("success"), true);
    
    if (Data.IsValid())
    {
        ResponseObject->SetObjectField(TEXT("data"), Data);
    }
    
    return ResponseObject;
}

void FEpicUnrealMCPCommonUtils::GetIntArrayFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, TArray<int32>& OutArray)
{
    OutArray.Reset();
    
    if (!JsonObject->HasField(FieldName))
    {
        return;
    }
    
    const TArray<TSharedPtr<FJsonValue>>* JsonArray;
    if (JsonObject->TryGetArrayField(FieldName, JsonArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *JsonArray)
        {
            OutArray.Add((int32)Value->AsNumber());
        }
    }
}

void FEpicUnrealMCPCommonUtils::GetFloatArrayFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, TArray<float>& OutArray)
{
    OutArray.Reset();
    
    if (!JsonObject->HasField(FieldName))
    {
        return;
    }
    
    const TArray<TSharedPtr<FJsonValue>>* JsonArray;
    if (JsonObject->TryGetArrayField(FieldName, JsonArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *JsonArray)
        {
            OutArray.Add((float)Value->AsNumber());
        }
    }
}

FVector2D FEpicUnrealMCPCommonUtils::GetVector2DFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName)
{
    FVector2D Result(0.0f, 0.0f);
    
    if (!JsonObject->HasField(FieldName))
    {
        return Result;
    }
    
    const TArray<TSharedPtr<FJsonValue>>* JsonArray;
    if (JsonObject->TryGetArrayField(FieldName, JsonArray) && JsonArray->Num() >= 2)
    {
        Result.X = (float)(*JsonArray)[0]->AsNumber();
        Result.Y = (float)(*JsonArray)[1]->AsNumber();
    }
    
    return Result;
}

FVector FEpicUnrealMCPCommonUtils::GetVectorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName)
{
    FVector Result(0.0f, 0.0f, 0.0f);
    
    if (!JsonObject->HasField(FieldName))
    {
        return Result;
    }
    
    const TArray<TSharedPtr<FJsonValue>>* JsonArray;
    if (JsonObject->TryGetArrayField(FieldName, JsonArray) && JsonArray->Num() >= 3)
    {
        Result.X = (float)(*JsonArray)[0]->AsNumber();
        Result.Y = (float)(*JsonArray)[1]->AsNumber();
        Result.Z = (float)(*JsonArray)[2]->AsNumber();
    }
    
    return Result;
}

FRotator FEpicUnrealMCPCommonUtils::GetRotatorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName)
{
    FRotator Result(0.0f, 0.0f, 0.0f);
    
    if (!JsonObject->HasField(FieldName))
    {
        return Result;
    }
    
    const TArray<TSharedPtr<FJsonValue>>* JsonArray;
    if (JsonObject->TryGetArrayField(FieldName, JsonArray) && JsonArray->Num() >= 3)
    {
        Result.Pitch = (float)(*JsonArray)[0]->AsNumber();
        Result.Yaw = (float)(*JsonArray)[1]->AsNumber();
        Result.Roll = (float)(*JsonArray)[2]->AsNumber();
    }
    
    return Result;
}

// Blueprint Utilities
UBlueprint* FEpicUnrealMCPCommonUtils::FindBlueprint(const FString& BlueprintName)
{
    return FindBlueprintByName(BlueprintName);
}

UBlueprint* FEpicUnrealMCPCommonUtils::FindBlueprintByName(const FString& BlueprintName)
{
    // The correct object path for a Blueprint asset is /Game/Path/AssetName.AssetName
    FString ObjectPath;

    // Check if BlueprintName is already a full path (starts with /)
    if (BlueprintName.StartsWith(TEXT("/")))
    {
        // It's already a full path, use it directly with the class suffix
        FString AssetName = FPaths::GetBaseFilename(BlueprintName);
        ObjectPath = FString::Printf(TEXT("%s.%s"), *BlueprintName, *AssetName);
    }
    else
    {
        // It's just a name, add the default /Game/Blueprints/ prefix
        ObjectPath = FString::Printf(TEXT("/Game/Blueprints/%s.%s"), *BlueprintName, *BlueprintName);
    }

    // First, try to load the object directly, as it's the fastest method.
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
    if (Blueprint)
    {
        return Blueprint;
    }

    // If direct loading fails, try to find the asset using the Asset Registry.
    // This is more robust for newly created assets.
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));

    if (AssetData.IsValid())
    {
        Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
        if (Blueprint)
        {
            return Blueprint;
        }
    }

    // Fallback for cases where the asset is in memory but not yet fully saved,
    // where it might be found via its package path.
    FString PackagePath = TEXT("/Game/Blueprints/") + BlueprintName;
    Blueprint = FindObject<UBlueprint>(nullptr, *PackagePath);

    if (!Blueprint)
    {
         UE_LOG(LogTemp, Error, TEXT("FindBlueprintByName: Failed to find or load blueprint: %s"), *BlueprintName);
    }

    return Blueprint;
}

UEdGraph* FEpicUnrealMCPCommonUtils::FindOrCreateEventGraph(UBlueprint* Blueprint)
{
    if (!Blueprint)
    {
        return nullptr;
    }
    
    // Try to find the event graph
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (Graph->GetName().Contains(TEXT("EventGraph")))
        {
            return Graph;
        }
    }
    
    // Create a new event graph if none exists
    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(TEXT("EventGraph")), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(Blueprint, NewGraph);
    return NewGraph;
}

// Blueprint node utilities
UK2Node_Event* FEpicUnrealMCPCommonUtils::CreateEventNode(UEdGraph* Graph, const FString& EventName, const FVector2D& Position)
{
    if (!Graph)
    {
        return nullptr;
    }
    
    UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
    if (!Blueprint)
    {
        return nullptr;
    }
    
    // Check for existing event node with this exact name
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
        if (EventNode && EventNode->EventReference.GetMemberName() == FName(*EventName))
        {
            UE_LOG(LogTemp, Display, TEXT("Using existing event node with name %s (ID: %s)"), 
                *EventName, *EventNode->NodeGuid.ToString());
            return EventNode;
        }
    }

    // No existing node found, create a new one
    UK2Node_Event* EventNode = nullptr;
    
    // Find the function to create the event
    UClass* BlueprintClass = Blueprint->GeneratedClass;
    UFunction* EventFunction = BlueprintClass->FindFunctionByName(FName(*EventName));
    
    if (EventFunction)
    {
        EventNode = NewObject<UK2Node_Event>(Graph);
        EventNode->EventReference.SetExternalMember(FName(*EventName), BlueprintClass);
        EventNode->NodePosX = Position.X;
        EventNode->NodePosY = Position.Y;
        Graph->AddNode(EventNode, true);
        EventNode->PostPlacedNewNode();
        EventNode->AllocateDefaultPins();
        UE_LOG(LogTemp, Display, TEXT("Created new event node with name %s (ID: %s)"), 
            *EventName, *EventNode->NodeGuid.ToString());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to find function for event name: %s"), *EventName);
    }
    
    return EventNode;
}

UK2Node_CallFunction* FEpicUnrealMCPCommonUtils::CreateFunctionCallNode(UEdGraph* Graph, UFunction* Function, const FVector2D& Position)
{
    if (!Graph || !Function)
    {
        return nullptr;
    }
    
    UK2Node_CallFunction* FunctionNode = NewObject<UK2Node_CallFunction>(Graph);
    FunctionNode->SetFromFunction(Function);
    FunctionNode->NodePosX = Position.X;
    FunctionNode->NodePosY = Position.Y;
    Graph->AddNode(FunctionNode, true);
    FunctionNode->CreateNewGuid();
    FunctionNode->PostPlacedNewNode();
    FunctionNode->AllocateDefaultPins();
    
    return FunctionNode;
}

UK2Node_VariableGet* FEpicUnrealMCPCommonUtils::CreateVariableGetNode(UEdGraph* Graph, UBlueprint* Blueprint, const FString& VariableName, const FVector2D& Position)
{
    if (!Graph || !Blueprint)
    {
        return nullptr;
    }
    
    UK2Node_VariableGet* VariableGetNode = NewObject<UK2Node_VariableGet>(Graph);
    
    FName VarName(*VariableName);
    FProperty* Property = FindFProperty<FProperty>(Blueprint->GeneratedClass, VarName);
    
    if (Property)
    {
        VariableGetNode->VariableReference.SetFromField<FProperty>(Property, false);
        VariableGetNode->NodePosX = Position.X;
        VariableGetNode->NodePosY = Position.Y;
        Graph->AddNode(VariableGetNode, true);
        VariableGetNode->PostPlacedNewNode();
        VariableGetNode->AllocateDefaultPins();
        
        return VariableGetNode;
    }
    
    return nullptr;
}

UK2Node_VariableSet* FEpicUnrealMCPCommonUtils::CreateVariableSetNode(UEdGraph* Graph, UBlueprint* Blueprint, const FString& VariableName, const FVector2D& Position)
{
    if (!Graph || !Blueprint)
    {
        return nullptr;
    }
    
    UK2Node_VariableSet* VariableSetNode = NewObject<UK2Node_VariableSet>(Graph);
    
    FName VarName(*VariableName);
    FProperty* Property = FindFProperty<FProperty>(Blueprint->GeneratedClass, VarName);
    
    if (Property)
    {
        VariableSetNode->VariableReference.SetFromField<FProperty>(Property, false);
        VariableSetNode->NodePosX = Position.X;
        VariableSetNode->NodePosY = Position.Y;
        Graph->AddNode(VariableSetNode, true);
        VariableSetNode->PostPlacedNewNode();
        VariableSetNode->AllocateDefaultPins();
        
        return VariableSetNode;
    }
    
    return nullptr;
}

UK2Node_InputAction* FEpicUnrealMCPCommonUtils::CreateInputActionNode(UEdGraph* Graph, const FString& ActionName, const FVector2D& Position)
{
    if (!Graph)
    {
        return nullptr;
    }
    
    UK2Node_InputAction* InputActionNode = NewObject<UK2Node_InputAction>(Graph);
    InputActionNode->InputActionName = FName(*ActionName);
    InputActionNode->NodePosX = Position.X;
    InputActionNode->NodePosY = Position.Y;
    Graph->AddNode(InputActionNode, true);
    InputActionNode->CreateNewGuid();
    InputActionNode->PostPlacedNewNode();
    InputActionNode->AllocateDefaultPins();
    
    return InputActionNode;
}

UK2Node_Self* FEpicUnrealMCPCommonUtils::CreateSelfReferenceNode(UEdGraph* Graph, const FVector2D& Position)
{
    if (!Graph)
    {
        return nullptr;
    }
    
    UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(Graph);
    SelfNode->NodePosX = Position.X;
    SelfNode->NodePosY = Position.Y;
    Graph->AddNode(SelfNode, true);
    SelfNode->CreateNewGuid();
    SelfNode->PostPlacedNewNode();
    SelfNode->AllocateDefaultPins();
    
    return SelfNode;
}

bool FEpicUnrealMCPCommonUtils::ConnectGraphNodes(UEdGraph* Graph, UEdGraphNode* SourceNode, const FString& SourcePinName, 
                                           UEdGraphNode* TargetNode, const FString& TargetPinName)
{
    if (!Graph || !SourceNode || !TargetNode)
    {
        return false;
    }
    
    UEdGraphPin* SourcePin = FindPin(SourceNode, SourcePinName, EGPD_Output);
    UEdGraphPin* TargetPin = FindPin(TargetNode, TargetPinName, EGPD_Input);
    
    if (SourcePin && TargetPin)
    {
        SourcePin->MakeLinkTo(TargetPin);
        return true;
    }
    
    return false;
}

UEdGraphPin* FEpicUnrealMCPCommonUtils::FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction)
{
    if (!Node)
    {
        return nullptr;
    }
    
    // Log all pins for debugging
    UE_LOG(LogTemp, Display, TEXT("FindPin: Looking for pin '%s' (Direction: %d) in node '%s'"), 
           *PinName, (int32)Direction, *Node->GetName());
    
    for (UEdGraphPin* Pin : Node->Pins)
    {
        UE_LOG(LogTemp, Display, TEXT("  - Available pin: '%s', Direction: %d, Category: %s"), 
               *Pin->PinName.ToString(), (int32)Pin->Direction, *Pin->PinType.PinCategory.ToString());
    }
    
    // First try exact match
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->PinName.ToString() == PinName && (Direction == EGPD_MAX || Pin->Direction == Direction))
        {
            UE_LOG(LogTemp, Display, TEXT("  - Found exact matching pin: '%s'"), *Pin->PinName.ToString());
            return Pin;
        }
    }
    
    // If no exact match and we're looking for a component reference, try case-insensitive match
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase) && 
            (Direction == EGPD_MAX || Pin->Direction == Direction))
        {
            UE_LOG(LogTemp, Display, TEXT("  - Found case-insensitive matching pin: '%s'"), *Pin->PinName.ToString());
            return Pin;
        }
    }
    
    // If we're looking for a component output and didn't find it by name, try to find the first data output pin
    if (Direction == EGPD_Output && Cast<UK2Node_VariableGet>(Node) != nullptr)
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
            {
                UE_LOG(LogTemp, Display, TEXT("  - Found fallback data output pin: '%s'"), *Pin->PinName.ToString());
                return Pin;
            }
        }
    }
    
    UE_LOG(LogTemp, Warning, TEXT("  - No matching pin found for '%s'"), *PinName);
    return nullptr;
}

// Actor utilities
TSharedPtr<FJsonValue> FEpicUnrealMCPCommonUtils::ActorToJson(AActor* Actor)
{
    if (!Actor)
    {
        return MakeShared<FJsonValueNull>();
    }
    
    TSharedPtr<FJsonObject> ActorObject = MakeShared<FJsonObject>();
    ActorObject->SetStringField(TEXT("name"), Actor->GetName());
    ActorObject->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
    
    FVector Location = Actor->GetActorLocation();
    TArray<TSharedPtr<FJsonValue>> LocationArray;
    LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
    LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
    LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
    ActorObject->SetArrayField(TEXT("location"), LocationArray);
    
    FRotator Rotation = Actor->GetActorRotation();
    TArray<TSharedPtr<FJsonValue>> RotationArray;
    RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
    RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
    RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
    ActorObject->SetArrayField(TEXT("rotation"), RotationArray);
    
    FVector Scale = Actor->GetActorScale3D();
    TArray<TSharedPtr<FJsonValue>> ScaleArray;
    ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.X));
    ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Y));
    ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Z));
    ActorObject->SetArrayField(TEXT("scale"), ScaleArray);
    
    return MakeShared<FJsonValueObject>(ActorObject);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPCommonUtils::ActorToJsonObject(AActor* Actor, bool bDetailed)
{
    if (!Actor)
    {
        return nullptr;
    }
    
    TSharedPtr<FJsonObject> ActorObject = MakeShared<FJsonObject>();
    ActorObject->SetStringField(TEXT("name"), Actor->GetName());
    ActorObject->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
    
    FVector Location = Actor->GetActorLocation();
    TArray<TSharedPtr<FJsonValue>> LocationArray;
    LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
    LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
    LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
    ActorObject->SetArrayField(TEXT("location"), LocationArray);
    
    FRotator Rotation = Actor->GetActorRotation();
    TArray<TSharedPtr<FJsonValue>> RotationArray;
    RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
    RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
    RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
    ActorObject->SetArrayField(TEXT("rotation"), RotationArray);
    
    FVector Scale = Actor->GetActorScale3D();
    TArray<TSharedPtr<FJsonValue>> ScaleArray;
    ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.X));
    ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Y));
    ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Z));
    ActorObject->SetArrayField(TEXT("scale"), ScaleArray);
    
    return ActorObject;
}

UK2Node_Event* FEpicUnrealMCPCommonUtils::FindExistingEventNode(UEdGraph* Graph, const FString& EventName)
{
    if (!Graph)
    {
        return nullptr;
    }

    // Look for existing event nodes
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
        if (EventNode && EventNode->EventReference.GetMemberName() == FName(*EventName))
        {
            UE_LOG(LogTemp, Display, TEXT("Found existing event node with name: %s"), *EventName);
            return EventNode;
        }
    }

    return nullptr;
}

bool FEpicUnrealMCPCommonUtils::SetObjectProperty(UObject* Object, const FString& PropertyName, 
                                     const TSharedPtr<FJsonValue>& Value, FString& OutErrorMessage)
{
    if (!Object)
    {
        OutErrorMessage = TEXT("Invalid object");
        return false;
    }

    if (!Value.IsValid())
    {
        OutErrorMessage = TEXT("Invalid property value");
        return false;
    }

    FProperty* Property = Object->GetClass()->FindPropertyByName(*PropertyName);
    if (!Property)
    {
        OutErrorMessage = FString::Printf(TEXT("Property not found: %s"), *PropertyName);
        return false;
    }

    void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(Object);

    if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
    {
        BoolProperty->SetPropertyValue(PropertyAddr, Value->AsBool());
        return true;
    }

    if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
    {
        IntProperty->SetPropertyValue(PropertyAddr, static_cast<int32>(Value->AsNumber()));
        return true;
    }

    if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
    {
        FloatProperty->SetPropertyValue(PropertyAddr, static_cast<float>(Value->AsNumber()));
        return true;
    }

    if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
    {
        DoubleProperty->SetPropertyValue(PropertyAddr, Value->AsNumber());
        return true;
    }

    if (FStrProperty* StrProperty = CastField<FStrProperty>(Property))
    {
        StrProperty->SetPropertyValue(PropertyAddr, Value->AsString());
        return true;
    }

    if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
    {
        NameProperty->SetPropertyValue(PropertyAddr, FName(*Value->AsString()));
        return true;
    }

    if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
    {
        FString ResolutionLog;
        UClass* ResolvedClass = nullptr;
        if (Value->Type == EJson::String)
        {
            ResolvedClass = ResolveClassReference(ClassProperty->MetaClass, Value->AsString(), ResolutionLog);
        }

        if (!ResolvedClass && Value->Type != EJson::Null)
        {
            OutErrorMessage = FString::Printf(TEXT("Could not resolve class reference for '%s'. Tried: %s"), *PropertyName, *ResolutionLog);
            return false;
        }

        ClassProperty->SetObjectPropertyValue(PropertyAddr, ResolvedClass);
        return true;
    }

    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        UObject* ResolvedObject = nullptr;
        FString ResolutionLog;
        if (Value->Type == EJson::String)
        {
            ResolvedObject = ResolveObjectReference(ObjectProperty->PropertyClass, Value->AsString(), ResolutionLog);
        }

        if (!ResolvedObject && Value->Type != EJson::Null)
        {
            OutErrorMessage = FString::Printf(TEXT("Could not resolve object reference for '%s'. Tried: %s"), *PropertyName, *ResolutionLog);
            return false;
        }

        ObjectProperty->SetObjectPropertyValue(PropertyAddr, ResolvedObject);
        return true;
    }

    if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
    {
        UEnum* EnumDef = ByteProp->GetIntPropertyEnum();
        if (EnumDef)
        {
            if (Value->Type == EJson::Number)
            {
                ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(Value->AsNumber()));
                return true;
            }

            if (Value->Type == EJson::String)
            {
                FString EnumValueName = Value->AsString();
                if (EnumValueName.IsNumeric())
                {
                    ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(FCString::Atoi(*EnumValueName)));
                    return true;
                }

                if (EnumValueName.Contains(TEXT("::")))
                {
                    EnumValueName.Split(TEXT("::"), nullptr, &EnumValueName);
                }

                int64 EnumValue = EnumDef->GetValueByNameString(EnumValueName);
                if (EnumValue == INDEX_NONE)
                {
                    EnumValue = EnumDef->GetValueByNameString(Value->AsString());
                }

                if (EnumValue != INDEX_NONE)
                {
                    ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(EnumValue));
                    return true;
                }

                OutErrorMessage = FString::Printf(TEXT("Could not find enum value for '%s'"), *EnumValueName);
                return false;
            }
        }
        else
        {
            ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(Value->AsNumber()));
            return true;
        }
    }

    if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
    {
        UEnum* EnumDef = EnumProp->GetEnum();
        FNumericProperty* UnderlyingNumericProp = EnumProp->GetUnderlyingProperty();
        if (EnumDef && UnderlyingNumericProp)
        {
            if (Value->Type == EJson::Number)
            {
                UnderlyingNumericProp->SetIntPropertyValue(PropertyAddr, static_cast<int64>(Value->AsNumber()));
                return true;
            }

            if (Value->Type == EJson::String)
            {
                FString EnumValueName = Value->AsString();
                if (EnumValueName.IsNumeric())
                {
                    UnderlyingNumericProp->SetIntPropertyValue(PropertyAddr, FCString::Atoi64(*EnumValueName));
                    return true;
                }

                if (EnumValueName.Contains(TEXT("::")))
                {
                    EnumValueName.Split(TEXT("::"), nullptr, &EnumValueName);
                }

                int64 EnumValue = EnumDef->GetValueByNameString(EnumValueName);
                if (EnumValue == INDEX_NONE)
                {
                    EnumValue = EnumDef->GetValueByNameString(Value->AsString());
                }

                if (EnumValue != INDEX_NONE)
                {
                    UnderlyingNumericProp->SetIntPropertyValue(PropertyAddr, EnumValue);
                    return true;
                }

                OutErrorMessage = FString::Printf(TEXT("Could not find enum value for '%s'"), *EnumValueName);
                return false;
            }
        }
    }

    if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        if (StructProperty->Struct == TBaseStructure<FVector>::Get())
        {
            FVector VectorValue;
            if (!TryReadVectorValue(Value, VectorValue))
            {
                OutErrorMessage = FString::Printf(TEXT("Expected FVector-compatible JSON value for property %s"), *PropertyName);
                return false;
            }

            StructProperty->Struct->CopyScriptStruct(PropertyAddr, &VectorValue);
            return true;
        }

        if (StructProperty->Struct == TBaseStructure<FVector2D>::Get())
        {
            FVector2D Vector2DValue;
            if (!TryReadVector2DValue(Value, Vector2DValue))
            {
                OutErrorMessage = FString::Printf(TEXT("Expected FVector2D-compatible JSON value for property %s"), *PropertyName);
                return false;
            }

            StructProperty->Struct->CopyScriptStruct(PropertyAddr, &Vector2DValue);
            return true;
        }

        if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
        {
            FRotator RotatorValue;
            if (!TryReadRotatorValue(Value, RotatorValue))
            {
                OutErrorMessage = FString::Printf(TEXT("Expected FRotator-compatible JSON value for property %s"), *PropertyName);
                return false;
            }

            StructProperty->Struct->CopyScriptStruct(PropertyAddr, &RotatorValue);
            return true;
        }
    }

    OutErrorMessage = FString::Printf(TEXT("Unsupported property type: %s for property %s"), 
                                    *Property->GetClass()->GetName(), *PropertyName);
    return false;
}
