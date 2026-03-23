// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/EQS/DEEQSContext.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"

#include "Characters/DEAgent.h"
#include "Actors/DECapturePoint.h"
#include "Team/DEMatchManager.h"

void UEnvQueryContext_DEQuerier::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner)
	{
		return;
	}

	// Provide querier location
	UEnvQueryItemType_Point::SetContextHelper(ContextData, QueryOwner->GetActorLocation());
}

void UEnvQueryContext_DEEnemies::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner)
	{
		return;
	}

	ADEAgent* DEChar = Cast<ADEAgent>(QueryOwner);
	AAIController* AIC = Cast<AAIController>(QueryOwner);

	// Owner may be ADEAgent (training) or AAIController (inference/BT)
	if (!DEChar && !AIC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEEQSContext] Query owner is not a DEAgent or AIController: %s"), *QueryOwner->GetName());
		return;
	}

	// Resolve the missing side
	if (!DEChar && AIC)
	{
		DEChar = Cast<ADEAgent>(AIC->GetPawn());
	}
	if (!AIC && DEChar)
	{
		AIC = Cast<AAIController>(DEChar->GetController());
	}

	if (!DEChar)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEEQSContext] DEEnemies: Could not resolve DEAgent from owner %s"), *QueryOwner->GetName());
		return;
	}

	const int32 MyTeamID = DEChar->GetTeamID_Implementation();
	const int32 MyEnvID = DEChar->GetEnvID_Implementation();
	TArray<FVector> EnemyPositions;

	UAIPerceptionComponent* PerceptionComp = AIC ? AIC->GetAIPerceptionComponent() : nullptr;
	if (PerceptionComp)
	{
		// Runtime path: use AIPerceptionComponent (inference / BT)
		TArray<AActor*> PerceivedActors;
		PerceptionComp->GetKnownPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);

		for (AActor* Actor : PerceivedActors)
		{
			ADEAgent* PerceivedChar = Cast<ADEAgent>(Actor);

			if (PerceivedChar &&
				PerceivedChar->IsAlive() &&
				PerceivedChar->GetTeamID_Implementation() != MyTeamID &&
				PerceivedChar->GetEnvID_Implementation() == MyEnvID)
			{
				FActorPerceptionBlueprintInfo PerceptionInfo;
				PerceptionComp->GetActorsPerception(Actor, PerceptionInfo);

				for (const FAIStimulus& Stimulus : PerceptionInfo.LastSensedStimuli)
				{
					if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>() && Stimulus.WasSuccessfullySensed())
					{
						EnemyPositions.Add(PerceivedChar->GetActorLocation());
						break;
					}
				}
			}
		}
	}
	else
	{
		// Training path: AIController is possessed by Trainer — fall back to manual LOS check
		UWorld* World = DEChar->GetWorld();
		if (!World) return;

		const FVector MyLocation = DEChar->GetActorLocation();
		const float VisionRangeSq = FMath::Square(DEChar->VisionRange);

		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(DEEnemiesLOS), true, DEChar);

		for (TActorIterator<ADEAgent> It(World); It; ++It)
		{
			ADEAgent* Candidate = *It;

			if (!Candidate ||
				Candidate == DEChar ||
				!Candidate->IsAlive() ||
				Candidate->GetTeamID_Implementation() == MyTeamID ||
				Candidate->GetEnvID_Implementation() != MyEnvID)
			{
				continue;
			}

			const FVector CandidateLocation = Candidate->GetActorLocation();
			if (FVector::DistSquared(MyLocation, CandidateLocation) > VisionRangeSq)
			{
				continue;
			}

			FHitResult Hit;
			const bool bBlocked = World->LineTraceSingleByChannel(
				Hit, MyLocation, CandidateLocation, ECC_Visibility, TraceParams);

			if (!bBlocked || Hit.GetActor() == Candidate)
			{
				EnemyPositions.Add(CandidateLocation);
			}
		}
	}

	UEnvQueryItemType_Point::SetContextHelper(ContextData, EnemyPositions);
}

