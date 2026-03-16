// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Debug/ActionDebugLibrary.h"
#include "LogScholaInteractors.h"

void UActionDebugLibrary::LogActionPoint(const FInstancedStruct& Action, const FString& Context)
{
	const UScriptStruct* ActionStruct = Action.GetScriptStruct();
	const FString TypeName = ActionStruct ? ActionStruct->GetName() : TEXT("null");
	
	UE_LOG(LogScholaInteractors, Warning, TEXT("[ACTION DEBUG] %s: Received type=%s"), *Context, *TypeName);
	
	if (const FDictPoint* DictAction = Action.GetPtr<FDictPoint>())
	{
		UE_LOG(LogScholaInteractors, Warning, TEXT("[ACTION DEBUG] %s: DictPoint with %d entries:"), *Context, DictAction->Points.Num());
		for (const TPair<FString, TInstancedStruct<FPoint>>& Pair : DictAction->Points)
		{
			const UScriptStruct* PointStruct = Pair.Value.GetScriptStruct();
			const FString PointTypeName = PointStruct ? PointStruct->GetName() : TEXT("null");
			
			// Also log the number of values if it's a BoxPoint
			if (const FBoxPoint* BoxPtr = Pair.Value.GetPtr<FBoxPoint>())
			{
				UE_LOG(LogScholaInteractors, Warning, TEXT("[ACTION DEBUG] %s:   - Key='%s', Type=%s, Values=%d"), 
					*Context, *Pair.Key, *PointTypeName, BoxPtr->Values.Num());
			}
			else
			{
				UE_LOG(LogScholaInteractors, Warning, TEXT("[ACTION DEBUG] %s:   - Key='%s', Type=%s"), 
					*Context, *Pair.Key, *PointTypeName);
			}
		}
	}
	else if (const FBoxPoint* BoxAction = Action.GetPtr<FBoxPoint>())
	{
		UE_LOG(LogScholaInteractors, Warning, TEXT("[ACTION DEBUG] %s: BoxPoint with %d values"), *Context, BoxAction->Values.Num());
	}
	else
	{
		UE_LOG(LogScholaInteractors, Warning, TEXT("[ACTION DEBUG] %s: Unknown point type"), *Context);
	}
}

bool UActionDebugLibrary::ExtractBoxPointFromDict(const FInstancedStruct& DictAction, const FString& Key, FInstancedStruct& OutBoxPoint)
{
	const FDictPoint* Dict = DictAction.GetPtr<FDictPoint>();
	if (!Dict)
	{
		const UScriptStruct* ActualStruct = DictAction.GetScriptStruct();
		const FString ActualType = ActualStruct ? ActualStruct->GetName() : TEXT("null");
		UE_LOG(LogScholaInteractors, Error, TEXT("[ACTION DEBUG] ExtractBoxPointFromDict: Input is not a DictPoint! Actual type: %s"), *ActualType);
		return false;
	}
	
	const TInstancedStruct<FPoint>* Entry = Dict->Points.Find(Key);
	if (!Entry)
	{
		UE_LOG(LogScholaInteractors, Error, TEXT("[ACTION DEBUG] ExtractBoxPointFromDict: Key '%s' not found in DictPoint"), *Key);
		UE_LOG(LogScholaInteractors, Error, TEXT("[ACTION DEBUG] Available keys in DictPoint:"));
		for (const TPair<FString, TInstancedStruct<FPoint>>& Pair : Dict->Points)
		{
			UE_LOG(LogScholaInteractors, Error, TEXT("[ACTION DEBUG]   - '%s'"), *Pair.Key);
		}
		return false;
	}
	
	const FBoxPoint* BoxPtr = Entry->GetPtr<FBoxPoint>();
	if (!BoxPtr)
	{
		const UScriptStruct* ActualType = Entry->GetScriptStruct();
		UE_LOG(LogScholaInteractors, Error, TEXT("[ACTION DEBUG] ExtractBoxPointFromDict: Key '%s' is not a BoxPoint, it's %s"), 
			*Key, ActualType ? *ActualType->GetName() : TEXT("null"));
		return false;
	}
	
	// Create a new FInstancedStruct from the BoxPoint
	OutBoxPoint.InitializeAs(FBoxPoint::StaticStruct(), reinterpret_cast<const uint8*>(BoxPtr));
	UE_LOG(LogScholaInteractors, Log, TEXT("[ACTION DEBUG] ExtractBoxPointFromDict: Successfully extracted BoxPoint for key '%s' with %d values"), 
		*Key, BoxPtr->Values.Num());
	return true;
}

bool UActionDebugLibrary::IsActionDictPoint(const FInstancedStruct& Action)
{
	return Action.GetPtr<FDictPoint>() != nullptr;
}

bool UActionDebugLibrary::IsActionBoxPoint(const FInstancedStruct& Action)
{
	return Action.GetPtr<FBoxPoint>() != nullptr;
}

FString UActionDebugLibrary::GetActionTypeName(const FInstancedStruct& Action)
{
	const UScriptStruct* ActionStruct = Action.GetScriptStruct();
	return ActionStruct ? ActionStruct->GetName() : TEXT("null");
}
