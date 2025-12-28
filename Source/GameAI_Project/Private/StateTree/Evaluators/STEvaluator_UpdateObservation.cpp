// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Evaluators/STEvaluator_UpdateObservation.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Team/FollowerAgentComponent.h"
#include "Team/TeamLeaderComponent.h"
#include "Perception/AgentPerceptionComponent.h"
#include "Combat/WeaponComponent.h"
#include "Combat/HealthComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Engine\OverlapResult.h"


void FSTEvaluator_UpdateObservation::TreeStart(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// Initialize time accumulator
	InstanceData.TimeAccumulator = 0.0f;

	UE_LOG(LogTemp, Warning, TEXT("[UPDATE OBS EVALUATOR] TreeStart called - UpdateInterval=%.3f"),
		InstanceData.UpdateInterval);
}

void FSTEvaluator_UpdateObservation::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// Get components from context owner
	UFollowerAgentComponent* FollowerComponent = nullptr;
	APawn* ControlledPawn = nullptr;

	if (const UObject* ContextOwner = Context.GetOwner())
	{
		if (const AActor* OwnerActor = Cast<AActor>(ContextOwner))
		{
			FollowerComponent = OwnerActor->FindComponentByClass<UFollowerAgentComponent>();
			ControlledPawn = const_cast<APawn*>(Cast<APawn>(OwnerActor));
		}
	}

	if (!FollowerComponent)
	{
		static bool bLoggedOnce = false;
		if (!bLoggedOnce)
		{
			UE_LOG(LogTemp, Error, TEXT("[UPDATE OBS] Missing FollowerComponent!"));
			bLoggedOnce = true;
		}
		return;
	}

	if (!ControlledPawn)
	{
		static bool bLoggedPawnOnce = false;
		if (!bLoggedPawnOnce)
		{
			UE_LOG(LogTemp, Error, TEXT("[UPDATE OBS] Cannot get Pawn!"));
			bLoggedPawnOnce = true;
		}
		return;
	}

	// Check interval - but still update critical combat data every tick
	InstanceData.TimeAccumulator += DeltaTime;
	bool bFullUpdate = (InstanceData.TimeAccumulator >= InstanceData.UpdateInterval);

	if (bFullUpdate)
	{
		InstanceData.TimeAccumulator = 0.0f;
	}

	// Full update (observations, perception, cover) - run at intervals
	if (bFullUpdate)
	{

		// Get StateTree component to access shared context
		UFollowerStateTreeComponent* StateTreeComp = ControlledPawn->FindComponentByClass<UFollowerStateTreeComponent>();
		if (!StateTreeComp)
		{
			UE_LOG(LogTemp, Error, TEXT("[UPDATE OBS] Cannot find StateTreeComponent!"));
			return;
		}

		// Get SHARED context reference (this is the SAME context used by tasks)
		FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();

		// Get observation from follower component
		SharedContext.PreviousObservation = SharedContext.CurrentObservation;
		SharedContext.CurrentObservation = FollowerComponent->GetLocalObservation();

		// Update target tracking from perception system
		ScanForEnemies(SharedContext, InstanceData, ControlledPawn, ControlledPawn->GetWorld());

		// Update cover state
		DetectCover(SharedContext, InstanceData, ControlledPawn, ControlledPawn->GetWorld());
	}

	// CRITICAL: Update combat state EVERY tick (LOS, distance) - needed for firing
	// Get StateTree component to access shared context
	UFollowerStateTreeComponent* StateTreeComp = ControlledPawn->FindComponentByClass<UFollowerStateTreeComponent>();
	if (StateTreeComp)
	{
		FFollowerStateTreeContext& SharedContext = StateTreeComp->GetSharedContext();
		UpdateCombatState(SharedContext, InstanceData, ControlledPawn);

		// Update distance to primary target
		if (SharedContext.PrimaryTarget)
		{
			SharedContext.DistanceToPrimaryTarget = FVector::Dist(
				ControlledPawn->GetActorLocation(),
				SharedContext.PrimaryTarget->GetActorLocation()
			);
		}
		else
		{
			SharedContext.DistanceToPrimaryTarget = 99999.0f;
		}
	}

	// NOTE: No need to "write back" to external context - InstanceData.Context IS the shared context
	// All tasks/conditions/evaluators access the same FFollowerStateTreeContext instance
}

