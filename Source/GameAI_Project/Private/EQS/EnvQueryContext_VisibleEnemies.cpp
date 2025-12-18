#include "EQS/EnvQueryContext_VisibleEnemies.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

void UEnvQueryContext_VisibleEnemies::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QueryOwner)
	{
		return;
	}

	TArray<FVector> EnemyLocations;

	// Production mode: Get FollowerStateTreeComponent to access shared context
	UFollowerStateTreeComponent* StateTreeComp = QueryOwner->FindComponentByClass<UFollowerStateTreeComponent>();
	if (StateTreeComp)
	{
		// Access VisibleEnemies from shared context
		FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
		for (AActor* Enemy : SharedContext.VisibleEnemies)
		{
			if (Enemy && IsValid(Enemy))
			{
				EnemyLocations.Add(Enemy->GetActorLocation());
			}
		}
	}
	else
	{
		// Testing mode: Fallback to finding actors by tag (for EQS Testing Pawn)
		TArray<AActor*> FoundEnemies;
		UGameplayStatics::GetAllActorsWithTag(QueryOwner->GetWorld(), FName("Enemy"), FoundEnemies);
		for (AActor* Enemy : FoundEnemies)
		{
			if (Enemy && IsValid(Enemy))
			{
				EnemyLocations.Add(Enemy->GetActorLocation());
			}
		}
	}

	// Set enemy locations as context data
	if (EnemyLocations.Num() > 0)
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, EnemyLocations);
	}
}
