#include "EQS/EnvQueryContext_ObjectiveLocation.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "Team/ObjectiveActor.h"
#include "Team/Components/TeamLeaderComponent.h"    
#include "Team/Components/TeamCommsComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

void UEnvQueryContext_ObjectiveLocation::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner)
	{
		return;
	}

	FVector ObjectiveLocation = FVector::ZeroVector;

	// v9.0 FIXED: Determine objective from strategy, not SharedContext.TargetObjective
	// MCTS no longer assigns objectives - reward functions encode objectives implicitly
	UFollowerStateTreeComponent* StateTreeComp = QueryOwner->FindComponentByClass<UFollowerStateTreeComponent>();
	if (StateTreeComp)
	{
		FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
		EStrategyType Strategy = SharedContext.AssignedStrategy;

		UTeamCommsComponent* CommsComp = QueryOwner->FindComponentByClass<UTeamCommsComponent>();

		if (CommsComp)
		{
			UTeamLeaderComponent* TeamLeader = CommsComp->GetTeamLeader();
			if (TeamLeader)
			{
				AObjectiveActor* TargetObjective = nullptr;

				// Strategy-based objective selection (v9.0)
				switch (Strategy)
				{
					case EStrategyType::Assault:
						// Assault focuses on approaching hostile objective
						TargetObjective = TeamLeader->GetHostileObjective();
						break;

					case EStrategyType::Defend:
						// Defend focuses on staying near friendly objective
						TargetObjective = TeamLeader->GetFriendlyObjective();
						break;

					case EStrategyType::Support:
						// Support doesn't use objective context (ally proximity dominates)
						UE_LOG(LogTemp, Verbose, TEXT("[EQS CONTEXT v9.0] %s: Support strategy - no objective context"), *QueryOwner->GetName());
						break;

					case EStrategyType::Retreat:
						// Retreat doesn't use objective context (enemy avoidance dominates)
						UE_LOG(LogTemp, Verbose, TEXT("[EQS CONTEXT v9.0] %s: Retreat strategy - no objective context"), *QueryOwner->GetName());
						break;

					default:
						UE_LOG(LogTemp, Warning, TEXT("[EQS CONTEXT v9.0] %s: Unknown strategy %s"),
							*QueryOwner->GetName(), *UEnum::GetValueAsString(Strategy));
						break;
				}

				if (TargetObjective)
				{
					ObjectiveLocation = TargetObjective->GetActorLocation();

					// v9.0: Periodic logging for verification
					static TMap<const UEnvQueryContext_ObjectiveLocation*, int32> LogCounts;
					int32& LogCount = LogCounts.FindOrAdd(this, 0);
					if (++LogCount % 200 == 0)
					{
						UE_LOG(LogTemp, Display, TEXT("✅ [EQS CONTEXT v9.0] %s (%s) → Objective=%s at %s"),
							*QueryOwner->GetName(),
							*UEnum::GetValueAsString(Strategy),
							*TargetObjective->GetName(),
							*ObjectiveLocation.ToCompactString());
					}
				}
			}
			else
			{
				static int32 NoLeaderCount = 0;
				if (++NoLeaderCount % 100 == 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("[EQS CONTEXT v9.0] %s: No TeamLeader (count=%d)"), *QueryOwner->GetName(), NoLeaderCount);
				}
			}
		}
		else
		{
			static int32 NoFollowerCount = 0;
			if (++NoFollowerCount % 100 == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[EQS CONTEXT v9.0] %s: No FollowerAgentComponent (count=%d)"), *QueryOwner->GetName(), NoFollowerCount);
			}
		}
	}
	else
	{
		// Testing mode: Fallback to finding ObjectiveActor by tag or class (for EQS Testing Pawn)
		UE_LOG(LogTemp, Log, TEXT("EnvQueryContext_ObjectiveLocation: No StateTreeComponent on %s, using test fallback"), *QueryOwner->GetName());

		// First try finding by tag "Objective"
		TArray<AActor*> FoundObjectives;
		UGameplayStatics::GetAllActorsWithTag(QueryOwner->GetWorld(), FName("Objective"), FoundObjectives);

		if (FoundObjectives.Num() > 0)
		{
			// Use nearest objective
			float MinDist = MAX_FLT;
			for (AActor* Obj : FoundObjectives)
			{
				if (Obj)
				{
					float Dist = FVector::Dist(QueryOwner->GetActorLocation(), Obj->GetActorLocation());
					if (Dist < MinDist)
					{
						MinDist = Dist;
						ObjectiveLocation = Obj->GetActorLocation();
					}
				}
			}
		}
		else
		{
			// Fallback: Find any ObjectiveActor in world
			TArray<AActor*> AllObjectives;
			UGameplayStatics::GetAllActorsOfClass(QueryOwner->GetWorld(), AObjectiveActor::StaticClass(), AllObjectives);
			if (AllObjectives.Num() > 0)
			{
				ObjectiveLocation = AllObjectives[0]->GetActorLocation();
			}
		}
	}

	// Set objective location as context data
	if (!ObjectiveLocation.IsNearlyZero())
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, ObjectiveLocation);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("EnvQueryContext_ObjectiveLocation: No valid objective location found for %s"), *QueryOwner->GetName());
	}
}
