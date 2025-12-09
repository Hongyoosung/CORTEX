// Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Common/InteractionManager.h"

void UInteractionManager::SetupObservers(const TArray<UAbstractObserver*>& InObservers, TArray<UAbstractObserver*>& OutObservers)
{
	for (UAbstractObserver* Observer : InObservers)
	{
		Observer->InitializeObserver();
		OutObservers.Add(Observer);
	}
	OutObservers.Sort([](UAbstractObserver& A, UAbstractObserver& B) { return A.GetSanitizedId() < B.GetSanitizedId(); });
}

void UInteractionManager::CollectObservationsFromObservers(const TArray<UAbstractObserver*>& InObservers, FDictPoint& OutObservationsMap)
{
	for (UAbstractObserver* Observer : InObservers)
	{
		TPoint& ObservationRef = OutObservationsMap.Add();
		Observer->CollectObservations(ObservationRef);

		FTrajectoryData Data;

		Data.InteractorName = Observer->GetSanitizedId();
		Data.Value = Visit([](auto& Point) { return Point.ToString(); }, ObservationRef);

		this->TrajectoryStep.Observations.Add(Data);
	}
};

void UInteractionManager::CollectObservationSpaceFromObservers(const TArray<UAbstractObserver*>& InObservers, FDictSpace& OutDictSpace)
{
	FString LastId;
	FString Id;
	int		CurrentIncrement = 1;
	for (UAbstractObserver* Observer : InObservers)
	{
		Id = Observer->GetSanitizedId();
		if (Id == LastId)
		{
			UE_LOG(LogSchola, Warning, TEXT("Duplicate Observer ID: %s, adding suffix _%03d"), *Id, CurrentIncrement);
			Id.Appendf(TEXT("_%03d"), CurrentIncrement);
			CurrentIncrement++;
		}
		else
		{
			CurrentIncrement = 1;
			LastId = Id;
		}
		TSpace& Space = OutDictSpace.Add(Id);
		// Make a reference and then pass it in for filling
		Observer->FillObservationSpace(Space);
	}
};

void UInteractionManager::SetupActuators(const TArray<UActuator*>& InActuators, TArray<UActuator*>& OutActuators)
{
	for (UActuator* Actuator : InActuators)
	{
		Actuator->InitializeActuator();
		OutActuators.Add(Actuator);
	}
	// Put the Actuators in alphabetical order
	OutActuators.Sort([](UActuator& A, UActuator& B) { return A.GetSanitizedId() < B.GetSanitizedId(); });
}

void UInteractionManager::SendActionsToActuators(TArray<UActuator*>& OutActuators, const FDictPoint& Actions)
{
	int Id = 0;

	for (UActuator* Actuator : OutActuators)
	{

		FTrajectoryData Data;

		Data.InteractorName = Actuator->GetSanitizedId();
		Data.Value = Visit([](auto& Point) { return Point.ToString(); }, Actions[Id]);

		this->TrajectoryStep.Actions.Add(Data);

		// Send the action to the actuator
		Actuator->TakeAction(Actions[Id++]);
	}
};

void UInteractionManager::CollectActionSpaceFromActuators(const TArray<UActuator*>& InActuators, FDictSpace& OutSpaceGroups)
{

	FString LastId;
	FString Id;
	int		CurrentIncrement = 1;

	for (UActuator* Actuator : InActuators)
	{
		Id = Actuator->GetSanitizedId();
		if (Id == LastId)
		{
			Id.Appendf(TEXT("_%03d"), CurrentIncrement);
			CurrentIncrement++;
		}
		else
		{
			CurrentIncrement = 1;
			LastId = Id;
		}
		TSpace& TempGroup = OutSpaceGroups.Add(Id);

		Actuator->FillActionSpace(TempGroup);
	}
};

void UInteractionManager::Initialize(TArray<UAbstractObserver*>& InObservers, TArray<UActuator*>& InActuators)
{
	// Collect all the attached sensors
	SetupObservers(InObservers, this->Observers);
	CollectObservationSpaceFromObservers(this->Observers, this->InteractionDefn.ObsSpaceDefn);

	this->InteractionDefn.ObsSpaceDefn.InitializeEmptyDictPoint(this->Observations);
	// Collect all the attached Actuators
	SetupActuators(InActuators, this->Actuators);
	CollectActionSpaceFromActuators(this->Actuators, this->InteractionDefn.ActionSpaceDefn);
}

void UInteractionManager::DistributeActions(const FDictPoint& ActionMap)
{
	SendActionsToActuators(this->Actuators, ActionMap);
}

FDictPoint& UInteractionManager::AggregateObservations()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Schola:Observation Collection");

	// Clear the observations
	Observations.Reset();
	// Collect observaions from the sensors
	CollectObservationsFromObservers(Observers, this->Observations);

	return this->Observations;
}

void UInteractionManager::Reset()
{

	for (UAbstractObserver* Observer : Observers)
	{
		Observer->Reset();
	}

	for (UActuator* Actuator : Actuators)
	{
		Actuator->Reset();
	}
}