// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MocTypes.generated.h"

/**
 * Five tactical strategies for MOC v10.1+
 */
UENUM(BlueprintType)
enum class EStrategyType : uint8
{
	Assault UMETA(DisplayName = "Assault"),
	Defend UMETA(DisplayName = "Defend"),
	Support UMETA(DisplayName = "Support"),
};

/**
 * MOC v10.2: Tactical Play - Predefined team compositions for action space pruning
 * Reduces combinatorial explosion (3^5 = 243 combinations → ~10 valid plays)
 */
UENUM(BlueprintType)
enum class ETacticalPlay : uint8
{
	// Aggressive Plays
	AllOutRush UMETA(DisplayName = "All-Out Rush"),          // 5 Assault
	AggressivePush UMETA(DisplayName = "Aggressive Push"),   // 4 Assault, 1 Support

	// Balanced Plays
	Phalanx UMETA(DisplayName = "Phalanx Formation"),        // 2 Defend, 3 Support
	StandardComp UMETA(DisplayName = "Standard Comp"),       // 2 Assault, 2 Defend, 1 Support

	// Defensive Plays
	FortressDefense UMETA(DisplayName = "Fortress Defense"), // 1 Assault, 4 Defend
	TurtleFormation UMETA(DisplayName = "Turtle Formation"), // 5 Defend

	// Tactical Plays
	BaitStrategy UMETA(DisplayName = "Bait Strategy"),       // 1 Assault (bait), 4 Defend (ambush)
	PincerManeuver UMETA(DisplayName = "Pincer Maneuver"),   // 3 Assault (split), 2 Support

	// Support-Focused
	HealerComp UMETA(DisplayName = "Healer Comp"),           // 2 Assault, 1 Defend, 2 Support
	ResourceDeny UMETA(DisplayName = "Resource Deny"),       // 3 Support, 2 Assault

	COUNT UMETA(Hidden)
};

/**
 * MOC v10.2: Critical event types that trigger immediate replanning
 */
UENUM(BlueprintType)
enum class ECriticalEventType : uint8
{
	AllyKilled UMETA(DisplayName = "Ally Killed"),
	EnemyKilled UMETA(DisplayName = "Enemy Killed"),
	ObjectiveCaptured UMETA(DisplayName = "Objective Captured"),
	ObjectiveLost UMETA(DisplayName = "Objective Lost"),
	HealthCritical UMETA(DisplayName = "Team Health Critical"), // Team average < 30%
	COUNT UMETA(Hidden)
};

/**
 * Team ownership
 */
UENUM(BlueprintType)
enum class ETeamOwnership : uint8
{
	Red UMETA(DisplayName = "Red Team"),
	Blue UMETA(DisplayName = "Blue Team"),
	Neutral UMETA(DisplayName = "Neutral")
};

/**
 * Tactical option output from MCTS
 */
USTRUCT(BlueprintType)
struct FTacticalOption
{
	GENERATED_BODY()

	/** Selected strategy */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	EStrategyType Strategy = EStrategyType::Assault;

	/** Confidence score from MCTS */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float Confidence = 0.5f;

	FTacticalOption() = default;

	FTacticalOption(EStrategyType InStrategy, float InConfidence = 0.5f)
		: Strategy(InStrategy)
		, Confidence(InConfidence)
	{}

	FString ToString() const
	{
		return FString::Printf(TEXT("Strategy=%d, Confidence=%.2f"),
			static_cast<int32>(Strategy),
			Confidence);
	}
};

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

/**
 * Tactical Parameters (4-dim) for v8.0 Schola System
 * Continuous action space for tactical behavior modulation.
 * Each parameter in range [0.0, 1.0].
 */
USTRUCT(BlueprintType)
struct FTacticalParameters
{
	GENERATED_BODY()

	/** Aggression level: 0.0 = passive, 1.0 = aggressive */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float Aggression = 0.5f;

	/** Cover preference: 0.0 = ignore cover, 1.0 = prioritize cover */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float CoverPreference = 0.5f;

	/** Formation spread: 0.0 = tight formation, 1.0 = spread out */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float SpreadDistance = 0.5f;

	/** Risk tolerance: 0.0 = retreat early, 1.0 = fight to death */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float RiskTolerance = 0.5f;

	FTacticalParameters() = default;

	FTacticalParameters(float InAggression, float InCoverPref, float InSpread, float InRisk)
		: Aggression(InAggression)
		, CoverPreference(InCoverPref)
		, SpreadDistance(InSpread)
		, RiskTolerance(InRisk)
	{}

	/** Convert to array for serialization */
	TArray<float> ToArray() const
	{
		return { Aggression, CoverPreference, SpreadDistance, RiskTolerance };
	}

	/** Clamp all values to valid range [0, 1] */
	void Clamp()
	{
		Aggression = FMath::Clamp(Aggression, 0.0f, 1.0f);
		CoverPreference = FMath::Clamp(CoverPreference, 0.0f, 1.0f);
		SpreadDistance = FMath::Clamp(SpreadDistance, 0.0f, 1.0f);
		RiskTolerance = FMath::Clamp(RiskTolerance, 0.0f, 1.0f);
	}
};

/**
 * Agent statistics for MOC Arena
 */
USTRUCT(BlueprintType)
struct FAgentStats
{
	GENERATED_BODY()

	// Combat
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float MaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float MovementSpeed = 600.0f; // UE5 units/second (~6 m/s)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float SprintSpeed = 900.0f; // 1.5x multiplier

	// Weapon System (Hitscan rifle)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float FireRate = 0.15f; // 6.67 rounds/second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float Damage = 15.0f; // 7 shots to kill at full health

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float Accuracy = 0.85f; // Base hit probability

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float EffectiveRange = 5000.0f; // 50m optimal range

	// Perception
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float VisionRange = 8000.0f; // 80m sight radius

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float VisionAngle = 90.0f; // 90-degree FOV cone

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float HearingRange = 3000.0f; // 30m audio detection

	FAgentStats() = default;
};

/**
 * Capture point state
 */
USTRUCT(BlueprintType)
struct FCapturePointState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	int32 OwnerTeam = -1; // -1 = neutral, 0 = red, 1 = blue

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	float CaptureProgress = 0.0f; // 0.0 to 1.0

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	bool bContested = false;

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	int32 RedAgentsPresent = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Capture")
	int32 BlueAgentsPresent = 0;

	FCapturePointState() = default;
};
