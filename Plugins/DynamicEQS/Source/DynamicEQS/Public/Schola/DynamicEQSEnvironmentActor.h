// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Schola/DynamicEQSAgentComponent.h"

#include "DynamicEQSEnvironmentActor.generated.h"

/**
 * ADynamicEQSEnvironmentActor
 * Thin wrapper actor that groups one or more agent components into a
 * named RL environment.  Place in the level and register agents at
 * BeginPlay or via the Editor details panel.
 *
 * In multi-environment training setups (vectorised envs) each instance
 * receives a unique EnvironmentId so the trainer can fan out correctly.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Dynamic EQS Environment"))
class DYNAMICEQS_API ADynamicEQSEnvironmentActor : public AActor
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

	// -------------------------------------------------------------------------
	// Runtime helpers
	// -------------------------------------------------------------------------

	/** Register an agent at runtime (e.g. from a spawned character). */
	UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Environment")
	void RegisterAgent(UDynamicEQSAgentComponent* Agent);

	/** Remove an agent from the environment (e.g. on death). */
	UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Environment")
	void UnregisterAgent(UDynamicEQSAgentComponent* Agent);

protected:
	virtual void BeginPlay() override;
};
