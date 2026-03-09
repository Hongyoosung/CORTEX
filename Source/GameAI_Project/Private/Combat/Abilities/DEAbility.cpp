// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/DEAbility.h"
#include "Characters/DECharacter.h"
#include "Team/DEMatchManager.h"
#include "Kismet/GameplayStatics.h"

void UDEAbility::Initialize(ADECharacter* InOwner)
{
	OwnerCharacter = InOwner;
	// NOTE: Do NOT cache MatchManager here.
	// At Initialize() time, EnvID is still 0 (default) because SpawnActor()
	// triggers BeginPlay/Initialize before SetEnvID_Implementation() is called.
	// Caching here causes envs 1,2,3 to lock onto env 0's MatchManager → no combat.
	// Instead, let GetMatchManager() lazily resolve on first use after EnvID is set.
	CachedMatchManager = nullptr;
}

ADEMatchManager* UDEAbility::GetMatchManager() const
{
	// Validate cache: ensure the cached MatchManager still matches the owner's EnvID.
	// During spawn, abilities are initialized before EnvID is set, so the cache
	// may point to the wrong environment's MatchManager.
	if (CachedMatchManager && OwnerCharacter
		&& CachedMatchManager->GetEnvID() == OwnerCharacter->GetEnvID_Implementation())
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
				CachedMatchManager = MM;
				return MM;
			}
		}
	}

	return nullptr;
}