void UEnvQueryContext_DEAllies::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner) return;

	ADEAgent* DEChar = Cast<ADEAgent>(QueryOwner);
	if (!DEChar)
	{
		AAIController* AIC = Cast<AAIController>(QueryOwner);
		if (AIC)
		{
			DEChar = Cast<ADEAgent>(AIC->GetPawn());
		}
	}

	if (!DEChar) return;

	UWorld* World = QueryOwner->GetWorld();
	if (!World) return;

	const int32 MyTeamID = DEChar->GetTeamID_Implementation();
	const int32 MyEnvID = DEChar->GetEnvID_Implementation();
	TArray<FVector> AllyPositions;

	// DECharacter를 순회하여 같은 팀 찾기
	for (TActorIterator<ADEAgent> It(World); It; ++It)
	{
		ADEAgent* Ally = *It;

		// 본인이 아니며, 살아있고, 같은 팀이며, 같은 환경인 경우 수집
		if (Ally && Ally != DEChar &&
			Ally->IsAlive() &&
			Ally->GetTeamID_Implementation() == MyTeamID &&
			Ally->GetEnvID_Implementation() == MyEnvID)
		{
			AllyPositions.Add(Ally->GetActorLocation());
		}
	}


	UEnvQueryItemType_Point::SetContextHelper(ContextData, AllyPositions);
}


void UEnvQueryContext_DECapturePoints::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	ADEAgent* DEChar = Cast<ADEAgent>(QueryInstance.Owner.Get());
	if (!DEChar)
	{
		AAIController* AIC = Cast<AAIController>(QueryInstance.Owner.Get());
		if (AIC) DEChar = Cast<ADEAgent>(AIC->GetPawn());
	}

	if (!DEChar) return;

	TArray<FVector> PointPositions;
	for (ADECapturePoint* CP : DEChar->AssignedCapturePoints)
	{
		if (CP) PointPositions.Add(CP->GetActorLocation());
	}

	UEnvQueryItemType_Point::SetContextHelper(ContextData, PointPositions);
}


void UEnvQueryContext_DEEnemyObjective::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	

	ADEAgent* DEChar = Cast<ADEAgent>(QueryInstance.Owner.Get());
	if (!DEChar)
	{
		AAIController* AIC = Cast<AAIController>(QueryInstance.Owner.Get());
		if (AIC) DEChar = Cast<ADEAgent>(AIC->GetPawn());
	}

	if (!DEChar)
	{
		UE_LOG(LogTemp, Warning, TEXT("qqqqqqqqqqqqqqqqqqqq"));
		return;
	}

	const int32 MyTeamID = DEChar->GetTeamID_Implementation();
	const FVector AgentPos = DEChar->GetActorLocation();

	ADECapturePoint* NearestNonFriendly = nullptr;
	float NearestDist = FLT_MAX;


	for (ADECapturePoint* CP : DEChar->AssignedCapturePoints)
	{
		if (!CP || CP->GetTeamID_Implementation() == MyTeamID) continue;

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
	else
	{
		// All points owned by this team — fall back to nearest friendly point (defend posture)
		ADECapturePoint* NearestFriendly = nullptr;
		float NearestFriendlyDist = FLT_MAX;
		for (ADECapturePoint* CP : DEChar->AssignedCapturePoints)
		{
			if (!CP) continue;
			const float Dist = FVector::Dist(AgentPos, CP->GetActorLocation());
			if (Dist < NearestFriendlyDist)
			{
				NearestFriendlyDist = Dist;
				NearestFriendly = CP;
			}
		}
		if (NearestFriendly)
		{
			UEnvQueryItemType_Point::SetContextHelper(ContextData, NearestFriendly->GetActorLocation());
		}
	}
}


void UEnvQueryContext_DEAllyObjective::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	ADEAgent* DEChar = Cast<ADEAgent>(QueryInstance.Owner.Get());
	if (!DEChar)
	{
		AAIController* AIC = Cast<AAIController>(QueryInstance.Owner.Get());
		if (AIC) DEChar = Cast<ADEAgent>(AIC->GetPawn());
	}

	if (!DEChar) return;

	const int32 MyTeamID = DEChar->GetTeamID_Implementation();
	ECapturePointID AllyBaseID = (MyTeamID == 0) ? ECapturePointID::PointA : ECapturePointID::PointE;

	for (ADECapturePoint* CP : DEChar->AssignedCapturePoints)
	{
		if (CP && CP->PointID == AllyBaseID)
		{
			UEnvQueryItemType_Point::SetContextHelper(ContextData, CP->GetActorLocation());
			return;
		}
	}
}


