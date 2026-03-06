// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/EQS/MocEQSContext.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "AIController.h"
#include "Characters/MocCharacter.h"
#include "EngineUtils.h"
#include "Actors/CapturePoint.h"
#include "Kismet/GameplayStatics.h"

#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"

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
	AAIController* AIC = Cast<AAIController>(QueryOwner);

	if (!MocChar || !AIC)
	{
		// Try to get from controller if not directly possessed
		if (AIC)
		{
			MocChar = Cast<AMocCharacter>(AIC->GetPawn());
		}

		if (!MocChar)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MocEQSContext] Query owner is not a MocCharacter or AIController: %s"), *QueryOwner->GetName())

			return;
		}
	}


	// 퍼셉션 컴포넌트 가져오기
	UAIPerceptionComponent* PerceptionComp = AIC->GetAIPerceptionComponent();
	if (!PerceptionComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MocEQSContext] %s: AIPerceptionComponent is missing on AIController!"), *MocChar->GetName());
		return;
	}

	const int32 MyTeamID = MocChar->GetTeamID_Implementation();
	const int32 MyEnvID = MocChar->GetEnvID_Implementation();
	TArray<FVector> EnemyPositions;

	// 시각(Sight)으로 인지된 모든 액터 가져오기
	TArray<AActor*> PerceivedActors;
	PerceptionComp->GetKnownPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);

	for (AActor* Actor : PerceivedActors)
	{
		AMocCharacter* PerceivedChar = Cast<AMocCharacter>(Actor);

		// 1. 대상이 MocCharacter이고, 살아있으며, 적팀이고, 같은 환경(Env)에 있는지 확인
		if (PerceivedChar &&
			PerceivedChar->IsAlive_Implementation() &&
			PerceivedChar->GetTeamID_Implementation() != MyTeamID &&
			PerceivedChar->GetEnvID_Implementation() == MyEnvID)
		{
			// 2. 현재 시야에 실제로 보이는지 확인 (기억 속에만 있는 위치는 제외)
			FActorPerceptionBlueprintInfo PerceptionInfo;
			PerceptionComp->GetActorsPerception(Actor, PerceptionInfo);

			for (const FAIStimulus& Stimulus : PerceptionInfo.LastSensedStimuli)
			{
				// 시각 정보이며, 성공적으로 인지 중(WasSuccessfullySensed)인 경우에만 추가
				if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>() && Stimulus.WasSuccessfullySensed())
				{
					EnemyPositions.Add(PerceivedChar->GetActorLocation());
					break; // 한 번 확인했으면 다음 액터로 넘어감
				}
			}
		}
	}

	UEnvQueryItemType_Point::SetContextHelper(ContextData, EnemyPositions);
}

void UEnvQueryContext_MocAllies::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner) return;

	AMocCharacter* MocChar = Cast<AMocCharacter>(QueryOwner);
	if (!MocChar)
	{
		AAIController* AIC = Cast<AAIController>(QueryOwner);
		if (AIC)
		{
			MocChar = Cast<AMocCharacter>(AIC->GetPawn());
		}
	}

	if (!MocChar) return;

	UWorld* World = QueryOwner->GetWorld();
	if (!World) return;

	const int32 MyTeamID = MocChar->GetTeamID_Implementation();
	const int32 MyEnvID = MocChar->GetEnvID_Implementation();
	TArray<FVector> AllyPositions;

	// MocCharacter를 순회하여 같은 팀 찾기
	for (TActorIterator<AMocCharacter> It(World); It; ++It)
	{
		AMocCharacter* Ally = *It;

		// 본인이 아니며, 살아있고, 같은 팀이며, 같은 환경인 경우 수집
		if (Ally && Ally != MocChar &&
			Ally->IsAlive_Implementation() &&
			Ally->GetTeamID_Implementation() == MyTeamID &&
			Ally->GetEnvID_Implementation() == MyEnvID)
		{
			AllyPositions.Add(Ally->GetActorLocation());
		}
	}

	UEnvQueryItemType_Point::SetContextHelper(ContextData, AllyPositions);
}


void UEnvQueryContext_MocCapturePoints::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AMocCharacter* MocChar = Cast<AMocCharacter>(QueryInstance.Owner.Get());
	if (!MocChar)
	{
		AAIController* AIC = Cast<AAIController>(QueryInstance.Owner.Get());
		if (AIC) MocChar = Cast<AMocCharacter>(AIC->GetPawn());
	}

	if (!MocChar) return;

	TArray<FVector> PointPositions;
	for (ACapturePoint* CP : MocChar->AssignedCapturePoints)
	{
		if (CP) PointPositions.Add(CP->GetActorLocation());
	}

	UEnvQueryItemType_Point::SetContextHelper(ContextData, PointPositions);
}


void UEnvQueryContext_MocEnemyObjective::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AMocCharacter* MocChar = Cast<AMocCharacter>(QueryInstance.Owner.Get());
	if (!MocChar)
	{
		AAIController* AIC = Cast<AAIController>(QueryInstance.Owner.Get());
		if (AIC) MocChar = Cast<AMocCharacter>(AIC->GetPawn());
	}

	if (!MocChar) return;

	const int32 MyTeamID = MocChar->GetTeamID_Implementation();
	const FVector AgentPos = MocChar->GetActorLocation();

	ACapturePoint* NearestNonFriendly = nullptr;
	float NearestDist = FLT_MAX;


	for (ACapturePoint* CP : MocChar->AssignedCapturePoints)
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
}


void UEnvQueryContext_MocAllyObjective::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AMocCharacter* MocChar = Cast<AMocCharacter>(QueryInstance.Owner.Get());
	if (!MocChar)
	{
		AAIController* AIC = Cast<AAIController>(QueryInstance.Owner.Get());
		if (AIC) MocChar = Cast<AMocCharacter>(AIC->GetPawn());
	}

	if (!MocChar) return;

	const int32 MyTeamID = MocChar->GetTeamID_Implementation();
	ECapturePointID AllyBaseID = (MyTeamID == 0) ? ECapturePointID::PointA : ECapturePointID::PointE;

	for (ACapturePoint* CP : MocChar->AssignedCapturePoints)
	{
		if (CP && CP->PointID == AllyBaseID)
		{
			UEnvQueryItemType_Point::SetContextHelper(ContextData, CP->GetActorLocation());
			return;
		}
	}
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
