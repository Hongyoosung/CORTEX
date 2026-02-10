// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/StrategyTypes.h"
#include "ObservationTypes.generated.h"

/**
 * MOC v10.0: Individual Agent Observation (52-dim)
 * Base state representation for a single agent's perception of the game state.
 * Used as input to RL policy and world model.
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FObservation
{
	GENERATED_BODY()

	//========================================
	// Self State (10-dim)
	//========================================

	/** Agent position (3-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	FVector Position = FVector::ZeroVector;

	/** Agent health [0.0-1.0] (1-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	float Health = 1.0f;

	/** Agent velocity (3-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	FVector Velocity = FVector::ZeroVector;

	/** Weapon cooldown [0.0-1.0] (1-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	float WeaponCooldown = 0.0f;

	/** Current strategy (1-dim encoded as uint8) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	EStrategyType CurrentStrategy = EStrategyType::Assault;

	/** Is alive (1-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	bool bIsAlive = true;

	//========================================
	// Allies State (4 agents × 5-dim = 20-dim)
	//========================================

	/** Ally positions (4 × 3 = 12-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Allies")
	TArray<FVector> AllyPositions;

	/** Ally health values (4-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Allies")
	TArray<float> AllyHealths;

	/** Ally strategies (4-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Allies")
	TArray<EStrategyType> AllyStrategies;

	//========================================
	// Enemies State (5 agents × 4-dim = 20-dim)
	//========================================

	/** Enemy positions with fog of war (5 × 3 = 15-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Enemies")
	TArray<FVector> EnemyPositions;

	/** Enemy visibility flags (5-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Enemies")
	TArray<bool> EnemyVisible;

	//========================================
	// Map State (2-dim)
	//========================================

	/** Controlled capture points count [-5, +5] (1-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Map")
	int32 CapturePointBalance = 0;

	/** Time remaining normalized [0.0-1.0] (1-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Map")
	float TimeRemaining = 1.0f;

	//========================================
	// Features Array
	//========================================

	/** Flattened feature array for neural network input (52-dim for agent, 56-dim for team) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation")
	TArray<float> Features;

	//========================================
	// Utility Methods
	//========================================

	/** Convert to flat float array for neural network input (52-dim) */
	TArray<float> ToArray() const
	{
		TArray<float> Result;
		Result.Reserve(52);

		// Self state (10-dim)
		Result.Add(Position.X / 10000.0f);
		Result.Add(Position.Y / 10000.0f);
		Result.Add(Position.Z / 1000.0f);
		Result.Add(Health);
		Result.Add(Velocity.X / 1000.0f);
		Result.Add(Velocity.Y / 1000.0f);
		Result.Add(Velocity.Z / 1000.0f);
		Result.Add(WeaponCooldown);
		Result.Add(static_cast<float>(CurrentStrategy));
		Result.Add(bIsAlive ? 1.0f : 0.0f);

		// Allies (20-dim: 4 agents × 5)
		for (int32 i = 0; i < 4; ++i)
		{
			if (i < AllyPositions.Num())
			{
				Result.Add(AllyPositions[i].X / 10000.0f);
				Result.Add(AllyPositions[i].Y / 10000.0f);
				Result.Add(AllyPositions[i].Z / 1000.0f);
			}
			else
			{
				Result.Add(0.0f);
				Result.Add(0.0f);
				Result.Add(0.0f);
			}

			if (i < AllyHealths.Num())
			{
				Result.Add(AllyHealths[i]);
			}
			else
			{
				Result.Add(0.0f);
			}

			if (i < AllyStrategies.Num())
			{
				Result.Add(static_cast<float>(AllyStrategies[i]));
			}
			else
			{
				Result.Add(0.0f);
			}
		}

		// Enemies (20-dim: 5 agents × 4)
		for (int32 i = 0; i < 5; ++i)
		{
			if (i < EnemyPositions.Num())
			{
				Result.Add(EnemyPositions[i].X / 10000.0f);
				Result.Add(EnemyPositions[i].Y / 10000.0f);
				Result.Add(EnemyPositions[i].Z / 1000.0f);
			}
			else
			{
				Result.Add(0.0f);
				Result.Add(0.0f);
				Result.Add(0.0f);
			}

			if (i < EnemyVisible.Num())
			{
				Result.Add(EnemyVisible[i] ? 1.0f : 0.0f);
			}
			else
			{
				Result.Add(0.0f);
			}
		}

		// Map state (2-dim)
		Result.Add(static_cast<float>(CapturePointBalance) / 5.0f); // Normalize [-1, 1]
		Result.Add(TimeRemaining);

		ensure(Result.Num() == 52);
		return Result;
	}

	/** Default constructor */
	FObservation()
	{
		AllyPositions.Init(FVector::ZeroVector, 4);
		AllyHealths.Init(1.0f, 4);
		AllyStrategies.Init(EStrategyType::Assault, 4);
		EnemyPositions.Init(FVector::ZeroVector, 5);
		EnemyVisible.Init(false, 5);
	}
};
