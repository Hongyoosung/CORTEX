// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Agent/AgentInterface.h"           // IAgent, EAgentStatus (Schola)
#include "Policies/PolicyInterface.h"       // IPolicy (Schola)
#include "Steppers/SimpleStepper.h"         // USimpleStepper (ScholaInferenceUtils)
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
 * Central agent component that wires Observer, Actuator, and Trainer together
 * with the Schola 2.0.1 IAgent interface.
 *
 * In Training mode the GymConnector drives observation/action via the environment.
 * In Inference mode a USimpleStepper runs the Observe→Policy→Act loop each tick.
 *
 * Attach to any AActor that should act as an RL agent driving EQS queries.
 */
UCLASS(Blueprintable, ClassGroup = (DynamicEQS), meta = (BlueprintSpawnableComponent))
class DYNAMICEQS_API UDynamicEQSAgentComponent : public UActorComponent, public IAgent
{
	GENERATED_BODY()

public:
	UDynamicEQSAgentComponent();

	// -------------------------------------------------------------------------
	// Configuration
	// -------------------------------------------------------------------------

	/** Operating mode: Training collects data; Inference runs the live policy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Agent")
	EDynamicEQSAgentMode AgentMode = EDynamicEQSAgentMode::Inference;

	/**
	 * Policy used in Inference mode.
	 * Assign a UNNEPolicy asset (or any IPolicy implementor) in the details panel.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "DynamicEQS|Agent|Inference")
	TObjectPtr<UObject> InferencePolicyObject;

	/**
	 * Minimum interval (seconds) between inference steps in Inference mode.
	 * Prevents the policy from being queried every frame, which would cause
	 * StopMovement()+MoveTo() thrashing. Should match the training step rate.
	 * Default 0.2s = 5 Hz (typical gRPC training step rate).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Agent|Inference", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float InferenceStepInterval = 0.2f;

	/**
	 * Observer subobject that collects observations for the Schola pipeline.
	 * Assign a UDynamicEQSObserverBase subclass in the details panel (Instanced).
	 * OwnerActor is set automatically to this component's owner in BeginPlay.
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "DynamicEQS|Agent")
	TObjectPtr<UDynamicEQSObserverBase> Observer;

	/**
	 * Actuator subobject that applies policy actions for the Schola pipeline.
	 * Assign a UDynamicEQSActuatorBase subclass in the details panel (Instanced).
	 * OwnerActor is set automatically to this component's owner in BeginPlay.
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "DynamicEQS|Agent")
	TObjectPtr<UDynamicEQSActuatorBase> Actuator;

	/** Returns the active observer subobject (may be null if not assigned). */
	UFUNCTION(BlueprintPure, Category = "DynamicEQS|Agent")
	UDynamicEQSObserverBase* GetObserver() const { return Observer.Get(); }

	/** Returns the active actuator subobject (may be null if not assigned). */
	UFUNCTION(BlueprintPure, Category = "DynamicEQS|Agent")
	UDynamicEQSActuatorBase* GetActuator() const { return Actuator.Get(); }

	// -------------------------------------------------------------------------
	// IAgent implementation
	// -------------------------------------------------------------------------

	virtual EAgentStatus GetStatus_Implementation() override;
	virtual void SetStatus_Implementation(EAgentStatus NewStatus) override;
	virtual void Define_Implementation(FInteractionDefinition& OutInteractionDefinition) override;
	virtual void Observe_Implementation(FInstancedStruct& OutObservations) override;
	virtual void Act_Implementation(const FInstancedStruct& InAction) override;

	// -------------------------------------------------------------------------
	// Runtime API
	// -------------------------------------------------------------------------

	/** Return a copy of the currently active EQS weight parameters. */
	UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Agent")
	FDynamicEQSWeightParameters GetCurrentWeights() const;

	/**
	 * Hot-swap the active inference policy at runtime.
	 * Tears down the existing stepper and rebuilds it with NewPolicyObject.
	 * No-op if not in Inference mode or if NewPolicyObject is the same object.
	 * @param NewPolicyObject  Must implement IPolicy. Pass null to stop inference.
	 */
	UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Agent|Inference")
	void SwapInferencePolicy(UObject* NewPolicyObject);

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
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Current agent status (Running/Stopped/Error). */
	EAgentStatus Status = EAgentStatus::Running;

	/** Stepper that drives the Observe→Policy→Act loop in Inference mode. */
	UPROPERTY()
	TObjectPtr<USimpleStepper> Stepper;

	/** Current weight vector set by the last actuator action. */
	FDynamicEQSWeightParameters CurrentWeights;

	/** Opaque external parameters forwarded to sub-objects as needed. */
	FInstancedStruct ExternalParams;

	/** Accumulated time since last inference step (inference throttle). */
	float TimeSinceLastStep = 0.0f;
};