void FSTEvaluator_UpdateObservation::TreeStop(FStateTreeExecutionContext& Context) const
{
	// No cleanup needed
}

FObservationElement FSTEvaluator_UpdateObservation::GatherObservationData(FStateTreeExecutionContext& Context) const
{
	// NOTE: Observation building delegated to FollowerAgentComponent::BuildLocalObservation() (lines 420-499)
	// This evaluator retrieves pre-built observations via FollowerComponent->GetLocalObservation() (line 103)
	return FObservationElement();
}

void FSTEvaluator_UpdateObservation::UpdateAgentState(FObservationElement& Observation, APawn* ControlledPawn) const
{
	// NOTE: Delegated to FollowerAgentComponent::BuildLocalObservation()
}

void FSTEvaluator_UpdateObservation::PerformRaycastPerception(FObservationElement& Observation, APawn* ControlledPawn, UWorld* World) const
{
	// NOTE: Delegated to FollowerAgentComponent::BuildLocalObservation()
}

void FSTEvaluator_UpdateObservation::ScanForEnemies(FFollowerStateTreeContext& SharedContext, FSTEvaluator_UpdateObservationInstanceData& InstanceData, APawn* ControlledPawn, UWorld* World) const
{
	if (!ControlledPawn || !World)
	{
		UE_LOG(LogTemp, Error, TEXT("[SCAN ENEMIES] '%s': ControlledPawn or World is NULL"),
			*GetNameSafe(ControlledPawn));
		return;
	}

	// Get perception component
	UAgentPerceptionComponent* PerceptionComp = ControlledPawn->FindComponentByClass<UAgentPerceptionComponent>();
	if (!PerceptionComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SCAN ENEMIES] '%s': No AgentPerceptionComponent found"),
			*ControlledPawn->GetName());
		// No perception component, clear enemies
		SharedContext.VisibleEnemies.Empty();
		SharedContext.PrimaryTarget = nullptr;
		return;
	}

	// Get detected enemies from perception system (individual sight)
	TArray<AActor*> IndividuallyDetected = PerceptionComp->GetDetectedEnemies();

	// FIX (Issue #4): Team-based enemy sharing - merge individual sight with team knowledge
	// This allows all team members to be aware of enemies spotted by any teammate
	TSet<AActor*> AllKnownEnemies;

	// Add individually detected enemies
	for (AActor* Enemy : IndividuallyDetected)
	{
		if (Enemy && Enemy->IsValidLowLevel())
		{
			AllKnownEnemies.Add(Enemy);
		}
	}

	// Get team leader and merge team knowledge
	UFollowerAgentComponent* FollowerComp = ControlledPawn->FindComponentByClass<UFollowerAgentComponent>();
	if (FollowerComp && FollowerComp->GetTeamLeader())
	{
		UTeamLeaderComponent* TeamLeader = FollowerComp->GetTeamLeader();
		TArray<AActor*> TeamKnownEnemies = TeamLeader->GetKnownEnemies();

		// Merge team knowledge with individual perception
		for (AActor* Enemy : TeamKnownEnemies)
		{
			if (Enemy && Enemy->IsValidLowLevel())
			{
				AllKnownEnemies.Add(Enemy);
			}
		}

		// Report newly detected enemies to team leader
		for (AActor* Enemy : IndividuallyDetected)
		{
			if (Enemy)
			{
				TeamLeader->RegisterEnemy(Enemy);
			}
		}
	}

	// Update visible enemies list in context (sorted by distance)
	SharedContext.VisibleEnemies.Empty();
	TArray<TPair<AActor*, float>> EnemyDistances;

	for (AActor* Enemy : AllKnownEnemies)
	{
		if (Enemy)
		{
			float Distance = FVector::Dist(ControlledPawn->GetActorLocation(), Enemy->GetActorLocation());
			EnemyDistances.Add(TPair<AActor*, float>(Enemy, Distance));
		}
	}

	// Sort by distance (nearest first)
	EnemyDistances.Sort([](const TPair<AActor*, float>& A, const TPair<AActor*, float>& B) {
		return A.Value < B.Value;
	});

	// Add sorted enemies to context
	for (const auto& Pair : EnemyDistances)
	{
		SharedContext.VisibleEnemies.Add(Pair.Key);
	}


	// Set primary target - PRIORITIZE objective target over perception target
	AActor* ObjectiveTarget = SharedContext.CurrentObjective ? SharedContext.CurrentObjective->TargetActor : nullptr;

	if (ObjectiveTarget && ObjectiveTarget->IsValidLowLevel() && !ObjectiveTarget->IsPendingKillPending())
	{
		// Use objective-specified target if valid
		SharedContext.PrimaryTarget = ObjectiveTarget;
		float Distance = FVector::Dist(ControlledPawn->GetActorLocation(), ObjectiveTarget->GetActorLocation());

		if (InstanceData.bDrawDebugInfo)
		{
			FVector TargetLocation = ObjectiveTarget->GetActorLocation();
			DrawDebugLine(World, ControlledPawn->GetActorLocation(), TargetLocation,
				FColor::Orange, false, 0.2f, 0, 3.0f); // Orange for objective target
			DrawDebugSphere(World, TargetLocation, 50.0f, 12, FColor::Orange, false, 0.2f);
		}
	}
	else if (SharedContext.VisibleEnemies.Num() > 0)
	{
		// Fall back to nearest known enemy (team-shared or individually detected)
		SharedContext.PrimaryTarget = SharedContext.VisibleEnemies[0]; // Already sorted by distance
		float Distance = FVector::Dist(ControlledPawn->GetActorLocation(), SharedContext.VisibleEnemies[0]->GetActorLocation());

		if (InstanceData.bDrawDebugInfo)
		{
			FVector TargetLocation = SharedContext.PrimaryTarget->GetActorLocation();
			DrawDebugLine(World, ControlledPawn->GetActorLocation(), TargetLocation,
				FColor::Red, false, 0.2f, 0, 2.0f);
			DrawDebugSphere(World, TargetLocation, 50.0f, 12, FColor::Red, false, 0.2f);
		}
	}
	else
	{
		SharedContext.PrimaryTarget = nullptr;
	}
}

