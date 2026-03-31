// Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Spaces/Space.h"
#include "InteractionDefinition.generated.h"

/**
 * @struct FInteractionDefinition
 * @brief Struct containing a definition of the inputs and outputs of a policy.
 * 
 * This structure defines the observation and action spaces for an agent,
 * specifying what observations it can receive and what actions it can produce.
 * This is fundamental to defining the interface between an agent and its environment.
 */
USTRUCT(BlueprintType)
struct SCHOLA_API FInteractionDefinition
{

	GENERATED_BODY()

	/**
	 * @brief Defines the range of values that the corresponding agent accepts as observations.
	 * 
	 * This space describes the structure and valid values for observations
	 * that the agent receives from its environment.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Schola|Interaction")
	TInstancedStruct<FSpace> ObsSpaceDefn;

	/**
	 * @brief Defines the range of values that are output by this agent's policy as actions.
	 * 
	 * This space describes the structure and valid values for actions
	 * that the agent can produce and send to its environment.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Schola|Interaction")
	TInstancedStruct<FSpace> ActionSpaceDefn;

	/**
	 * @brief Copy constructor.
	 * @param[in] Other The interaction definition to copy from.
	 */
	FInteractionDefinition(const FInteractionDefinition& Other)
	{
		ObsSpaceDefn = Other.ObsSpaceDefn;
		ActionSpaceDefn = Other.ActionSpaceDefn;
	}

	/**
	 * @brief Constructor with observation and action space parameters.
	 * @param[in] InObsSpaceDefn The observation space definition.
	 * @param[in] InActionSpaceDefn The action space definition.
	 */
	FInteractionDefinition(const TInstancedStruct<FSpace>& InObsSpaceDefn, const TInstancedStruct<FSpace>& InActionSpaceDefn)
		: ObsSpaceDefn(InObsSpaceDefn), ActionSpaceDefn(InActionSpaceDefn)
	{

	}

	/**
	 * @brief Default constructor.
	 */
	FInteractionDefinition()
	{

	}

};
