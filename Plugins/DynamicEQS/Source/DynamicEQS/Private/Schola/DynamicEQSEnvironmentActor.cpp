// Copyright Epic Games, Inc. All Rights Reserved.

#include "Schola/DynamicEQSEnvironmentActor.h"
#include "DynamicEQS.h"
#include "Agent/AgentInterface.h"       // IAgent::Execute_Define / Observe / Act
#include "SensorInterface.h"
#include "ActuatorInterface.h"

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

// -----------------------------------------------------------------------------
// IMultiAgentScholaEnvironment — default implementations
// -----------------------------------------------------------------------------

void ADynamicEQSEnvironmentActor::InitializeEnvironment_Implementation(
	TMap<FString, FInteractionDefinition>& OutAgentDefinitions)
{
	for (int32 i = 0; i < RegisteredAgents.Num(); ++i)
	{
		UDynamicEQSAgentComponent* Agent = RegisteredAgents[i];
		if (!Agent) continue;

		FString AgentId = FString::Printf(TEXT("env%d_agent%d"), EnvironmentId, i);
		FInteractionDefinition Def;
		IAgent::Execute_Define(Agent, Def);

		if (!Def.ObsSpaceDefn.IsValid() || !Def.ActionSpaceDefn.IsValid())
		{
			UE_LOG(LogDynamicEQS, Error,
				TEXT("DynamicEQSEnvironmentActor: Agent '%s' returned an invalid InteractionDefinition (missing Observer or Actuator). Skipping registration."),
				*Agent->GetOwner()->GetName());
			continue;
		}

		OutAgentDefinitions.Add(AgentId, Def);
	}
}

void ADynamicEQSEnvironmentActor::Reset_Implementation(
	TMap<FString, FInitialAgentState>& OutAgentState)
{
	for (int32 i = 0; i < RegisteredAgents.Num(); ++i)
	{
		UDynamicEQSAgentComponent* Agent = RegisteredAgents[i];
		if (!Agent) continue;

		IAgent::Execute_SetStatus(Agent, EAgentStatus::Running);

		FString AgentId = FString::Printf(TEXT("env%d_agent%d"), EnvironmentId, i);
		FInitialAgentState InitState;
		IAgent::Execute_Observe(Agent, InitState.Observations);
		OutAgentState.Add(AgentId, InitState);
	}
}

void ADynamicEQSEnvironmentActor::Step_Implementation(
	const TMap<FString, FInstancedStruct>& InActions,
	TMap<FString, FAgentState>& OutAgentStates)
{
	for (int32 i = 0; i < RegisteredAgents.Num(); ++i)
	{
		UDynamicEQSAgentComponent* Agent = RegisteredAgents[i];
		if (!Agent) continue;

		FString AgentId = FString::Printf(TEXT("env%d_agent%d"), EnvironmentId, i);

		if (const FInstancedStruct* ActionPtr = InActions.Find(AgentId))
		{
			IAgent::Execute_Act(Agent, *ActionPtr);
		}

		FAgentState State;
		IAgent::Execute_Observe(Agent, State.Observations);
		State.Reward      = 0.0f;
		State.bTerminated = (IAgent::Execute_GetStatus(Agent) != EAgentStatus::Running);
		State.bTruncated  = false;
		OutAgentStates.Add(AgentId, State);
	}
}
