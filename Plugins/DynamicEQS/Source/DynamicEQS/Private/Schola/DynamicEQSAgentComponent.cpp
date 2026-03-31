// Copyright Epic Games, Inc. All Rights Reserved.

#include "Schola/DynamicEQSAgentComponent.h"
#include "DynamicEQS.h"
#include "GameFramework/Actor.h"
#include "Common/InteractionDefinition.h"
#include "SensorInterface.h"
#include "ActuatorInterface.h"

UDynamicEQSAgentComponent::UDynamicEQSAgentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;  // enabled via SetComponentTickEnabled in BeginPlay for Inference mode
}

void UDynamicEQSAgentComponent::BeginPlay()
{
	Super::BeginPlay();

	const TCHAR* ModeStr = (AgentMode == EDynamicEQSAgentMode::Training)
		? TEXT("Training") : TEXT("Inference");

	UE_LOG(LogDynamicEQS, Log,
		TEXT("DynamicEQSAgentComponent: Initialized on '%s' in %s mode."),
		*GetOwner()->GetName(), ModeStr);

	// Bind instanced subobjects to this component's owner actor
	if (Observer)
	{
		Observer->SetOwnerActor(GetOwner());
		IScholaSensor::Execute_InitSensor(Observer);
	}
	if (Actuator)
	{
		Actuator->SetOwnerActor(GetOwner());
		IScholaActuator::Execute_InitActuator(Actuator);
	}

	if (AgentMode == EDynamicEQSAgentMode::Inference)
	{
		IPolicy* PolicyInterface = Cast<IPolicy>(InferencePolicyObject);
		if (!PolicyInterface)
		{
			UE_LOG(LogDynamicEQS, Warning,
				TEXT("DynamicEQSAgentComponent: Inference mode but InferencePolicyObject is null or not an IPolicy on '%s'. Stepper will not be created."),
				*GetOwner()->GetName());
			return;
		}

		// Initialize the policy with this agent's interaction definition.
		FInteractionDefinition Definition;
		Define_Implementation(Definition);
		PolicyInterface->Init(Definition);

		// Create the stepper and wire it up.
		Stepper = NewObject<USimpleStepper>(this);
		TScriptInterface<IAgent> AgentInterface;
		AgentInterface.SetObject(this);
		AgentInterface.SetInterface(Cast<IAgent>(this));
		TScriptInterface<IPolicy> PolicyScriptInterface;
		PolicyScriptInterface.SetObject(InferencePolicyObject);
		PolicyScriptInterface.SetInterface(PolicyInterface);

		TArray<TScriptInterface<IAgent>> Agents = { AgentInterface };
		Stepper->Init(Agents, PolicyScriptInterface);

		// Enable tick so Stepper->Step() fires every frame.
		SetComponentTickEnabled(true);
	}
}

void UDynamicEQSAgentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (AgentMode == EDynamicEQSAgentMode::Inference && Stepper && Status == EAgentStatus::Running)
	{
		TimeSinceLastStep += DeltaTime;
		if (TimeSinceLastStep >= InferenceStepInterval)
		{
			TimeSinceLastStep = 0.0f;
			Stepper->Step();
		}
	}
}

// -------------------------------------------------------------------------
// IAgent implementation
// -------------------------------------------------------------------------

EAgentStatus UDynamicEQSAgentComponent::GetStatus_Implementation()
{
	return Status;
}

void UDynamicEQSAgentComponent::SetStatus_Implementation(EAgentStatus NewStatus)
{
	Status = NewStatus;
}

void UDynamicEQSAgentComponent::Define_Implementation(FInteractionDefinition& OutInteractionDefinition)
{
	if (!Observer)
	{
		UE_LOG(LogDynamicEQS, Error,
			TEXT("DynamicEQSAgentComponent::Define — Observer is null on '%s'. "
				 "Assign a UDynamicEQSObserverBase subclass (e.g. UDETacticalObserver) "
				 "to the Observer slot in the component's Details panel."),
			*GetOwner()->GetName());
		return;
	}
	if (!Actuator)
	{
		UE_LOG(LogDynamicEQS, Error,
			TEXT("DynamicEQSAgentComponent::Define — Actuator is null on '%s'. "
				 "Assign a UDynamicEQSActuatorBase subclass (e.g. UDETacticalParameterActuator) "
				 "to the Actuator slot in the component's Details panel."),
			*GetOwner()->GetName());
		return;
	}

	IScholaSensor::Execute_GetObservationSpace(Observer, OutInteractionDefinition.ObsSpaceDefn);
	IScholaActuator::Execute_GetActionSpace(Actuator, OutInteractionDefinition.ActionSpaceDefn);
}

void UDynamicEQSAgentComponent::Observe_Implementation(FInstancedStruct& OutObservations)
{
	if (Observer)
	{
		IScholaSensor::Execute_CollectObservations(Observer, OutObservations);
	}
}

void UDynamicEQSAgentComponent::Act_Implementation(const FInstancedStruct& InAction)
{
	if (Actuator)
	{
		IScholaActuator::Execute_TakeAction(Actuator, InAction);
	}
}

// -------------------------------------------------------------------------
// Runtime API
// -------------------------------------------------------------------------

FDynamicEQSWeightParameters UDynamicEQSAgentComponent::GetCurrentWeights() const
{
	return CurrentWeights;
}

void UDynamicEQSAgentComponent::SwapInferencePolicy(UObject* NewPolicyObject)
{
	if (AgentMode != EDynamicEQSAgentMode::Inference)
	{
		return;
	}

	if (NewPolicyObject == InferencePolicyObject)
	{
		return;
	}

	// Tear down existing stepper
	Stepper = nullptr;
	InferencePolicyObject = NewPolicyObject;

	if (!NewPolicyObject)
	{
		SetComponentTickEnabled(false);
		return;
	}

	IPolicy* PolicyInterface = Cast<IPolicy>(NewPolicyObject);
	if (!PolicyInterface)
	{
		UE_LOG(LogDynamicEQS, Warning,
			TEXT("SwapInferencePolicy: NewPolicyObject on '%s' does not implement IPolicy. Inference stopped."),
			*GetOwner()->GetName());
		SetComponentTickEnabled(false);
		return;
	}

	FInteractionDefinition Definition;
	Define_Implementation(Definition);
	PolicyInterface->Init(Definition);

	Stepper = NewObject<USimpleStepper>(this);
	TScriptInterface<IAgent> AgentInterface;
	AgentInterface.SetObject(this);
	AgentInterface.SetInterface(Cast<IAgent>(this));
	TScriptInterface<IPolicy> PolicyScriptInterface;
	PolicyScriptInterface.SetObject(NewPolicyObject);
	PolicyScriptInterface.SetInterface(PolicyInterface);

	TArray<TScriptInterface<IAgent>> Agents = { AgentInterface };
	Stepper->Init(Agents, PolicyScriptInterface);

	SetComponentTickEnabled(true);
	TimeSinceLastStep = 0.0f;
}

void UDynamicEQSAgentComponent::SetExternalParameters(FInstancedStruct Params)
{
	ExternalParams = MoveTemp(Params);
}
