// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/EQS/MocEQSContext.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "AIController.h"
#include "Characters/MocCharacter.h"
#include "Team/TeamManager.h"
#include "Schola/ScholaEnvironment.h"
#include "EngineUtils.h"
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

	// Get TeamManager from character (multi-env safe) or ScholaEnvironment fallback
	ATeamManager* TeamManager = MocChar->GetTeamManager();
	if (!TeamManager)
	{
		for (TActorIterator<AScholaEnvironment> It(World); It; ++It)
		{
			if ((*It)->GetEnvId() == MocChar->GetEnvID_Implementation())
			{
				TeamManager = (*It)->GetTeamManager();
				break;
			}
		}
	}
	if (!TeamManager)
	{
		return;
	}

	int32 MyTeamID = MocChar->GetTeamID_Implementation();
	const FVector MyLocation = MocChar->GetActorLocation();

	// Get enemy positions — scoped to this env via TeamManager, filtered by direct line-of-sight
	TArray<FVector> EnemyPositions;
	TArray<AMocCharacter*> EnemyAgents = TeamManager->GetEnemyAgents(MyTeamID);

	for (AMocCharacter* Enemy : EnemyAgents)
	{
		if (!Enemy || !Enemy->IsAlive_Implementation())
		{
			continue;
		}

		const float Distance = FVector::Dist(MyLocation, Enemy->GetActorLocation());
		if (Distance >= 8000.0f)
		{
			continue;
		}

		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(MocChar);
		QueryParams.AddIgnoredActor(Enemy);

		const bool bBlocked = World->LineTraceSingleByChannel(
			HitResult,
			MyLocation + FVector(0, 0, 90),
			Enemy->GetActorLocation() + FVector(0, 0, 90),
			ECC_Visibility,
			QueryParams
		);

		if (!bBlocked)
		{
			EnemyPositions.Add(Enemy->GetActorLocation());
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

	// Get TeamManager from character (multi-env safe) or ScholaEnvironment fallback
	ATeamManager* TeamManager = MocChar->GetTeamManager();
	if (!TeamManager)
	{
		for (TActorIterator<AScholaEnvironment> It(World); It; ++It)
		{
			if ((*It)->GetEnvId() == MocChar->GetEnvID_Implementation())
			{
				TeamManager = (*It)->GetTeamManager();
				break;
			}
		}
	}
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

	// Resolve querier's EnvID for multi-env isolation
	AMocCharacter* MocChar = Cast<AMocCharacter>(QueryOwner);
	if (!MocChar)
	{
		AAIController* AIC = Cast<AAIController>(QueryOwner);
		if (AIC)
		{
			MocChar = Cast<AMocCharacter>(AIC->GetPawn());
		}
	}
	const int32 EnvID = MocChar ? MocChar->GetEnvID_Implementation() : -1;

	TArray<FVector> PointPositions;
	for (TActorIterator<ACapturePoint> It(World); It; ++It)
	{
		ACapturePoint* CP = *It;
		if (CP && (EnvID == -1 || CP->GetEnvID_Implementation() == EnvID))
		{
			PointPositions.Add(CP->GetActorLocation());
		}
	}

	UEnvQueryItemType_Point::SetContextHelper(ContextData, PointPositions);
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

	const int32 MyEnvID = MocChar->GetEnvID_Implementation();
	const int32 MyTeamID = MocChar->GetTeamID_Implementation();
	FVector AgentPos = MocChar->GetActorLocation();

	AScholaEnvironment* MyEnv = nullptr;
	for (TActorIterator<AScholaEnvironment> It(GetWorld()); It; ++It)
	{
		if ((*It)->GetEnvId() == MyEnvID)
		{
			MyEnv = *It;
			break;
		}
	}

	if (!MyEnv) return;

	// 환경이 소유한 거점들 중에서만 탐색
	TArray<ACapturePoint*> EnvPoints = MyEnv->GetAllCapturePoints();

	ACapturePoint* NearestNonFriendly = nullptr;
	float NearestDist = FLT_MAX;

	// Enum 매핑 주의: CP->GetOwningTeamID()가 에이전트의 TeamID 체계와 맞는지 반드시 확인 필요
	for (ACapturePoint* CP : EnvPoints)
	{
		if (!CP || CP->GetOwningTeamID() == MyTeamID) continue;

		const float Dist = FVector::Dist(AgentPos, CP->GetActorLocation());
		if (Dist < NearestDist)
		{
			NearestDist = Dist;
			NearestNonFriendly = CP;
		}
	}

	if (NearestNonFriendly)
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, NearestNonFriendly->GetActorLocation());
	}
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

	// Find the specific capture point — scoped to this env via EnvID
	for (TActorIterator<ACapturePoint> It(World); It; ++It)
	{
		ACapturePoint* CapturePoint = *It;
		if (CapturePoint && CapturePoint->GetEnvID_Implementation() == MocChar->GetEnvID_Implementation() && CapturePoint->PointID == AllyBaseID)
		{
			UEnvQueryItemType_Point::SetContextHelper(ContextData, CapturePoint->GetActorLocation());
			return;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("[MocEQSContext] Ally objective not found (Point %d) for team %d (EnvID %d)"),
		static_cast<int32>(AllyBaseID), MyTeamID, MocChar->GetEnvID_Implementation());
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
