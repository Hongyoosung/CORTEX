// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/DEAbility.h"
#include "Characters/DECharacter.h"
#include "Team/DEMatchManager.h"
#include "Kismet/GameplayStatics.h"

void UDEAbility::Initialize(ADECharacter* InOwner)
{
	OwnerCharacter = InOwner;
	CachedMatchManager = GetMatchManager();
}

ADEMatchManager* UDEAbility::GetMatchManager() const
{
	if (CachedMatchManager)
	{
		return CachedMatchManager;
	}

	if (!OwnerCharacter || !OwnerCharacter->GetWorld())
	{
		return nullptr;
	}

	// Use the character's stored EnvID to find the correct DEMatchManager in multi-env levels
	TArray<AActor*> MatchManagers;
	UGameplayStatics::GetAllActorsOfClass(OwnerCharacter->GetWorld(), ADEMatchManager::StaticClass(), MatchManagers);

	for (AActor* Actor : MatchManagers)
	{
		if (ADEMatchManager* MM = Cast<ADEMatchManager>(Actor))
		{
			if (MM->GetEnvID() == OwnerCharacter->GetEnvID_Implementation())
			{
				return MM;
			}
		}
	}

	return nullptr;
}
