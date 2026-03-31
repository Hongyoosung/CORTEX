// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Environment/MultiAgentEnvironmentInterface.h"  // IMultiAgentScholaEnvironment (ScholaTraining)
#include "Common/InteractionDefinition.h"                // FInteractionDefinition (Schola)
#include "TrainingDataTypes/AgentState.h"                // FInitialAgentState, FAgentState (ScholaTraining)
#include "StructUtils/InstancedStruct.h"
#include "Schola/DynamicEQSAgentComponent.h"

#include "DynamicEQSEnvironmentActor.generated.h"

// Forward declarations
class AGymConnectorManager;

/**
 * ADynamicEQSEnvironmentActor
 * Base environment actor for the DynamicEQS plugin. Implements IMultiAgentScholaEnvironment
 * with sensible defaults so project subclasses only need to override what differs.
 *
 * Default IMultiAgentScholaEnvironment behaviour:
 *  - InitializeEnvironment: calls IAgent::Execute_Define on every registered agent.
 *  - Reset:                 resets all agent statuses and collects initial observations.
 *  - Step:                  applies actions then collects observations via the agent interface.
 *  - SeedEnvironment:       no-op default (override for deterministic randomness).
 *  - SetEnvironmentOptions: no-op default.
 *
 * For Python-side Schola training integration, assign a GymConnectorManager actor placed
 * in the level.  The manager owns the gRPC connector and drives the environment interface
 * each frame; no project code is required to wire it up manually.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Dynamic EQS Environment"))
class DYNAMICEQS_API ADynamicEQSEnvironmentActor : public AActor, public IMultiAgentScholaEnvironment
{
	GENERATED_BODY()

public:
	ADynamicEQSEnvironmentActor();

	// -------------------------------------------------------------------------
	// Configuration
	// -------------------------------------------------------------------------

	/** Unique identifier for this environment instance (used by the trainer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Environment")
	int32 EnvironmentId = 0;

	/** Agents managed by this environment. Populated in the editor or at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Environment")
	TArray<TObjectPtr<UDynamicEQSAgentComponent>> RegisteredAgents;

	/**
	 * Optional reference to an AGymConnectorManager actor placed in the level.
	 * Assign this to enable external Python script training via Schola's gRPC connector
	 * without requiring any project-side wiring code.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "DynamicEQS|Training")
	TObjectPtr<AGymConnectorManager> GymConnectorManager;

	// -------------------------------------------------------------------------
	// Runtime helpers
	// -------------------------------------------------------------------------

	/** Register an agent at runtime (e.g. from a spawned character). */
	UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Environment")
	void RegisterAgent(UDynamicEQSAgentComponent* Agent);

	/** Remove an agent from the environment (e.g. on death). */
	UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Environment")
	void UnregisterAgent(UDynamicEQSAgentComponent* Agent);

	// -------------------------------------------------------------------------
	// IMultiAgentScholaEnvironment — default implementations
	// Override in project subclasses to add game-specific logic.
	// -------------------------------------------------------------------------

	virtual void InitializeEnvironment_Implementation(TMap<FString, FInteractionDefinition>& OutAgentDefinitions) override;
	virtual void Reset_Implementation(TMap<FString, FInitialAgentState>& OutAgentState) override;
	virtual void Step_Implementation(const TMap<FString, FInstancedStruct>& InActions, TMap<FString, FAgentState>& OutAgentStates) override;
	virtual void SeedEnvironment_Implementation(int Seed) override {}
	virtual void SetEnvironmentOptions_Implementation(const TMap<FString, FString>& Options) override {}

protected:
	virtual void BeginPlay() override;
};
