// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inference/InferenceComponent.h"
#include "StructUtils/InstancedStruct.h"
#include "EQS/DynamicEQSWeightParameters.h"
#include "Schola/Base/DynamicEQSObserverBase.h"
#include "Schola/Base/DynamicEQSActuatorBase.h"
#include "Schola/Base/DynamicEQSTrainerBase.h"

#include "DynamicEQSAgentComponent.generated.h"

/**
 * EDynamicEQSAgentMode
 * Controls whether the component drives the EQS executor from a live RL
 * policy (Inference) or collects data while being scripted (Training).
 */
UENUM(BlueprintType)
enum class EDynamicEQSAgentMode : uint8
{
	Training  UMETA(DisplayName = "Training"),
	Inference UMETA(DisplayName = "Inference"),
};

/**
 * UDynamicEQSAgentComponent
 * Central agent component that wires Observer, Actuator, and Trainer
 * together with a Schola UInferenceComponent integration.
 * Attach to any AActor that should act as an RL agent driving EQS queries.
 */
UCLASS(Blueprintable, ClassGroup = (DynamicEQS), meta = (BlueprintSpawnableComponent))
class DYNAMICEQS_API UDynamicEQSAgentComponent : public UInferenceComponent
{
	GENERATED_BODY()

public:
	UDynamicEQSAgentComponent();

	// -------------------------------------------------------------------------
	// Sub-object wiring
	// -------------------------------------------------------------------------

	/** Collects the observation vector each step. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "DynamicEQS|Agent")
	TObjectPtr<UDynamicEQSObserverBase> Observer;

	/** Translates policy output into EQS weight updates. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "DynamicEQS|Agent")
	TObjectPtr<UDynamicEQSActuatorBase> Actuator;

	/** Owns reward / termination / reset logic for the episode. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "DynamicEQS|Agent")
	TObjectPtr<UDynamicEQSTrainerBase> Trainer;

	// -------------------------------------------------------------------------
	// Configuration
	// -------------------------------------------------------------------------

	/** Operating mode: Training collects data; Inference runs the live policy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Agent")
	EDynamicEQSAgentMode AgentMode = EDynamicEQSAgentMode::Inference;

	// -------------------------------------------------------------------------
	// Runtime API
	// -------------------------------------------------------------------------

	/** Return a copy of the currently active EQS weight parameters. */
	UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Agent")
	FDynamicEQSWeightParameters GetCurrentWeights() const;

	/**
	 * Store an opaque external parameter struct that Observer / Actuator /
	 * Trainer subclasses can retrieve to customise their behaviour without
	 * requiring concrete component dependencies.
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Agent")
	void SetExternalParameters(FInstancedStruct Params);

	/** Returns the stored external parameters (read-only). */
	const FInstancedStruct& GetExternalParameters() const { return ExternalParams; }

protected:
	virtual void BeginPlay() override;

private:
	/** Current weight vector set by the last actuator action. */
	FDynamicEQSWeightParameters CurrentWeights;

	/** Opaque external parameters forwarded to sub-objects as needed. */
	FInstancedStruct ExternalParams;
};
