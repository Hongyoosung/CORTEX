// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "ImitationConnectors/AbstractImitationConnector.h"
#include "Environment/MultiAgentImitationEnvironmentInterface.h"
#include "Environment/SingleAgentImitationEnvironmentInterface.h"
#include "Kismet/GameplayStatics.h"
#include "LogScholaImitation.h"
#include "Engine/World.h"


UAbstractImitationConnector::UAbstractImitationConnector()
{
}

UAbstractImitationConnector::~UAbstractImitationConnector()
{
	for (IImitationScholaEnvironment* Env : Environments)
	{
		delete Env;
	}
	Environments.Empty();
}

void UAbstractImitationConnector::Init(const FTrainingDefinition& AgentDefinitions) {}

void UAbstractImitationConnector::Init(const TArray<TScriptInterface<IBaseImitationScholaEnvironment>>& InEnvironments)
{
	if (InEnvironments.Num() == 0)
	{
		UE_LOG(LogScholaImitation, Warning, TEXT("No Environments provided to Imitation Connector"));
		return;
	}

	this->Status = EImitationConnectorStatus::NotStarted;
	this->PrepareEnvironments(InEnvironments);

	// Add a bunch of defaulted values
	this->TrainingDefinition.EnvironmentDefinitions.AddDefaulted(Environments.Num());

	for (int i = 0; i < Environments.Num(); i++)
	{
		Environments[i]->InitializeEnvironment(TrainingDefinition.EnvironmentDefinitions[i].AgentDefinitions);
		Environments[i]->SeedEnvironment(0);
		Environments[i]->SetEnvironmentOptions({});
	}

	this->Init(this->TrainingDefinition);
}

void UAbstractImitationConnector::PrepareEnvironments(const TArray<TScriptInterface<IBaseImitationScholaEnvironment>>& InEnvironments)
{
	this->Environments.Empty();
	for (const TScriptInterface<IBaseImitationScholaEnvironment>& TempEnv : InEnvironments)
	{
		if (TempEnv.GetObject()->GetClass()->ImplementsInterface(UMultiAgentImitationScholaEnvironment::StaticClass()))
		{
			this->Environments.Add(new TImitationScholaEnvironment<IMultiAgentImitationScholaEnvironment>(TempEnv.GetObject()));
			UE_LOG(LogScholaImitation, Log, TEXT("Collected MultiAgent Imitation Environment %s"), *TempEnv.GetObject()->GetName());
		}
		else if (TempEnv.GetObject()->GetClass()->ImplementsInterface(USingleAgentImitationScholaEnvironment::StaticClass()))
		{
			this->Environments.Add(new TImitationScholaEnvironment<ISingleAgentImitationScholaEnvironment>(TempEnv.GetObject()));
			UE_LOG(LogScholaImitation, Log, TEXT("Collected SingleAgent Imitation Environment %s"), *TempEnv.GetObject()->GetName());
		}
		else
		{
			UE_LOG(LogScholaImitation, Warning, TEXT("Collected Environment %s does not implement a known Schola Imitation Environment Interface"), *TempEnv.GetObject()->GetName());
		}
	}
}

void UAbstractImitationConnector::CollectEnvironments(TArray<TScriptInterface<IBaseImitationScholaEnvironment>>& OutCollectedEnvironments)
{
	TArray<AActor*> TempEnvArray;
	UGameplayStatics::GetAllActorsWithInterface(GetWorld(), UBaseImitationScholaEnvironment::StaticClass(), TempEnvArray);
	
	for (AActor* TempEnv : TempEnvArray)
	{
		OutCollectedEnvironments.Add(TScriptInterface<IBaseImitationScholaEnvironment>(TempEnv));
		UE_LOG(LogScholaImitation, Log, TEXT("Collected Imitation Environment %s"), *TempEnv->GetName());
	}
}

void UAbstractImitationConnector::SetStatus(EImitationConnectorStatus NewStatus)
{
	if (NewStatus == EImitationConnectorStatus::Running)
	{
		this->OnConnectorStarted.Broadcast();
	}
	else if (NewStatus == EImitationConnectorStatus::Closed)
	{
		this->OnConnectorClosed.Broadcast();
	}
	else if (NewStatus == EImitationConnectorStatus::Error)
	{
		this->OnConnectorError.Broadcast();
	}
	this->Status = NewStatus;
}

void UAbstractImitationConnector::HandleImitationStep(FImitationTrainingState& OutTrainingState, FInitialState& OutInitialState)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Schola: Imitation Connector Step");

	

}

void UAbstractImitationConnector::Step()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Schola: Imitation Connector Main Step");
	FInitialState OutInitialState;
	FImitationTrainingState OutTrainingState;

	if (this->IsNotStarted())
	{
		bFirstStep = true;
		bool bStarted = this->CheckForStart();
		if (!bStarted)
		{
			return;
		}
	}

	// Request imitation state from external source (expert policy)
	if (this->IsRunning())
	{
		if (bFirstStep)
		{
			// Reset all environments and get initial states
			for (int i = 0; i < Environments.Num(); i++)
			{
				FInitialEnvironmentState& InitialEnvState = OutInitialState.EnvironmentStates.Emplace(i);
				Environments[i]->Reset(InitialEnvState.AgentStates);
			}
		}

		OutTrainingState.EnvironmentStates.AddDefaulted(this->Environments.Num());
		// Process each environment
		for (int i = 0; i < this->Environments.Num(); i++)
		{
			FImitationEnvironmentState& OutEnvState = OutTrainingState.EnvironmentStates[i];

			Environments[i]->Step(OutEnvState.AgentStates);

			// Check if all agents completed
			bool bAllCompleted = true;
			for (const TPair<FString, FImitationAgentState>& AgentStatePair : OutEnvState.AgentStates)
			{
				const FImitationAgentState& AgentState = AgentStatePair.Value;
				if (!(AgentState.bTerminated || AgentState.bTruncated))
				{
					bAllCompleted = false;
					break;
				}
			}
			// If all agents completed, reset the environment
			if (bAllCompleted)
			{
				FInitialEnvironmentState& InitialEnvState = OutInitialState.EnvironmentStates.Emplace(i);
				Environments[i]->Reset(InitialEnvState.AgentStates);
			}
		}
		this->SubmitStateWithInitialState(OutTrainingState, OutInitialState);
	}

	bFirstStep = false;
}

