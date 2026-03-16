// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Points/Point.h"
#include "Points/DictPoint.h"
#include "Points/BoxPoint.h"
#include "ActionDebugLibrary.generated.h"

/**
 * Blueprint Function Library for debugging action dispatch issues.
 * Use these functions to log action types and safely extract BoxPoints from DictPoints.
 */
UCLASS()
class SCHOLAINTERACTORS_API UActionDebugLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 
	 * @brief Log the type and contents of an action Point with detailed information.
	 * 
	 * Prints the action type name, and if it's a DictPoint, lists all keys and their types.
	 * If it's a BoxPoint, prints the number of values.
	 * Use this in Blueprint before dispatching actions to see what type you're actually receiving.
	 * 
	 * @param[in] Action The action to inspect
	 * @param[in] Context A label for the log message (e.g., "BeforeDispatch", "Agent0Action")
	 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Debug")
	static void LogActionPoint(const FInstancedStruct& Action, const FString& Context);
	
	/** 
	 * @brief Extract a BoxPoint from a DictPoint by key, with detailed logging.
	 * 
	 * Safely extracts an individual actuator action from a dictionary of actions.
	 * Logs detailed error messages if the key is not found or if the value is not a BoxPoint.
	 * Use this to extract individual actuator actions from the agent's DictPoint action.
	 * 
	 * @param[in] DictAction The DictPoint containing all actuator actions
	 * @param[in] Key The key for the actuator (e.g., "MovementInputActuator0")
	 * @param[out] OutBoxPoint The extracted BoxPoint (only valid if function returns true)
	 * @return True if extraction succeeded, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Debug")
	static bool ExtractBoxPointFromDict(const FInstancedStruct& DictAction, const FString& Key, FInstancedStruct& OutBoxPoint);
	
	/**
	 * @brief Check if an action is a DictPoint.
	 * 
	 * @param[in] Action The action to check
	 * @return True if the action is a DictPoint, false otherwise
	 */
	UFUNCTION(BlueprintPure, Category = "Schola|Debug")
	static bool IsActionDictPoint(const FInstancedStruct& Action);
	
	/**
	 * @brief Check if an action is a BoxPoint.
	 * 
	 * @param[in] Action The action to check
	 * @return True if the action is a BoxPoint, false otherwise
	 */
	UFUNCTION(BlueprintPure, Category = "Schola|Debug")
	static bool IsActionBoxPoint(const FInstancedStruct& Action);
	
	/**
	 * @brief Get the type name of an action for debugging.
	 * 
	 * Returns the UStruct name of the action (e.g., "BoxPoint", "DictPoint", "MultiDiscretePoint").
	 * 
	 * @param[in] Action The action to inspect
	 * @return The type name as a string, or "null" if the action is invalid
	 */
	UFUNCTION(BlueprintPure, Category = "Schola|Debug")
	static FString GetActionTypeName(const FInstancedStruct& Action);
};
