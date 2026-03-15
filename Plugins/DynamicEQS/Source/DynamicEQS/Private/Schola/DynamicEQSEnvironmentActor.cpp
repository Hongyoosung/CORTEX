// Copyright Epic Games, Inc. All Rights Reserved.

#include "Schola/DynamicEQSEnvironmentActor.h"
#include "DynamicEQS.h"

ADynamicEQSEnvironmentActor::ADynamicEQSEnvironmentActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ADynamicEQSEnvironmentActor::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogDynamicEQS, Log,
		TEXT("DynamicEQSEnvironmentActor: Environment %d started with %d registered agent(s)."),
		EnvironmentId, RegisteredAgents.Num());
}

void ADynamicEQSEnvironmentActor::RegisterAgent(UDynamicEQSAgentComponent* Agent)
{
	if (Agent && !RegisteredAgents.Contains(Agent))
	{
		RegisteredAgents.Add(Agent);
	}
}

void ADynamicEQSEnvironmentActor::UnregisterAgent(UDynamicEQSAgentComponent* Agent)
{
	RegisteredAgents.Remove(Agent);
}
