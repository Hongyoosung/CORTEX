// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TrainingDataTypes/EnvironmentDefinition.h"
#include "Common/InteractionDefinition.h"
#include "TrainingDataTypes/EnvironmentUpdate.h"
#include "TrainingDataTypes/EnvironmentState.h"
#include "Containers/Map.h"
#include "EnvironmentInterface.generated.h"

struct FPoint;

/**
 * @brief A type-erased interface for a variety of Schola Environments (e.g. single and multi agent).
 * @details This interface provides a unified API for interacting with multiple flavors of 
 * reinforcement learning environments in Schola.
 */
class SCHOLATRAINING_API IScholaEnvironment
{
public:

	/**
	 * @brief Initialize the environment and retrieve agent definitions.
	 * @param[out] OutAgentDefinitions Map of agent names to their interaction definitions (observation/action spaces).
	 */
	virtual void InitializeEnvironment(TMap<FString, FInteractionDefinition>& OutAgentDefinitions) = 0;

	/**
	 * @brief Set the random seed for the environment.
	 * @param[in] Seed The random seed value to use for reproducibility.
	 */
	virtual void SeedEnvironment(int Seed) = 0;

	/**
	 * @brief Configure the environment with custom options. These options are specific to the environment implementation and are used to configure the environment.
	 * @param[in] Options Map of option names to values for environment configuration.
	 */
	virtual void SetEnvironmentOptions(const TMap<FString, FString>& Options) = 0;

	/**
	 * @brief Reset the environment to its initial state.
	 * @param[out] OutAgentState Map of agent names to their initial states after reset.
	 */
	virtual void Reset(TMap<FString, FInitialAgentState>& OutAgentState) = 0;

	/**
	 * @brief Execute one step in the environment with the given actions.
	 * @param[in] InActions Map of agent names to their selected actions.
	 * @param[out] OutAgentStates Map of agent names to their resulting states (observation, reward, done, etc.).
	 */
	virtual void Step(const TMap<FString, TInstancedStruct<FPoint>>& InActions, TMap<FString, FAgentState>& OutAgentStates) = 0;

	/**
	 * @brief Virtual destructor for proper cleanup of derived classes.
	 */
	virtual ~IScholaEnvironment() = default;
}; 

/**
 * @brief Template wrapper that bridges Blueprint-implementable interfaces to the type-erased IScholaEnvironment.
 * @tparam T The specific environment interface type (e.g., ISingleAgentScholaEnvironment).
 * @details This template allows Blueprint-implemented environments to be used through the generic
 * IScholaEnvironment interface, enabling polymorphic environment handling in C++.
 */
template <typename T>
class SCHOLATRAINING_API TScholaEnvironment : public TScriptInterface<T>, public IScholaEnvironment
{
public:
	/**
	 * @brief Virtual destructor for proper cleanup.
	 */
	~TScholaEnvironment() = default;

	/**
	 * @brief Construct a typed environment wrapper from a UObject.
	 * @param[in] InObject The UObject implementing the environment interface.
	 */
	TScholaEnvironment(UObject* InObject)
		: TScriptInterface<T>(InObject) {};

	/**
	 * @brief Initialize the environment through the Blueprint interface.
	 * @param[out] OutAgentDefinitions Map of agent names to their interaction definitions.
	 */
	void InitializeEnvironment(TMap<FString, FInteractionDefinition>& OutAgentDefinitions) override
	{
		T::Execute_InitializeEnvironment(this->GetObject(), OutAgentDefinitions);
	};

	/**
	 * @brief Set the random seed through the Blueprint interface.
	 * @param[in] Seed The random seed value.
	 */
	void SeedEnvironment(int Seed) override
	{
		T::Execute_SeedEnvironment(this->GetObject(), Seed);
	};

	/**
	 * @brief Set environment options through the Blueprint interface.
	 * @param[in] Options Map of configuration options.
	 */
	void SetEnvironmentOptions(const TMap<FString, FString>& Options) override
	{
		T::Execute_SetEnvironmentOptions(this->GetObject(), Options);
	};

	/**
	 * @brief Reset the environment through the Blueprint interface.
	 * @param[out] OutAgentState Map of agent names to their initial states.
	 */
	void Reset(TMap<FString, FInitialAgentState>& OutAgentState) override
	{
		T::Execute_Reset(this->GetObject(), OutAgentState);
	};

	/**
	 * @brief Execute a step through the Blueprint interface.
	 * @param[in] InActions Map of agent names to their actions.
	 * @param[out] OutAgentStates Map of agent names to their resulting states.
	 */
	void Step(const TMap<FString, TInstancedStruct<FPoint>>& InActions, TMap<FString, FAgentState>& OutAgentStates) override
	{
		T::Execute_Step(this->GetObject(), InActions, OutAgentStates);
	};

};

/**
 * @brief Base interface for all Schola Environments.
 * @details Use this to mark your environment class so it can be detected and managed by the training system.
 * All environment interfaces must derive from IScholaEnvironment.
 */
UINTERFACE(BlueprintType, Blueprintable)
class SCHOLATRAINING_API UBaseScholaEnvironment : public UInterface
{
	GENERATED_BODY()
};

/**
 * @brief Base interface implementation for Schola Environments.
 * @details This is the base class that all environment interfaces inherit from.
 */
class SCHOLATRAINING_API IBaseScholaEnvironment
{
	GENERATED_BODY()

};
