// Copyright Epic Games, Inc. All Rights Reserved.

#include "Schola/DynamicEQSAgentComponent.h"
#include "DynamicEQS.h"
#include "GameFramework/Actor.h"

UDynamicEQSAgentComponent::UDynamicEQSAgentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDynamicEQSAgentComponent::BeginPlay()
{
	Super::BeginPlay();

	const TCHAR* ModeStr = (AgentMode == EDynamicEQSAgentMode::Training)
		? TEXT("Training") : TEXT("Inference");

	UE_LOG(LogDynamicEQS, Log,
		TEXT("DynamicEQSAgentComponent: Initialized on '%s' in %s mode."),
		*GetOwner()->GetName(), ModeStr);
}

FDynamicEQSWeightParameters UDynamicEQSAgentComponent::GetCurrentWeights() const
{
	return CurrentWeights;
}

void UDynamicEQSAgentComponent::SetExternalParameters(FInstancedStruct Params)
{
	ExternalParams = MoveTemp(Params);
}
