#include "EQS/EnvQueryContext_Teammates.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "Team/FollowerAgentComponent.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"

void UEnvQueryContext_Teammates::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	// Get querier actor
	UObject* QuerierObject = QueryInstance.Owner.Get();
	if (!QuerierObject)
	{
		return;
	}

	AActor* QuerierActor = Cast<AActor>(QuerierObject);
	if (!QuerierActor)
	{
		return;
	}

	// Get FollowerAgentComponent to determine team
	UFollowerAgentComponent* AgentComp = QuerierActor->FindComponentByClass<UFollowerAgentComponent>();
	if (!AgentComp)
	{
		// Fallback: EQS Testing Pawn mode (search by tag)
		UE_LOG(LogTemp, Warning, TEXT("[EQS Context] No FollowerAgentComponent found on %s, using tag-based fallback"), *QuerierActor->GetName());

		TArray<AActor*> FoundActors;
		for (TActorIterator<AActor> It(QuerierActor->GetWorld()); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && Actor != QuerierActor && Actor->ActorHasTag(TEXT("Teammate")))
			{
				FoundActors.Add(Actor);
			}
		}

		UEnvQueryItemType_Actor::SetContextHelper(ContextData, FoundActors);
		return;
	}

	// Production mode: Get teammates from team leader's registered agents
	int32 MyTeamID = AgentComp->GetTeamID();
	TArray<AActor*> Teammates;

	// Find all agents on the same team
	for (TActorIterator<APawn> It(QuerierActor->GetWorld()); It; ++It)
	{
		APawn* OtherPawn = *It;
		if (!OtherPawn || OtherPawn == QuerierActor)
		{
			continue;
		}

		UFollowerAgentComponent* OtherAgentComp = OtherPawn->FindComponentByClass<UFollowerAgentComponent>();
		if (OtherAgentComp && OtherAgentComp->GetTeamID() == MyTeamID)
		{
			Teammates.Add(OtherPawn);
		}
	}

	UEnvQueryItemType_Actor::SetContextHelper(ContextData, Teammates);

	UE_LOG(LogTemp, VeryVerbose, TEXT("[EQS Context] Found %d teammates for %s (TeamID: %d)"),
		Teammates.Num(), *QuerierActor->GetName(), MyTeamID);
}
