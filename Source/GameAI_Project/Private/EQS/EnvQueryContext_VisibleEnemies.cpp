#include "EQS/EnvQueryContext_VisibleEnemies.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "GameFramework/Pawn.h"

void UEnvQueryContext_VisibleEnemies::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
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

	// Access VisibleEnemies from shared context
	FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
	if (SharedContext.VisibleEnemies.Num() == 0)
	{
		// No visible enemies
		return;
	}

	// Collect enemy locations
	TArray<FVector> EnemyLocations;
	for (AActor* Enemy : SharedContext.VisibleEnemies)
	{
		if (Enemy && IsValid(Enemy))
		{
			EnemyLocations.Add(Enemy->GetActorLocation());
		}
	}

	// Set enemy locations as context data
	if (EnemyLocations.Num() > 0)
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, EnemyLocations);
	}
}
