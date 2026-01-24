// Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.

#include "GymConnectors/ExternalGymConnector.h"

UExternalGymConnector::UExternalGymConnector()
{
}


void UExternalGymConnector::SubmitEnvironmentStates()
{
	this->SendState(this->TrainingState);

	UE_LOG(LogSchola, Warning, TEXT("Submitted Environment States to External Gym Connector"));
}

FTrainingStateUpdate* UExternalGymConnector::ResolveEnvironmentStateUpdate()
{
	TFuture<FTrainingStateUpdate*> UpdateFuture = this->RequestStateUpdate();
	if (UpdateFuture.WaitFor(FTimespan(0, 0, Timeout)))
	{
		return UpdateFuture.Get();
	}
	else
	{ // Timed out so assume we aren't running anymore
		UE_LOG(LogSchola, Warning, TEXT("Gym Connector Timed out. Marking as errored"));
		this->Status = EConnectorStatus::Error;
		UpdateFuture.Reset();
		return nullptr;
	}
}
