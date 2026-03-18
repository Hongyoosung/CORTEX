// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

# pragma once
#include "CoreMinimal.h"
#include "Steppers/StepperInterface.h"
#include "Agent/AgentInterface.h"
#include "LogScholaInferenceUtils.h"
#include "SimpleStepper.generated.h"

/**
 * @brief Simple synchronous stepper implementation.
 * 
 * A straightforward stepper that performs the full observation-inference-action loop
 * synchronously on the calling thread. On each Step() call:
 * 1. Collects observations from all agents
 * 2. Performs batched inference using the policy
 * 3. Applies the resulting actions to the agents
 * 
 * This stepper blocks during policy inference and is suitable for simple scenarios
 * or policies with fast inference times.
 */
UCLASS(Blueprintable, BlueprintType)
class SCHOLAINFERENCEUTILS_API USimpleStepper : public UObject, public IStepper
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TScriptInterface<IAgent>> Agents;

	UPROPERTY()
	TScriptInterface<IPolicy>		 Policy;

public:
	/**
	 * @brief Initialize the stepper with the given agents and policy.
	 * 
	 * @param[in] InAgents The array of agents to manage
	 * @param[in] InPolicy The policy to use for inference
	 * @return true if initialization succeeded, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Stepper")
	bool Init(const TArray<TScriptInterface<IAgent>>& InAgents, const TScriptInterface<IPolicy>& InPolicy) override
	{	
		this->Agents = InAgents;
		this->Policy = InPolicy;
		return true;
	}

	/**
	 * @brief Execute one step of the agent-policy loop.
	 * 
	 * Performs the full observation-inference-action cycle synchronously:
	 * - Collects observations from all agents
	 * - Calls the policy's BatchedThink method
	 * - Applies the resulting actions to each agent
	 * 
	 * This method blocks during policy inference.
	 */
	UFUNCTION(BlueprintCallable, Category = "Schola|Stepper")
	void Step()
	{
		if (Agents.Num() > 0 && Policy)
		{
			TArray<TInstancedStruct<FPoint>> Observations;
			TArray<TInstancedStruct<FPoint>> Actions;
			for (int i = 0; i < Agents.Num(); i++)
			{
				TInstancedStruct<FPoint> Observation;
				IAgent::Execute_Observe(Agents[i].GetObject(), Observation);
				Observations.Add(Observation);
			}
			if (Policy->BatchedThink(Observations, Actions))
			{
				if (Actions.Num() == Agents.Num())
				{
					for (int i = 0; i < Agents.Num(); i++)
					{
						IAgent::Execute_Act(Agents[i].GetObject(), Actions[i]);
					}
				}
				else
				{
					UE_LOG(LogScholaInferenceUtils, Error, TEXT("SimpleStepper: Number of actions (%d) does not match number of agents (%d)!"), Actions.Num(), Agents.Num());
				}
			}
			else
			{
				UE_LOG(LogScholaInferenceUtils, Error, TEXT("SimpleStepper: Policy failed to think!"));
			}
			
		}
		else
		{
			UE_LOG(LogScholaInferenceUtils, Error, TEXT("SimpleStepper: Agent or Policy is not set!"));
		}
	}
};