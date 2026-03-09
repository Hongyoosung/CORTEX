// Copyright Epic Games, Inc. All Rights Reserved.

#include "GAS/DEGameplayAbility.h"

UDEGameplayAbility::UDEGameplayAbility()
{
	// Server-initiated, no input binding needed (AI agents)
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}
