// TacticalRewardProvider.cpp - v9.0: Strategy-specific reward provider

#include "Schola/TacticalRewardProvider.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "RL/Components/RewardCalculator.h"
#include "RL/Components/RLAgentComponent.h"
#include "Observation/Components/ObservationBuilderComponent.h"
#include "Team/TeamTypes.h"  // For EStrategyType enum

UTacticalRewardProvider::UTacticalRewardProvider()
{
}

void UTacticalRewardProvider::Initialize()
{
	if (bAutoFindFollower && FollowerAgent == nullptr)
	{
		FollowerAgent = FindFollowerAgent();
	}

	if (FollowerAgent)
	{
		// v9.0: Cache RewardCalculator and ObservationBuilder for performance
		if (FollowerAgent->RLAgent)
		{
			CachedRewardCalculator = FollowerAgent->RLAgent->GetRewardCalculator();
		}
		CachedObservationBuilder = FollowerAgent->ObservationBuilder;

		LastRewardValue = FollowerAgent->GetAccumulatedReward();

		UE_LOG(LogTemp, Log, TEXT("[TacticalRewardProvider v9.0] Initialized with FollowerAgent %s"),
			*FollowerAgent->GetOwner()->GetName());

		if (CachedRewardCalculator)
		{
			UE_LOG(LogTemp, Log, TEXT("  ✅ RewardCalculator found (strategy-specific rewards enabled)"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("  ⚠️ RewardCalculator NOT found (falling back to legacy rewards)"));
		}

		if (CachedObservationBuilder)
		{
			UE_LOG(LogTemp, Log, TEXT("  ✅ ObservationBuilder found"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("  ⚠️ ObservationBuilder NOT found"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalRewardProvider] No FollowerAgent found"));
	}
}

float UTacticalRewardProvider::GetReward()
{
	// v9.0 FIX: Per-instance call counting (was global static - caused only 1 agent to log per 50 calls!)
	static TMap<UTacticalRewardProvider*, int32> CallCounts;
	int32& CallCount = CallCounts.FindOrAdd(this, 0);
	CallCount++;

	if (!FollowerAgent)
	{
		static int32 NoFollowerWarnings = 0;
		if (++NoFollowerWarnings % 100 == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("❌ [REWARD ERROR v9.0] TacticalRewardProvider has no FollowerAgent (count=%d)"), NoFollowerWarnings);
		}
		return 0.0f;
	}

	// v9.0: Use sophisticated RewardCalculator if available
	if (CachedRewardCalculator && CachedObservationBuilder)
	{
		// Get current and previous observations from ObservationBuilder
		FObservationElement PrevObs = CachedObservationBuilder->GetPreviousObservation();
		FObservationElement CurrentObs = CachedObservationBuilder->GetLocalObservation();

		// Calculate unified reward (strategy-specific + tactical + combat + survival + coordination)
		EStrategyType CurrentStrategy = CachedRewardCalculator->GetCurrentStrategy();
		FRewardComponentBreakdown Breakdown = CachedRewardCalculator->CalculateUnifiedReward(
			CurrentStrategy,
			PrevObs,
			CurrentObs
		);

		// v9.0 TEMPORARY: Log EVERY reward calculation to diagnose clustering issue
		static int32 GlobalCallCount = 0;
		GlobalCallCount++;

		// Log every 10 calls initially (change to % 1 for debugging every single call)
		bool bShouldLog = (CallCount % 10 == 0) || (GlobalCallCount <= 100);

		if (bShouldLog)
		{
			// Always log to verify rewards are being calculated
			UE_LOG(LogTemp, Warning, TEXT("🎯 [SCHOLA REWARD v9.0] %s (Strategy=%s, Call#%d, Global#%d):"),
				*FollowerAgent->GetOwner()->GetName(),
				*UEnum::GetValueAsString(CurrentStrategy),
				CallCount,
				GlobalCallCount
			);
			UE_LOG(LogTemp, Warning, TEXT("   Obj=%.4f, Combat=%.4f, Surv=%.4f, Cover=%.4f, Coord=%.4f, Tact=%.4f → Total=%.4f"),
				Breakdown.ObjectiveProgress,
				Breakdown.CombatEffectiveness,
				Breakdown.Survival,
				Breakdown.CoverUsage,
				Breakdown.TeamCoordination,
				Breakdown.TacticalEffectiveness,
				Breakdown.Total
			);

			// Verify observations are differentiated
			UE_LOG(LogTemp, Warning, TEXT("   Obs: Friendly=%.3f, Hostile=%.3f, Enemy=%.3f, Ally=%.3f"),
				CurrentObs.FriendlyObjectiveDistance,
				CurrentObs.HostileObjectiveDistance,
				CurrentObs.DistanceToNearestEnemy,
				CurrentObs.AllyDistance
			);

			// Detect default observations (problem indicator)
			if (CurrentObs.FriendlyObjectiveDistance >= 0.99f && CurrentObs.HostileObjectiveDistance >= 0.99f)
			{
				UE_LOG(LogTemp, Error, TEXT("   ⚠️ WARNING: Default objective distances! All agents will have IDENTICAL rewards."));
			}

			// Detect if strategy is stuck on default
			if (CurrentStrategy == EStrategyType::Assault && CallCount > 500)
			{
				UE_LOG(LogTemp, Warning, TEXT("   ⚠️ Strategy is Assault for %d consecutive calls. Verify MCTS assignments."), CallCount);
			}
		}

		// v9.0 CRITICAL: Log the exact reward value being returned to Schola/Python
		if (bShouldLog)
		{
			UE_LOG(LogTemp, Verbose, TEXT("   ➡️ RETURNING TO SCHOLA: %.4f (Agent=%s, Strategy=%s)"),
				Breakdown.Total,
				*FollowerAgent->GetOwner()->GetName(),
				*UEnum::GetValueAsString(CurrentStrategy)
			);
		}

		return Breakdown.Total;
	}
	else
	{
		// Fallback: Use legacy accumulated reward (combat events only)
		static int32 LegacyWarnings = 0;
		if (++LegacyWarnings <= 5)
		{
			UE_LOG(LogTemp, Error, TEXT("⚠️ [LEGACY REWARD v9.0] Agent '%s': Using fallback rewards (RewardCalculator=%s, ObsBuilder=%s)"),
				*FollowerAgent->GetOwner()->GetName(),
				CachedRewardCalculator ? TEXT("OK") : TEXT("NULL"),
				CachedObservationBuilder ? TEXT("OK") : TEXT("NULL")
			);
		}

		float CurrentReward = FollowerAgent->GetAccumulatedReward();
		float DeltaReward = CurrentReward - LastRewardValue;

		if (CallCount % 50 == 0 && FMath::Abs(DeltaReward) > 0.01f)
		{
			UE_LOG(LogTemp, Display, TEXT("⚠️ [LEGACY REWARD] Agent=%s | Delta=%.3f | Accumulated=%.3f"),
				*FollowerAgent->GetOwner()->GetName(),
				DeltaReward,
				CurrentReward
			);
		}

		LastRewardValue = CurrentReward;
		return DeltaReward;
	}
}

void UTacticalRewardProvider::Reset()
{
	LastRewardValue = 0.0f;

	if (FollowerAgent)
	{
		FollowerAgent->ResetEpisode();
		LastRewardValue = FollowerAgent->GetAccumulatedReward();
	}

	// v9.0: Re-cache components after episode reset
	if (FollowerAgent && FollowerAgent->RLAgent)
	{
		CachedRewardCalculator = FollowerAgent->RLAgent->GetRewardCalculator();
	}
	if (FollowerAgent)
	{
		CachedObservationBuilder = FollowerAgent->ObservationBuilder;
	}
}

UFollowerAgentComponent* UTacticalRewardProvider::FindFollowerAgent() const
{
	AActor* Owner = Cast<AActor>(GetOuter());
	if (!Owner)
	{
		UActorComponent* OuterComponent = Cast<UActorComponent>(GetOuter());
		if (OuterComponent)
		{
			Owner = OuterComponent->GetOwner();
		}
	}

	if (Owner)
	{
		return Owner->FindComponentByClass<UFollowerAgentComponent>();
	}

	return nullptr;
}
