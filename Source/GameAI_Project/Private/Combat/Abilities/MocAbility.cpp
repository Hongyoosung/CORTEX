// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/MocAbility.h"
#include "Characters/MocCharacter.h"
#include "Team/MatchManager.h"
#include "Kismet/GameplayStatics.h"

void UMocAbility::Initialize(AMocCharacter* InOwner)
{
	OwnerCharacter = InOwner;
	CachedMatchManager = GetMatchManager();
}

AMatchManager* UMocAbility::GetMatchManager() const
{
	if (CachedMatchManager)
	{
		return CachedMatchManager;
	}

	if (!OwnerCharacter || !OwnerCharacter->GetWorld())
	{
		return nullptr;
	}

	// Use the character's stored EnvID to find the correct MatchManager in multi-env levels
	TArray<AActor*> MatchManagers;
	UGameplayStatics::GetAllActorsOfClass(OwnerCharacter->GetWorld(), AMatchManager::StaticClass(), MatchManagers);

	for (AActor* Actor : MatchManagers)
	{
		if (AMatchManager* MM = Cast<AMatchManager>(Actor))
		{
			if (MM->GetEnvID() == OwnerCharacter->GetEnvID_Implementation())
			{
				return MM;
			}
		}
	}

	return nullptr;
}
