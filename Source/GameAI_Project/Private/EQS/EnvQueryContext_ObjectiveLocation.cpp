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
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("EnvQueryContext_ObjectiveLocation: No StateTreeComponent found on %s"), *QueryOwner->GetName());
	}

	// Set objective location as context data
	if (!ObjectiveLocation.IsNearlyZero())
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, ObjectiveLocation);
	}
}
