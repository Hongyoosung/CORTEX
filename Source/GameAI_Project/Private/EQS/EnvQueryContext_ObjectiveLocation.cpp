#include "EQS/EnvQueryContext_ObjectiveLocation.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "Team/Mission.h"
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
		// Access CurrentMission from shared context
		FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
		if (SharedContext.CurrentMission)
		{
			// Get objective target location
			ObjectiveLocation = SharedContext.CurrentMission->TargetLocation;

			// Fallback: If TargetLocation is zero, try TargetActor location
			if (ObjectiveLocation.IsNearlyZero() && SharedContext.CurrentMission->TargetActor)
			{
				ObjectiveLocation = SharedContext.CurrentMission->TargetActor->GetActorLocation();
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed StateTreeComp check"))
	}

	// Set objective location as context data
	if (!ObjectiveLocation.IsNearlyZero())
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, ObjectiveLocation);
	}
}
