#include "EQS/EnvQueryContext_ObjectiveLocation.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "Team/Objective.h"
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
		// Access CurrentObjective from shared context
		FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
		if (SharedContext.CurrentObjective)
		{
			// Get objective target location
			ObjectiveLocation = SharedContext.CurrentObjective->TargetLocation;

			// Fallback: If TargetLocation is zero, try TargetActor location
			if (ObjectiveLocation.IsNearlyZero() && SharedContext.CurrentObjective->TargetActor)
			{
				ObjectiveLocation = SharedContext.CurrentObjective->TargetActor->GetActorLocation();
			}
		}
	}
	else
	{
		// Testing mode: Fallback to finding actor by tag (for EQS Testing Pawn)
		TArray<AActor*> FoundObjectives;
		UGameplayStatics::GetAllActorsWithTag(QueryOwner->GetWorld(), FName("Objective"), FoundObjectives);
		if (FoundObjectives.Num() > 0 && IsValid(FoundObjectives[0]))
		{
			ObjectiveLocation = FoundObjectives[0]->GetActorLocation();
		}
	}

	// Set objective location as context data
	if (!ObjectiveLocation.IsNearlyZero())
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, ObjectiveLocation);
	}
}
