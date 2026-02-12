// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BT/Services/BTService_UpdateCombatTarget.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Combat/CombatStatsInterface.h"
#include "Team/MocTeamInterface.h"
#include "GameFramework/Pawn.h"

UBTService_UpdateCombatTarget::UBTService_UpdateCombatTarget()
{
	NodeName = "Update Combat Target";
	Interval = 0.5f;
	RandomDeviation = 0.1f;

	// Setup default blackboard keys
	TargetEnemyKey.SelectedKeyName = "TargetEnemy";
	HasTargetKey.SelectedKeyName = "HasTarget";
}

void UBTService_UpdateCombatTarget::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	// Get blackboard component
	UBlackboardComponent* BlackboardComp = OwnerComp.GetBlackboardComponent();
	if (!BlackboardComp)
	{
		return;
	}

	// Find best target
	AActor* BestTarget = FindBestTarget(OwnerComp);

	// Get current target
	AActor* CurrentTarget = Cast<AActor>(BlackboardComp->GetValueAsObject(TargetEnemyKey.SelectedKeyName));

	// Update blackboard if target changed
	if (BestTarget != CurrentTarget)
	{
		BlackboardComp->SetValueAsObject(TargetEnemyKey.SelectedKeyName, BestTarget);
		BlackboardComp->SetValueAsBool(HasTargetKey.SelectedKeyName, BestTarget != nullptr);

		if (BestTarget)
		{
			UE_LOG(LogBehaviorTree, Log, TEXT("Combat target updated to: %s"), *BestTarget->GetName());
		}
		else
		{
			UE_LOG(LogBehaviorTree, Log, TEXT("Combat target cleared"));
		}
	}
}

AActor* UBTService_UpdateCombatTarget::FindBestTarget(UBehaviorTreeComponent& OwnerComp) const
{
	// Get AI Controller
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		return nullptr;
	}

	// Get controlled pawn
	APawn* OwnerPawn = AIController->GetPawn();
	if (!OwnerPawn)
	{
		return nullptr;
	}

	// Get perception component
	UAIPerceptionComponent* PerceptionComp = AIController->GetPerceptionComponent();
	if (!PerceptionComp)
	{
		return nullptr;
	}

	// Get owner's team ID
	int32 OwnerTeamID = -1;
	if (OwnerPawn->Implements<UMocTeamInterface>())
	{
		OwnerTeamID = IMocTeamInterface::Execute_GetTeamID(OwnerPawn);
	}

	// Get all perceived actors
	TArray<AActor*> PerceivedActors;
	PerceptionComp->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);

	// Filter valid enemies
	TArray<AActor*> ValidEnemies;
	for (AActor* Actor : PerceivedActors)
	{
		if (!Actor)
		{
			continue;
		}

		// Check if enemy team
		if (Actor->Implements<UMocTeamInterface>())
		{
			int32 ActorTeamID = IMocTeamInterface::Execute_GetTeamID(Actor);
			if (ActorTeamID == OwnerTeamID)
			{
				continue; // Same team, skip
			}
		}

		// Validate target
		if (IsValidTarget(Actor, OwnerPawn))
		{
			ValidEnemies.Add(Actor);
		}
	}

	// No valid enemies found
	if (ValidEnemies.Num() == 0)
	{
		return nullptr;
	}

	// Get current target for sticky targeting
	UBlackboardComponent* BlackboardComp = OwnerComp.GetBlackboardComponent();
	AActor* CurrentTarget = nullptr;
	if (BlackboardComp && bPreferCurrentTarget)
	{
		CurrentTarget = Cast<AActor>(BlackboardComp->GetValueAsObject(TargetEnemyKey.SelectedKeyName));
	}

	// Select best target based on mode
	AActor* BestTarget = nullptr;
	float BestScore = -1.0f;

	for (AActor* Enemy : ValidEnemies)
	{
		float Score = 0.0f;

		switch (TargetSelectionMode)
		{
		case ETargetSelectionMode::Nearest:
		{
			float Distance = FVector::Dist(OwnerPawn->GetActorLocation(), Enemy->GetActorLocation());
			Score = 1.0f / (Distance + 1.0f); // Higher score for closer enemies
			break;
		}

		case ETargetSelectionMode::LowestHealth:
		{
			if (Enemy->Implements<UCombatStatsInterface>())
			{
				float HealthPercent = ICombatStatsInterface::Execute_GetHealthPercentage(Enemy);
				Score = 100.0f - HealthPercent; // Higher score for lower health
			}
			break;
		}

		case ETargetSelectionMode::HighestThreat:
		{
			Score = CalculateThreatLevel(Enemy, OwnerPawn);
			break;
		}
		}

		// Apply sticky targeting bonus
		if (bPreferCurrentTarget && Enemy == CurrentTarget)
		{
			// Convert bonus range to score bonus (same scale as distance)
			Score += 1.0f / (CurrentTargetBonusRange + 1.0f);
		}

		// Update best target
		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = Enemy;
		}
	}

	return BestTarget;
}

