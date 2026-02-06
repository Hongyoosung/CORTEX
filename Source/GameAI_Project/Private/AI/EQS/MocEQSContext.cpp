// Copyright Epic Games, Inc. All Rights Reserved.

#include "EQS/MocEQSContext.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "AIController.h"
#include "Characters/MocCharacter.h"
#include "Core/MocGameMode.h"
#include "Team/TeamManager.h"
#include "Kismet/GameplayStatics.h"

void UEnvQueryContext_MocQuerier::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner)
	{
		return;
	}

	// Provide querier location
	UEnvQueryItemType_Point::SetContextHelper(ContextData, QueryOwner->GetActorLocation());
}

void UEnvQueryContext_MocEnemies::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner)
	{
		return;
	}

	AMocCharacter* MocChar = Cast<AMocCharacter>(QueryOwner);
	if (!MocChar)
	{
		// Try to get from controller
		AAIController* AIC = Cast<AAIController>(QueryOwner);
		if (AIC)
		{
			MocChar = Cast<AMocCharacter>(AIC->GetPawn());
		}
	}

	if (!MocChar)
	{
		return;
	}

	// Get enemy positions from team manager
	UWorld* World = QueryOwner->GetWorld();
	if (!World)
	{
		return;
	}

	AMocGameMode* GameMode = Cast<AMocGameMode>(UGameplayStatics::GetGameMode(World));
	if (!GameMode)
	{
		return;
	}

	ATeamManager* TeamManager = (MocChar->TeamID == 0) ? GameMode->GetRedTeamManager() : GameMode->GetBlueTeamManager();
	if (!TeamManager)
	{
		return;
	}

	// Get visible enemy positions
	TArray<FVector> EnemyPositions;
	for (const FSharedKnowledge& Knowledge : TeamManager->GetSharedKnowledge())
	{
		for (const FEnemyInfo& EnemyInfo : Knowledge.LastKnownEnemyPositions)
		{
			if (EnemyInfo.TimeSinceLastSeen < 5.0f) // 5-second memory decay
			{
				EnemyPositions.Add(EnemyInfo.LastKnownPosition);
			}
		}
	}

	UEnvQueryItemType_Point::SetContextHelper(ContextData, EnemyPositions);
}

void UEnvQueryContext_MocAllies::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner)
	{
		return;
	}

	AMocCharacter* MocChar = Cast<AMocCharacter>(QueryOwner);
	if (!MocChar)
	{
		AAIController* AIC = Cast<AAIController>(QueryOwner);
		if (AIC)
		{
			MocChar = Cast<AMocCharacter>(AIC->GetPawn());
		}
	}

	if (!MocChar)
	{
		return;
	}

	UWorld* World = QueryOwner->GetWorld();
	if (!World)
	{
		return;
	}

	AMocGameMode* GameMode = Cast<AMocGameMode>(UGameplayStatics::GetGameMode(World));
	if (!GameMode)
	{
		return;
	}

	ATeamManager* TeamManager = (MocChar->TeamID == 0) ? GameMode->GetRedTeamManager() : GameMode->GetBlueTeamManager();
	if (!TeamManager)
	{
		return;
	}

	// Get ally positions
	TArray<FVector> AllyPositions;
	for (AMocCharacter* Ally : TeamManager->GetTeamMembers())
	{
		if (Ally && Ally != MocChar && !Ally->IsDead())
		{
			AllyPositions.Add(Ally->GetActorLocation());
		}
	}

	UEnvQueryItemType_Point::SetContextHelper(ContextData, AllyPositions);
}

void UEnvQueryContext_MocCapturePoints::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner)
	{
		return;
	}

	UWorld* World = QueryOwner->GetWorld();
	if (!World)
	{
		return;
	}

	AMocGameMode* GameMode = Cast<AMocGameMode>(UGameplayStatics::GetGameMode(World));
	if (!GameMode)
	{
		return;
	}

	// Get all capture points
	TArray<FVector> PointPositions;
	for (ACapturePoint* Point : GameMode->GetCapturePoints())
	{
		if (Point)
		{
			PointPositions.Add(Point->GetActorLocation());
		}
	}

	UEnvQueryItemType_Point::SetContextHelper(ContextData, PointPositions);
}

void UEnvQueryContext_MocPickups::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner)
	{
		return;
	}

	UWorld* World = QueryOwner->GetWorld();
	if (!World)
	{
		return;
	}

	// Find all pickup actors (health packs, ammo crates)
	TArray<AActor*> FoundPickups;
	UGameplayStatics::GetAllActorsWithTag(World, FName("Pickup"), FoundPickups);

	TArray<FVector> PickupPositions;
	for (AActor* Pickup : FoundPickups)
	{
		if (Pickup)
		{
			PickupPositions.Add(Pickup->GetActorLocation());
		}
	}

	UEnvQueryItemType_Point::SetContextHelper(ContextData, PickupPositions);
}
