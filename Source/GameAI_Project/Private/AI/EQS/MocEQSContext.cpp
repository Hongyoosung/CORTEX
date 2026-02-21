// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/EQS/MocEQSContext.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "AIController.h"
#include "Characters/MocCharacter.h"
#include "Core/MocGameMode.h"
#include "Team/TeamManager.h"
#include "Actors/CapturePoint.h"
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
		if (Enemy && Enemy->IsAlive_Implementation())
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
		if (Ally && Ally != MocChar && Ally->IsAlive_Implementation())
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

void UEnvQueryContext_MocEnemyObjective::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner)
	{
		return;
	}

	// Get querier's character
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

	int32 MyTeamID = MocChar->GetTeamID_Implementation();
	FVector AgentPos = MocChar->GetActorLocation();

	// Find the NEAREST non-friendly capture point instead of only the enemy base.
	// This aligns EQS objective-seeking with the reward function, which rewards
	// progress toward the closest non-friendly point.
	TArray<AActor*> AllCapturePoints;
	UGameplayStatics::GetAllActorsOfClass(World, ACapturePoint::StaticClass(), AllCapturePoints);

	ACapturePoint* NearestNonFriendly = nullptr;
	float NearestDist = FLT_MAX;

	for (AActor* Actor : AllCapturePoints)
	{
		ACapturePoint* CP = Cast<ACapturePoint>(Actor);
		if (!CP) continue;

		// Skip points already owned by this agent's team
		if (CP->GetOwningTeamID() == MyTeamID) continue;

		float Dist = FVector::Dist(AgentPos, CP->GetActorLocation());
		if (Dist < NearestDist)
		{
			NearestDist = Dist;
			NearestNonFriendly = CP;
		}
	}

	if (NearestNonFriendly)
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, NearestNonFriendly->GetActorLocation());
		return;
	}

	// Fallback: all points are friendly — target enemy base as push target
	int32 EnemyTeamID = (MyTeamID == 0) ? 1 : 0;
	ECapturePointID EnemyBaseID = (EnemyTeamID == 0) ? ECapturePointID::PointA : ECapturePointID::PointE;

	for (AActor* Actor : AllCapturePoints)
	{
		ACapturePoint* CP = Cast<ACapturePoint>(Actor);
		if (CP && CP->PointID == EnemyBaseID)
		{
			UEnvQueryItemType_Point::SetContextHelper(ContextData, CP->GetActorLocation());
			return;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("[MocEQSContext] No non-friendly objective found for team %d"), MyTeamID);
}

void UEnvQueryContext_MocAllyObjective::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner)
	{
		return;
	}

	// Get querier's character
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

	// Get my team ID to determine friendly base
	int32 MyTeamID = MocChar->GetTeamID_Implementation();

	// Find friendly team's base capture point
	// Red team base = PointA, Blue team base = PointE
	ECapturePointID AllyBaseID = (MyTeamID == 0) ? ECapturePointID::PointA : ECapturePointID::PointE;

	// Find the specific capture point
	TArray<AActor*> AllCapturePoints;
	UGameplayStatics::GetAllActorsOfClass(World, ACapturePoint::StaticClass(), AllCapturePoints);

	for (AActor* Actor : AllCapturePoints)
	{
		ACapturePoint* CapturePoint = Cast<ACapturePoint>(Actor);
		if (CapturePoint && CapturePoint->PointID == AllyBaseID)
		{
			UEnvQueryItemType_Point::SetContextHelper(ContextData, CapturePoint->GetActorLocation());
			return;
		}
	}

	// If not found, log warning
	UE_LOG(LogTemp, Error, TEXT("[MocEQSContext] Ally objective not found (Point %d) for team %d"),
		static_cast<int32>(AllyBaseID), MyTeamID);
}

void UEnvQueryContext_MocCoverPoints::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
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

	// Find all actors tagged as "Cover"
	// Note: Cover points should be placed in the level and tagged appropriately
	TArray<AActor*> FoundCoverPoints;
	UGameplayStatics::GetAllActorsWithTag(World, FName("Cover"), FoundCoverPoints);

	TArray<FVector> CoverPositions;
	for (AActor* Cover : FoundCoverPoints)
	{
		if (Cover)
		{
			CoverPositions.Add(Cover->GetActorLocation());
		}
	}

	// If no cover points found, use level geometry as fallback
	// (This could be enhanced with a nav mesh query for nearby walls)
	if (CoverPositions.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MocEQSContext] No cover points found with 'Cover' tag. Place cover actors in level."));
	}

	UEnvQueryItemType_Point::SetContextHelper(ContextData, CoverPositions);
}
