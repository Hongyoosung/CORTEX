#include "EQS/EnvQueryContext_ObjectiveLocation.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "Team/Objective.h"
#include "GameFramework/Pawn.h"

void UEnvQueryContext_ObjectiveLocation::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner)
	{
		return;
	}

	// Get FollowerStateTreeComponent to access shared context
	UFollowerStateTreeComponent* StateTreeComp = QueryOwner->FindComponentByClass<UFollowerStateTreeComponent>();
	if (!StateTreeComp)
	{
		return;
	}

	// Access CurrentObjective from shared context
	FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
	if (!SharedContext.CurrentObjective)
	{
		// No objective assigned
		return;
	}

	// Get objective target location
	FVector ObjectiveLocation = SharedContext.CurrentObjective->TargetLocation;

	// Fallback: If TargetLocation is zero, try TargetActor location
	if (ObjectiveLocation.IsNearlyZero() && SharedContext.CurrentObjective->TargetActor)
	{
		ObjectiveLocation = SharedContext.CurrentObjective->TargetActor->GetActorLocation();
	}

	// Set objective location as context data
	if (!ObjectiveLocation.IsNearlyZero())
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, ObjectiveLocation);
	}
}
