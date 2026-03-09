// Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.

#include "GymConnectors/AbstractGymConnector.h"

UAbstractGymConnector::UAbstractGymConnector()
{
}

void UAbstractGymConnector::Init(const FTrainingDefinition& AgentDefinitions) {}

void UAbstractGymConnector::Init()
{
	this->Status = EConnectorStatus::NotStarted;
	this->CollectEnvironments();
	// Add a bunch of defaulted values
	this->TrainingState.EnvironmentStates.AddDefaulted(Environments.Num());
	this->TrainingDefinition.EnvironmentDefinitions.AddDefaulted(Environments.Num());

	for (int i = 0; i < Environments.Num(); i++)
	{
		Environments[i]->Initialize();
		Environments[i]->PopulateAgentDefinitionPointers(TrainingDefinition.EnvironmentDefinitions[i]);
		Environments[i]->PopulateAgentStatePointers(TrainingState.EnvironmentStates[i]);
		Environments[i]->State = &TrainingState.EnvironmentStates[i];
	}

	this->Init(this->TrainingDefinition);
}

void UAbstractGymConnector::ResetCompletedEnvironments()
{

	int Count = 0;
	for (AAbstractScholaEnvironment* Environment : this->Environments)
	{
		if (Environment->GetStatus() == EEnvironmentStatus::Completed)
		{
			Count++;
			Environment->Reset();
		}
	}
	if (Count == 0)
	{
		return;
	}
	// TODO make this take an array of ints
	this->SubmitPostResetState(this->TrainingState);
	UE_LOG(LogSchola, Verbose, TEXT("Reset %d Environments"), Count);

	UE_LOG(LogSchola, Warning, TEXT("ResetCompletedEnvironments: Reset $d Environments"));

	//We set the environment back to running once we've sent out the state etc. etc.
	for (AAbstractScholaEnvironment* Environment : this->Environments)
	{
		if (Environment->GetStatus() == EEnvironmentStatus::Completed)
		{
			Environment->UpdateStatus(EEnvironmentStatus::Running);
		}
	}
}

void UAbstractGymConnector::CollectEnvironments()
{

	TArray<AActor*> TempEnvArray;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AAbstractScholaEnvironment::StaticClass(), TempEnvArray);

	for (AActor* Actor : TempEnvArray)
	{
		this->RegisterEnvironment(Cast<AAbstractScholaEnvironment>(Actor));
	}
}

void UAbstractGymConnector::RegisterEnvironment(AAbstractScholaEnvironment* Environment)
{
	UE_LOG(LogSchola, Log, TEXT("Environment Registered"))
	// Since Environments can not be deregistered we can just use the length of this list as an ever increasing ID
	// If we allow Environments to be deregistered we need to change how we handle EnvIds
	int Id = this->Environments.Num();
	this->Environments.Add(Environment);
	Environment->SetEnvId(Id);
}

void UAbstractGymConnector::CollectEnvironmentStates()
{
	for (AAbstractScholaEnvironment* Environment : this->Environments)
	{
		if (Environment->GetStatus() != EEnvironmentStatus::Error)
		{
			Environment->AllAgentsThink();
		}
	}
}

void UAbstractGymConnector::SetStatus(EConnectorStatus NewStatus)
{
	if (NewStatus == EConnectorStatus::Running)
	{
		this->OnConnectorStarted.Broadcast();
	}
	else if (NewStatus == EConnectorStatus::Closed)
	{
		this->OnConnectorClosed.Broadcast();
	}
	else if (NewStatus == EConnectorStatus::Error)
	{
		this->OnConnectorError.Broadcast();
	}
	this->Status = NewStatus;
}

void UAbstractGymConnector::UpdateConnectorStatus(const FTrainingStateUpdate& Decision)
{
	if (Decision.IsError())
	{
		this->SetStatus(EConnectorStatus::Error);
		UE_LOG(LogSchola, Warning, TEXT("Gym Connector Error"));
	}
	else if (Decision.IsClose())
	{
		this->SetStatus(EConnectorStatus::Closed);
		UE_LOG(LogSchola, Warning, TEXT("Gym Connector Closed"));
	}
}

void UAbstractGymConnector::UpdateConnectorStatus()
{
	this->UpdateConnectorStatus(*this->GetCurrentStateUpdate());
}

void UAbstractGymConnector::UpdateEnvironments(FTrainingStateUpdate& StateUpdate)
{

	for (const TTuple<int, FEnvUpdate>& EnvironmentStateUpdatePair : StateUpdate.EnvUpdates)
	{
		const FEnvUpdate& EnvUpdate = EnvironmentStateUpdatePair.Value;
		if (EnvUpdate.IsReset())
		{
			FEnvReset EnvReset = EnvUpdate.GetReset();
			if (EnvReset.bHasSeed)
			{
				Environments[EnvironmentStateUpdatePair.Key]->SeedEnvironment(EnvReset.Seed);
			}
			UE_LOG(LogSchola, Log, TEXT("Environment Has %d options supplied"), EnvReset.Options.Num());

			if (EnvReset.Options.Num() > 0)
			{
				Environments[EnvironmentStateUpdatePair.Key]->SetEnvironmentOptions(EnvReset.Options);
			}

			UE_LOG(LogSchola, Log, TEXT("Marking Environment %d as completed"), EnvironmentStateUpdatePair.Key);
			Environments[EnvironmentStateUpdatePair.Key]->MarkCompleted();
		}
		else
		{
			Environments[EnvironmentStateUpdatePair.Key]->AllAgentsAct(EnvironmentStateUpdatePair.Value.GetStep());
		}
	}
}

void UAbstractGymConnector::SameStepAutoReset()
{
	this->ResetCompletedEnvironments();
}

void UAbstractGymConnector::AutoReset()
{	
	EAutoResetType AutoResetType = this->GetAutoResetType();

	if (AutoResetType == EAutoResetType::SameStep)
	{
		this->SameStepAutoReset();
	}
	else if (AutoResetType == EAutoResetType::NextStep)
	{
		UE_LOG(LogSchola, Warning, TEXT("EAutoResetType::NextStep not supported. Using Default AutoReset"));
	}
}
