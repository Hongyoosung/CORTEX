// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/MocTypes.h"
#include "TeamState.generated.h"

/**
 * FTeamState - MOC v10.2 Global Squad Status for Centralized Planning
 *
 * Represents the complete state of a 5-agent team for use by Squad Commander.
 * Replaces individual FObservation (52-dim) with team-wide awareness including:
 * - Friendly unit status (positions, health, strategies, cooldowns)
 * - Enemy estimates (last known positions with confidence decay)
 * - Map state (capture points, pickups)
 *
 * Total Dimensionality: ~60-dim
 *
 * Usage:
 * - Collected by ASquadManager every planning cycle
 * - Input to Centralized MCTS for tactical play selection
 * - Batch processed by UMocTeamWorldModel for prediction
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FTeamState
{
	GENERATED_BODY()

	//========================================
	// Friendly Units (5 agents)
	//========================================

	/**
	 * Agent positions in world space
	 * [5 × 3D] = 15-dim
	 */
	UPROPERTY(BlueprintReadWrite, Category = "TeamState|Friendly")
	TArray<FVector> FriendlyPositions;

	/**
	 * Agent health percentages [0.0-1.0]
	 * [5] = 5-dim
	 */
	UPROPERTY(BlueprintReadWrite, Category = "TeamState|Friendly")
	TArray<float> FriendlyHealths;

	/**
	 * Current active strategies
	 * [5] = 5-dim (encoded as uint8)
	 */
	UPROPERTY(BlueprintReadWrite, Category = "TeamState|Friendly")
	TArray<EStrategyType> FriendlyStrategies;

	/**
	 * Weapon cooldown status [0.0-1.0]
	 * 0.0 = ready to fire, 1.0 = full cooldown
	 * [5] = 5-dim
	 */
	UPROPERTY(BlueprintReadWrite, Category = "TeamState|Friendly")
	TArray<float> FriendlyCooldowns;

	/**
	 * Alive status (true = alive, false = dead/respawning)
	 * [5] = 5-dim
	 */
	UPROPERTY(BlueprintReadWrite, Category = "TeamState|Friendly")
	TArray<bool> FriendlyAlive;

	//========================================
	// Enemy Estimates (with Fog of War)
	//========================================

	/**
	 * Last known enemy positions (from FogOfWarManager)
	 * [5 × 3D] = 15-dim
	 */
	UPROPERTY(BlueprintReadWrite, Category = "TeamState|Enemy")
	TArray<FVector> EnemyPositions;

	/**
	 * Enemy position confidence [0.0-1.0]
	 * Decays over time based on staleness
	 * 1.0 = just seen, 0.0 = very old/uncertain
	 * [5] = 5-dim
	 */
	UPROPERTY(BlueprintReadWrite, Category = "TeamState|Enemy")
	TArray<float> EnemyConfidences;

	/**
	 * Estimated enemy health (last observation)
	 * [5] = 5-dim
	 */
	UPROPERTY(BlueprintReadWrite, Category = "TeamState|Enemy")
	TArray<float> EnemyHealths;

	/**
	 * Enemy alive status (estimated)
	 * [5] = 5-dim
	 */
	UPROPERTY(BlueprintReadWrite, Category = "TeamState|Enemy")
	TArray<bool> EnemyAlive;

	//========================================
	// Map State
	//========================================

	/**
	 * Capture point ownership
	 * -1 = Enemy controlled
	 *  0 = Neutral
	 * +1 = Friendly controlled
	 * [5 points] = 5-dim
	 */
	UPROPERTY(BlueprintReadWrite, Category = "TeamState|Map")
	TArray<int32> CapturePointOwnership;

	/**
	 * Pickup availability bitmask
	 * Bit flags: 0=Taken, 1=Available
	 * Can be expanded to TArray<bool> for clarity
	 */
	UPROPERTY(BlueprintReadWrite, Category = "TeamState|Map")
	int32 PickupAvailability;

	/**
	 * Game time remaining (normalized 0-1)
	 * 1.0 = match start, 0.0 = match end
	 */
	UPROPERTY(BlueprintReadWrite, Category = "TeamState|Map")
	float TimeRemaining;

	//========================================
	// Utility Methods
	//========================================

	/**
	 * Serialize to flat float array for neural network input
	 * Total: ~60-dim
	 */
	TArray<float> ToTensor() const
	{
		TArray<float> Tensor;
		Tensor.Reserve(70); // Pre-allocate for efficiency

		// Friendly positions (15-dim)
		for (const FVector& Pos : FriendlyPositions)
		{
			Tensor.Add(Pos.X / 10000.0f); // Normalize to reasonable range
			Tensor.Add(Pos.Y / 10000.0f);
			Tensor.Add(Pos.Z / 1000.0f);
		}

		// Friendly health (5-dim)
		Tensor.Append(FriendlyHealths);

		// Friendly cooldowns (5-dim)
		Tensor.Append(FriendlyCooldowns);

		// Friendly alive (5-dim)
		for (bool bAlive : FriendlyAlive)
		{
			Tensor.Add(bAlive ? 1.0f : 0.0f);
		}

		// Friendly strategies (5-dim, one-hot encoded)
		for (EStrategyType Strategy : FriendlyStrategies)
		{
			Tensor.Add(static_cast<float>(Strategy));
		}

		// Enemy positions (15-dim)
		for (const FVector& Pos : EnemyPositions)
		{
			Tensor.Add(Pos.X / 10000.0f);
			Tensor.Add(Pos.Y / 10000.0f);
			Tensor.Add(Pos.Z / 1000.0f);
		}

		// Enemy confidences (5-dim)
		Tensor.Append(EnemyConfidences);

		// Enemy health (5-dim)
		Tensor.Append(EnemyHealths);

		// Enemy alive (5-dim)
		for (bool bAlive : EnemyAlive)
		{
			Tensor.Add(bAlive ? 1.0f : 0.0f);
		}

		// Capture point ownership (5-dim)
		for (int32 Ownership : CapturePointOwnership)
		{
			Tensor.Add(static_cast<float>(Ownership));
		}

		// Pickup availability (1-dim, can be expanded)
		Tensor.Add(static_cast<float>(PickupAvailability));

		// Time remaining (1-dim)
		Tensor.Add(TimeRemaining);

		return Tensor;
	}

	/**
	 * Get team average health
	 */
	float GetAverageHealth() const
	{
		if (FriendlyHealths.Num() == 0) return 0.0f;

		float Sum = 0.0f;
		int32 Count = 0;
		for (int32 i = 0; i < FriendlyHealths.Num(); ++i)
		{
			if (FriendlyAlive[i])
			{
				Sum += FriendlyHealths[i];
				Count++;
			}
		}

		return Count > 0 ? Sum / Count : 0.0f;
	}

	/**
	 * Get number of alive friendly agents
	 */
	int32 GetAliveCount() const
	{
		int32 Count = 0;
		for (bool bAlive : FriendlyAlive)
		{
			if (bAlive) Count++;
		}
		return Count;
	}

	/**
	 * Check if team is in critical health state
	 */
	bool IsTeamHealthCritical() const
	{
		return GetAverageHealth() < 0.3f;
	}

	/**
	 * Default constructor - initializes arrays
	 */
	FTeamState()
	{
		// Initialize with 5 agents
		FriendlyPositions.Init(FVector::ZeroVector, 5);
		FriendlyHealths.Init(1.0f, 5);
		FriendlyStrategies.Init(EStrategyType::Assault, 5);
		FriendlyCooldowns.Init(0.0f, 5);
		FriendlyAlive.Init(true, 5);

		EnemyPositions.Init(FVector::ZeroVector, 5);
		EnemyConfidences.Init(0.0f, 5);
		EnemyHealths.Init(1.0f, 5);
		EnemyAlive.Init(true, 5);

		CapturePointOwnership.Init(0, 5);
		PickupAvailability = 0;
		TimeRemaining = 1.0f;
	}
};
