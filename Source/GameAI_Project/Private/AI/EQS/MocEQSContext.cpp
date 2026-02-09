// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/EQS/MocEQSContext.h"
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

	ATeamManager* TeamManager = GameMode->GetTeamManager();
	if (!TeamManager)
	{
		return;
	}

	int32 MyTeamID = MocChar->GetTeamID_Implementation();

	// Get enemy positions from enemy team agents
	TArray<FVector> EnemyPositions;
	TArray<AMocCharacter*> EnemyAgents = TeamManager->GetEnemyAgents(MyTeamID);

	for (AMocCharacter* Enemy : EnemyAgents)
	{
		if (Enemy && Enemy->IsAlive())
		{
			// Use FogOfWarManager to check if enemy is visible
			AFogOfWarManager* FogManager = TeamManager->GetFogOfWarManager();
			if (FogManager && TeamManager->IsEnemyPositionValid(MyTeamID, Enemy))
			{
				EnemyPositions.Add(TeamManager->GetLastKnownEnemyPosition(MyTeamID, Enemy));
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

	ATeamManager* TeamManager = GameMode->GetTeamManager();
	if (!TeamManager)
	{
		return;
	}

	int32 MyTeamID = MocChar->GetTeamID_Implementation();

	// Get ally positions
	TArray<FVector> AllyPositions;
	TArray<AMocCharacter*> TeamAgents = TeamManager->GetTeamAgents(MyTeamID);

	for (AMocCharacter* Ally : TeamAgents)
	{
		if (Ally && Ally != MocChar && Ally->IsAlive())
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

	// Get all capture points by tag
	TArray<AActor*> FoundCapturePoints;
	UGameplayStatics::GetAllActorsWithTag(World, FName("CapturePoint"), FoundCapturePoints);

	TArray<FVector> PointPositions;
	for (AActor* Point : FoundCapturePoints)
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
