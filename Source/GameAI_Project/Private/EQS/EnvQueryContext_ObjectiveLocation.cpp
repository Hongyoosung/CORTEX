#include "EQS/EnvQueryContext_ObjectiveLocation.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "Team/ObjectiveActor.h"
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

	// Production mode: Get FollowerStateTreeComponent to access shared context
	UFollowerStateTreeComponent* StateTreeComp = QueryOwner->FindComponentByClass<UFollowerStateTreeComponent>();
	if (StateTreeComp)
	{
		// v8.0: Access TargetObjective from shared context (MCTS-assigned)
		FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
		if (SharedContext.TargetObjective)
		{
			// Get objective actor location
			ObjectiveLocation = SharedContext.TargetObjective->GetActorLocation();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("EnvQueryContext_ObjectiveLocation: TargetObjective is NULL in SharedContext for %s"), *QueryOwner->GetName());
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