void UEnvQueryContext_DEAssignedBase::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	ADEAgent* DEChar = Cast<ADEAgent>(QueryInstance.Owner.Get());
	if (!DEChar)
	{
		AAIController* AIC = Cast<AAIController>(QueryInstance.Owner.Get());
		if (AIC) DEChar = Cast<ADEAgent>(AIC->GetPawn());
	}

	if (!DEChar) return;

	// Read AssignedBaseIndex from Blackboard
	int32 AssignedIndex = DEChar->AssignedBaseIndex;

	AAIController* AIC = Cast<AAIController>(DEChar->GetController());
	if (AIC)
	{
		if (UBlackboardComponent* BB = AIC->GetBlackboardComponent())
		{
			AssignedIndex = BB->GetValueAsInt(TEXT("AssignedBaseIndex"));
		}
	}

	// Look up the capture point position from MatchManager
	ADEMatchManager* MatchMgr = DEChar->GetMatchManager();
	if (!MatchMgr) return;

	const TArray<ADECapturePoint*>& CPs = MatchMgr->GetCapturePoints();
	if (CPs.IsValidIndex(AssignedIndex) && CPs[AssignedIndex])
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, CPs[AssignedIndex]->GetActorLocation());
	}

}


void UEnvQueryContext_DECombatRange::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	ADEAgent* DEChar = Cast<ADEAgent>(QueryInstance.Owner.Get());
	if (!DEChar)
	{
		AAIController* AIC = Cast<AAIController>(QueryInstance.Owner.Get());
		if (AIC) DEChar = Cast<ADEAgent>(AIC->GetPawn());
	}
	if (!DEChar) return;

	const int32 MyTeamID = DEChar->GetTeamID_Implementation();
	const int32 MyEnvID  = DEChar->GetEnvID_Implementation();
	const FVector MyLocation = DEChar->GetActorLocation();

	ADEAgent* NearestEnemy = nullptr;
	float NearestDistSq = FLT_MAX;

	AAIController* AIC = Cast<AAIController>(DEChar->GetController());
	UAIPerceptionComponent* PerceptionComp = AIC ? AIC->GetAIPerceptionComponent() : nullptr;

	if (PerceptionComp)
	{
		// Runtime path: use perception system
		TArray<AActor*> PerceivedActors;
		PerceptionComp->GetKnownPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);

		for (AActor* Actor : PerceivedActors)
		{
			ADEAgent* Candidate = Cast<ADEAgent>(Actor);
			if (!Candidate || !Candidate->IsAlive() ||
				Candidate->GetTeamID_Implementation() == MyTeamID ||
				Candidate->GetEnvID_Implementation()  != MyEnvID)
			{
				continue;
			}

			FActorPerceptionBlueprintInfo Info;
			PerceptionComp->GetActorsPerception(Actor, Info);
			for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
			{
				if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>() && Stimulus.WasSuccessfullySensed())
				{
					const float DistSq = FVector::DistSquared(MyLocation, Candidate->GetActorLocation());
					if (DistSq < NearestDistSq)
					{
						NearestDistSq = DistSq;
						NearestEnemy  = Candidate;
					}
					break;
				}
			}
		}
	}
	else
	{
		// Training path: manual LOS check
		UWorld* World = DEChar->GetWorld();
		if (!World) return;

		const float VisionRangeSq = FMath::Square(DEChar->VisionRange);
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(DECombatRangeLOS), true, DEChar);

		for (TActorIterator<ADEAgent> It(World); It; ++It)
		{
			ADEAgent* Candidate = *It;
			if (!Candidate || Candidate == DEChar || !Candidate->IsAlive() ||
				Candidate->GetTeamID_Implementation() == MyTeamID ||
				Candidate->GetEnvID_Implementation()  != MyEnvID)
			{
				continue;
			}

			const FVector CandidateLocation = Candidate->GetActorLocation();
			const float DistSq = FVector::DistSquared(MyLocation, CandidateLocation);
			if (DistSq > VisionRangeSq || DistSq >= NearestDistSq) continue;

			FHitResult Hit;
			const bool bBlocked = World->LineTraceSingleByChannel(
				Hit, MyLocation, CandidateLocation, ECC_Visibility, TraceParams);

			if (!bBlocked || Hit.GetActor() == Candidate)
			{
				NearestDistSq = DistSq;
				NearestEnemy  = Candidate;
			}
		}
	}

	if (NearestEnemy)
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, NearestEnemy->GetActorLocation());
	}
}


void UEnvQueryContext_DECoverPoints::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
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


	UEnvQueryItemType_Point::SetContextHelper(ContextData, CoverPositions);
}
