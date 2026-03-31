// Copyright Epic Games, Inc. All Rights Reserved.

#include "Schola/Base/DynamicEQSTrainerBase.h"

EAgentTrainingStatus ADynamicEQSTrainerBase::ComputeStatus()
{
	return ComputeTermination();
}

void ADynamicEQSTrainerBase::GetInfo(TMap<FString, FString>& Info)
{
	for (const TPair<FString, float>& Pair : GetDebugInfo())
	{
		Info.Add(Pair.Key, FString::SanitizeFloat(Pair.Value));
	}
}

void ADynamicEQSTrainerBase::ResetTrainer()
{
	ResetEpisode();
}

void ADynamicEQSTrainerBase::OnCompletion()
{
	// Default: nothing to do on completion.
}

void ADynamicEQSTrainerBase::ResetEpisode()
{
	// Default: nothing to reset.
	// Subclasses should call Super::ResetEpisode() then reset their own state.
}

TMap<FString, float> ADynamicEQSTrainerBase::GetDebugInfo()
{
	// Default: return empty map.
	return TMap<FString, float>();
}
