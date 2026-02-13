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

	// ===== DIAGNOSTIC LOG: Enemy positions =====
	UE_LOG(LogTemp, Warning, TEXT("[DIAG-EQS-CONTEXT] %s: Found %d visible enemies"), *MocChar->GetName(), EnemyPositions.Num());
	for (int32 i = 0; i < FMath::Min(3, EnemyPositions.Num()); i++)
	{
		FVector Delta = EnemyPositions[i] - MocChar->GetActorLocation();
		UE_LOG(LogTemp, Warning, TEXT("[DIAG-EQS-CONTEXT]   Enemy #%d at: %s (Delta: X=%.0f, Y=%.0f)"),
			i, *EnemyPositions[i].ToString(), Delta.X, Delta.Y);
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

	// ===== DIAGNOSTIC LOG: Ally positions =====
	UE_LOG(LogTemp, Warning, TEXT("[DIAG-EQS-CONTEXT] %s: Found %d allies"), *MocChar->GetName(), AllyPositions.Num());
	for (int32 i = 0; i < FMath::Min(3, AllyPositions.Num()); i++)
	{
		FVector Delta = AllyPositions[i] - MocChar->GetActorLocation();
		UE_LOG(LogTemp, Warning, TEXT("[DIAG-EQS-CONTEXT]   Ally #%d at: %s (Delta: X=%.0f, Y=%.0f)"),
			i, *AllyPositions[i].ToString(), Delta.X, Delta.Y);
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

	// ===== DIAGNOSTIC LOG: Pickups =====
	UE_LOG(LogTemp, Warning, TEXT("[DIAG-EQS-CONTEXT] Pickups: Found %d pickup items"), PickupPositions.Num());

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

	// Get my team ID to determine enemy team
	int32 MyTeamID = MocChar->GetTeamID_Implementation();
	int32 EnemyTeamID = (MyTeamID == 0) ? 1 : 0; // Red = 0, Blue = 1

	// Find enemy team's base capture point
	// Red team base = PointA, Blue team base = PointE
	ECapturePointID EnemyBaseID = (EnemyTeamID == 0) ? ECapturePointID::PointA : ECapturePointID::PointE;

	// ===== DIAGNOSTIC LOG: Enemy objective determination =====
	FVector CharPos = MocChar->GetActorLocation();
	UE_LOG(LogTemp, Warning, TEXT("[DIAG-EQS-CONTEXT] %s (at %s) requesting EnemyObjective: MyTeam=%d, EnemyTeam=%d, TargetPoint=%d"),
		*MocChar->GetName(), *CharPos.ToString(), MyTeamID, EnemyTeamID, static_cast<int32>(EnemyBaseID));

	// Find the specific capture point
	TArray<AActor*> AllCapturePoints;
	UGameplayStatics::GetAllActorsOfClass(World, ACapturePoint::StaticClass(), AllCapturePoints);

	for (AActor* Actor : AllCapturePoints)
	{
		ACapturePoint* CapturePoint = Cast<ACapturePoint>(Actor);
		if (CapturePoint && CapturePoint->PointID == EnemyBaseID)
		{
			FVector ObjectiveLocation = CapturePoint->GetActorLocation();
			FVector Delta = ObjectiveLocation - MocChar->GetActorLocation();
			UE_LOG(LogTemp, Warning, TEXT("[DIAG-EQS-CONTEXT]   ✓ EnemyObjective found at: %s (Delta: X=%.0f, Y=%.0f, Dist=%.0f)"),
				*ObjectiveLocation.ToString(), Delta.X, Delta.Y, Delta.Size2D());
			UEnvQueryItemType_Point::SetContextHelper(ContextData, ObjectiveLocation);
			return;
		}
	}

	// If not found, log warning
	UE_LOG(LogTemp, Error, TEXT("[MocEQSContext] Enemy objective (Point %d) NOT FOUND for team %d"),
		static_cast<int32>(EnemyBaseID), MyTeamID);
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

	// ===== DIAGNOSTIC LOG: Ally objective determination =====
	FVector CharPos = MocChar->GetActorLocation();
	UE_LOG(LogTemp, Warning, TEXT("[DIAG-EQS-CONTEXT] %s (at %s) requesting AllyObjective: MyTeam=%d, TargetPoint=%d"),
		*MocChar->GetName(), *CharPos.ToString(), MyTeamID, static_cast<int32>(AllyBaseID));

	// Find the specific capture point
	TArray<AActor*> AllCapturePoints;
	UGameplayStatics::GetAllActorsOfClass(World, ACapturePoint::StaticClass(), AllCapturePoints);

	for (AActor* Actor : AllCapturePoints)
	{
		ACapturePoint* CapturePoint = Cast<ACapturePoint>(Actor);
		if (CapturePoint && CapturePoint->PointID == AllyBaseID)
		{
			FVector ObjectiveLocation = CapturePoint->GetActorLocation();
			FVector Delta = ObjectiveLocation - MocChar->GetActorLocation();
			UE_LOG(LogTemp, Warning, TEXT("[DIAG-EQS-CONTEXT]   ✓ AllyObjective found at: %s (Delta: X=%.0f, Y=%.0f, Dist=%.0f)"),
				*ObjectiveLocation.ToString(), Delta.X, Delta.Y, Delta.Size2D());
			UEnvQueryItemType_Point::SetContextHelper(ContextData, ObjectiveLocation);
			return;
		}
	}

	// If not found, log warning
	UE_LOG(LogTemp, Error, TEXT("[MocEQSContext] Ally objective (Point %d) NOT FOUND for team %d"),
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

	// ===== DIAGNOSTIC LOG: Cover points =====
	UE_LOG(LogTemp, Warning, TEXT("[DIAG-EQS-CONTEXT] CoverPoints: Found %d cover points"), CoverPositions.Num());

	UEnvQueryItemType_Point::SetContextHelper(ContextData, CoverPositions);
}
