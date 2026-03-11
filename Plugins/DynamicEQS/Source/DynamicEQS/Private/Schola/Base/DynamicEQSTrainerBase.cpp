// Copyright Epic Games, Inc. All Rights Reserved.

#include "Schola/Base/DynamicEQSTrainerBase.h"

void UDynamicEQSTrainerBase::ResetEpisode()
{
	// Default: nothing to reset.
	// Subclasses should call Super::ResetEpisode() then reset their own state.
}

TMap<FString, float> UDynamicEQSTrainerBase::GetDebugInfo()
{
	// Default: return empty map.
	return TMap<FString, float>();
}