void FSTEvaluator_UpdateObservation::DetectCover(FFollowerStateTreeContext& SharedContext, FSTEvaluator_UpdateObservationInstanceData& InstanceData, APawn* ControlledPawn, UWorld* World) const
{
	if (!ControlledPawn || !World)
	{
		return;
	}

	// Simple cover detection: check for nearby actors tagged with "Cover"
	FVector AgentLocation = ControlledPawn->GetActorLocation();
	float CoverSearchRadius = 500.0f; // 5 meters

	TArray<FOverlapResult> OverlapResults;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(ControlledPawn);

	bool bFoundCover = World->OverlapMultiByChannel(
		OverlapResults,
		AgentLocation,
		FQuat::Identity,
		ECC_WorldStatic,
		FCollisionShape::MakeSphere(CoverSearchRadius),
		QueryParams
	);

	SharedContext.bInCover = false;

	if (bFoundCover)
	{
		// Check if any overlapped actor is tagged as cover
		for (const FOverlapResult& Result : OverlapResults)
		{
			AActor* OverlappedActor = Result.GetActor();
			if (OverlappedActor && OverlappedActor->ActorHasTag(FName("Cover")))
			{
				// Check if agent is within close proximity to cover
				float DistanceToCover = FVector::Dist(AgentLocation, OverlappedActor->GetActorLocation());
				if (DistanceToCover < 200.0f) // 2 meters
				{
					SharedContext.bInCover = true;
					SharedContext.CurrentCover = OverlappedActor;

					if (InstanceData.bDrawDebugInfo)
					{
						DrawDebugSphere(World, OverlappedActor->GetActorLocation(), 100.0f,
							12, FColor::Blue, false, 0.2f);
					}
					break;
				}
			}
		}
	}
}