bool UBTService_UpdateCombatTarget::IsValidTarget(AActor* Target, APawn* OwnerPawn) const
{
	if (!Target || !OwnerPawn)
	{
		return false;
	}

	// Check if target is alive
	if (Target->Implements<UCombatStatsInterface>())
	{
		if (!ICombatStatsInterface::Execute_IsAlive(Target))
		{
			return false;
		}
	}

	// Check distance if max range is specified
	if (MaxDetectionRange > 0.0f)
	{
		float Distance = FVector::Dist(OwnerPawn->GetActorLocation(), Target->GetActorLocation());
		if (Distance > MaxDetectionRange)
		{
			return false;
		}
	}

	// Check line of sight if required
	if (bRequireLineOfSight)
	{
		FVector StartLocation = OwnerPawn->GetActorLocation();
		FVector TargetLocation = Target->GetActorLocation();

		// Add offset to start location (eye height)
		StartLocation.Z += 50.0f;

		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(OwnerPawn);
		QueryParams.AddIgnoredActor(Target);

		UWorld* World = OwnerPawn->GetWorld();
		bool bHit = World->LineTraceSingleByChannel(
			HitResult,
			StartLocation,
			TargetLocation,
			ECC_Visibility,
			QueryParams
		);

		// If we hit something other than the target, line of sight is blocked
		if (bHit && HitResult.GetActor() != Target)
		{
			return false;
		}
	}

	return true;
}

float UBTService_UpdateCombatTarget::CalculateThreatLevel(AActor* Target, APawn* OwnerPawn) const
{
	if (!Target || !OwnerPawn)
	{
		return 0.0f;
	}

	float ThreatScore = 0.0f;

	// Distance factor (closer = more threatening)
	float Distance = FVector::Dist(OwnerPawn->GetActorLocation(), Target->GetActorLocation());
	ThreatScore += 1000.0f / (Distance + 1.0f);

	// Health factor (higher health = more threatening)
	if (Target->Implements<UCombatStatsInterface>())
	{
		float HealthPercent = ICombatStatsInterface::Execute_GetHealthPercentage(Target);
		ThreatScore += HealthPercent * 0.5f;
	}


	return ThreatScore;
}

FString UBTService_UpdateCombatTarget::GetStaticDescription() const
{
	FString Description = FString::Printf(TEXT("Update Combat Target"));

	switch (TargetSelectionMode)
	{
	case ETargetSelectionMode::Nearest:
		Description += TEXT(" (Nearest)");
		break;
	case ETargetSelectionMode::LowestHealth:
		Description += TEXT(" (Lowest Health)");
		break;
	case ETargetSelectionMode::HighestThreat:
		Description += TEXT(" (Highest Threat)");
		break;
	}

	if (MaxDetectionRange > 0.0f)
	{
		Description += FString::Printf(TEXT(" [%.0fm]"), MaxDetectionRange / 100.0f);
	}

	return Description;
}
