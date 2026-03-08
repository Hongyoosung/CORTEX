// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/DEStrategyTypes.h"
#include "DEObservationTypes.generated.h"

/**
 * Individual Agent Observation (48-dim base)
 *
 * Layout:
 *   Self      (7):  pos/7500(3), health(1), vel/600(3)
 *   Allies   (16):  4 × [rel_pos/8000(3), health(1)]
 *   Enemies  (20):  5 × [rel_pos/8000_if_visible(3), visible(1)]
 *   Map       (5):  capture_point_status×5 (+1=friendly, 0=neutral, -1=enemy)
 *
 * Note: WeaponCooldown is kept in struct for gameplay but excluded from ToArray().
 *
 * Positions are agent-relative (ally/enemy pos minus self pos) so the same
 * tactical geometry always produces the same feature values regardless of
 * map location.  Self position is environment-relative and map-normalized,
 * ensuring consistent features across parallel environments.
 *
 * CurrentStrategy and bIsAlive are kept in the struct for reward computation
 * but are NOT included in ToArray() — strategy is appended as a one-hot by
 * DETacticalObserver.
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FDEObservation
{
	GENERATED_BODY()

	//========================================
	// Self State
	//========================================

	/** Agent world position (used in ToArray + reward computation) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	FVector Position = FVector::ZeroVector;

	/** Environment origin — subtracted from Position in ToArray() so that
	 *  self-position features are environment-relative, not absolute world coords.
	 *  Must be set by the observation gatherer (DETrainer / DETacticalObserver). */
	FVector EnvironmentOrigin = FVector::ZeroVector;

	/** Agent health [0.0-1.0] */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	float Health = 1.0f;

	/** Agent velocity (cm/s) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	FVector Velocity = FVector::ZeroVector;

	/** Weapon cooldown progress [0.0-1.0] */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	float WeaponCooldown = 0.0f;

	/** Current commanded strategy — kept for reward caching, NOT in ToArray */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	EDEStrategyType CurrentStrategy = EDEStrategyType::Assault;

	/** Alive flag — kept for death reward computation, NOT in ToArray */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	bool bIsAlive = true;

	//========================================
	// Allies State (4 agents × 4-dim = 16-dim)
	//========================================

	/** Ally world positions (relative offset computed in ToArray) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Allies")
	TArray<FVector> AllyPositions;

	/** Ally health values [0.0-1.0] */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Allies")
	TArray<float> AllyHealths;

	//========================================
	// Enemies State (5 agents × 4-dim = 20-dim)
	//========================================

	/** Enemy world positions — set to ZeroVector when not visible */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Enemies")
	TArray<FVector> EnemyPositions;

	/** Enemy line-of-sight flags */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Enemies")
	TArray<bool> EnemyVisible;

	//========================================
	// Map State (5-dim)
	//========================================

	/** Per-point ownership relative to this agent's team (5-dim)
	 *  +1.0 = owned by my team, 0.0 = neutral, -1.0 = owned by enemy
	 *  Order: PointA, PointB, PointC, PointD, PointE */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Map")
	TArray<float> CapturePointStatuses;

	//========================================
	// Utility Methods
	//========================================

	/**
	 * Convert to flat float array for neural network input (48-dim).
	 * Ally and enemy positions are expressed relative to self position
	 * and normalized by vision range (8000 cm) so spatial relationships
	 * are map-position invariant.
	 */
	TArray<float> ToArray() const
	{
		TArray<float> Result;
		Result.Reserve(48);

		// Self state (7-dim) — WeaponCooldown excluded (not useful for NN)
		// Position: environment-relative, map-normalized (150m map = 15000cm half-width = 7500)
		const FVector RelativePos = Position - EnvironmentOrigin;
		Result.Add(RelativePos.X / 7500.0f);
		Result.Add(RelativePos.Y / 7500.0f);
		Result.Add(RelativePos.Z / 1000.0f);
		Result.Add(Health);
		// Velocity: normalized by max walk speed (600 cm/s)
		Result.Add(Velocity.X / 600.0f);
		Result.Add(Velocity.Y / 600.0f);
		Result.Add(Velocity.Z / 600.0f);
		// CurrentStrategy and bIsAlive intentionally excluded:
		//   Strategy → appended as one-hot by DETacticalObserver
		//   bIsAlive  → always 1 when a real observation is gathered

		// Allies (16-dim: 4 agents × 4)
		// Relative position normalized by vision range (8000 cm)
		for (int32 i = 0; i < 4; ++i)
		{
			if (i < AllyPositions.Num())
			{
				const FVector RelPos = (AllyPositions[i] - Position) / 8000.0f;
				Result.Add(RelPos.X);
				Result.Add(RelPos.Y);
				Result.Add(RelPos.Z);
			}
			else
			{
				Result.Add(0.0f);
				Result.Add(0.0f);
				Result.Add(0.0f);
			}
			Result.Add(i < AllyHealths.Num() ? AllyHealths[i] : 0.0f);
		}

		// Enemies (20-dim: 5 agents × 4)
		// Position is relative and only exposed when the enemy is visible;
		// non-visible slots output (0,0,0) to avoid stale absolute coordinates.
		for (int32 i = 0; i < 5; ++i)
		{
			const bool bVis = (i < EnemyVisible.Num()) && EnemyVisible[i];
			if (bVis && i < EnemyPositions.Num())
			{
				const FVector RelPos = (EnemyPositions[i] - Position) / 8000.0f;
				Result.Add(RelPos.X);
				Result.Add(RelPos.Y);
				Result.Add(RelPos.Z);
			}
			else
			{
				Result.Add(0.0f);
				Result.Add(0.0f);
				Result.Add(0.0f);
			}
			Result.Add(bVis ? 1.0f : 0.0f);
		}

		// Map state (5-dim): per-point ownership
		for (int32 i = 0; i < 5; ++i)
		{
			Result.Add(i < CapturePointStatuses.Num() ? CapturePointStatuses[i] : 0.0f);
		}

		ensure(Result.Num() == 48);
		return Result;
	}

	/** Default constructor */
	FDEObservation()
	{
		AllyPositions.Init(FVector::ZeroVector, 4);
		AllyHealths.Init(1.0f, 4);
		EnemyPositions.Init(FVector::ZeroVector, 5);
		EnemyVisible.Init(false, 5);
		CapturePointStatuses.Init(0.0f, 5);
	}
};