void FSTEvaluator_UpdateObservation::UpdateCombatState(FFollowerStateTreeContext& SharedContext, FSTEvaluator_UpdateObservationInstanceData& InstanceData, APawn* ControlledPawn) const
{
	if (!ControlledPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[UPDATE COMBAT] ControlledPawn is NULL"));
		return;
	}

	UWorld* World = ControlledPawn->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[UPDATE COMBAT] '%s': World is NULL"), *ControlledPawn->GetName());
		return;
	}

	// Check LOS and distance to primary target
	if (SharedContext.PrimaryTarget)
	{
		// Use eye height for LOS trace (avoid hitting ground)
		FVector StartLocation = ControlledPawn->GetActorLocation() + FVector(0, 0, 80.0f); // Eye height offset
		FVector TargetLocation = SharedContext.PrimaryTarget->GetActorLocation() + FVector(0, 0, 80.0f);

		// Calculate distance
		float Distance = FVector::Dist(StartLocation, TargetLocation);
		SharedContext.CurrentObservation.DistanceToNearestEnemy = Distance;

		/*UE_LOG(LogTemp, Display, TEXT("[UPDATE COMBAT] '%s': Checking LOS to target '%s' at distance %.1f cm"),
			*ControlledPawn->GetName(), *SharedContext.PrimaryTarget->GetName(), Distance);
		UE_LOG(LogTemp, Display, TEXT("  → Start: (%.1f, %.1f, %.1f), Target: (%.1f, %.1f, %.1f)"),
			StartLocation.X, StartLocation.Y, StartLocation.Z,
			TargetLocation.X, TargetLocation.Y, TargetLocation.Z);*/

		// Check line of sight
		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(ControlledPawn);
		QueryParams.bTraceComplex = false;
		QueryParams.bReturnPhysicalMaterial = false;

		bool bHit = World->LineTraceSingleByChannel(
			HitResult,
			StartLocation,
			TargetLocation,
			ECC_Visibility,
			QueryParams
		);

		// Has LOS if hit the target or no blocking hit
		SharedContext.bHasLOS = !bHit || HitResult.GetActor() == SharedContext.PrimaryTarget;

		// Debug: Log what blocked LOS
		if (bHit && HitResult.GetActor() != SharedContext.PrimaryTarget)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[UPDATE COMBAT] '%s': ❌ LOS BLOCKED by '%s' at distance %.1f cm"),
				*ControlledPawn->GetName(),
				HitResult.GetActor() ? *HitResult.GetActor()->GetName() : TEXT("Unknown"),
				HitResult.Distance);
		}

		if (InstanceData.bDrawDebugInfo)
		{
			FColor LOSColor = SharedContext.bHasLOS ? FColor::Green : FColor::Yellow;
			DrawDebugLine(World, StartLocation, TargetLocation, LOSColor, false, 0.2f, 0, 1.0f);
		}
	}
	else
	{
		SharedContext.bHasLOS = false;
		SharedContext.CurrentObservation.DistanceToNearestEnemy = 99999.0f;
	}

	// Check if under fire (simplified - check if health component recently took damage)
	if (UHealthComponent* HealthComp = ControlledPawn->FindComponentByClass<UHealthComponent>())
	{
		// Consider "under fire" if took damage in last 2 seconds
		float TimeSinceLastDamage = HealthComp->GetTimeSinceLastDamage();
		SharedContext.bUnderFire = (TimeSinceLastDamage >= 0.0f && TimeSinceLastDamage < 2.0f);
	}
	else
	{
		SharedContext.bUnderFire = false;
	}
}

ERaycastHitType FSTEvaluator_UpdateObservation::ClassifyHitType(const FHitResult& HitResult) const
{
	return ERaycastHitType::None;
}

ETerrainType FSTEvaluator_UpdateObservation::DetectTerrainType(APawn* ControlledPawn) const
{
	return ETerrainType::Flat;
}
