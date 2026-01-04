// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Observation/ObservationElement.h"
#include "RLTypes.generated.h"

/**
 * Strategy types for MCTS individual assignment (v5.0)
 * Each agent receives an individual strategy based on their state
 */
UENUM(BlueprintType)
enum class EStrategyType : uint8
{
	Assault   UMETA(DisplayName = "Assault - Push toward objective"),
	Defend    UMETA(DisplayName = "Defend - Hold current position"),
	Support   UMETA(DisplayName = "Support - Protect ally in need"),
	Retreat   UMETA(DisplayName = "Retreat - Disengage safely"),
	COUNT     UMETA(Hidden)
};

/**
 * Position selection for macro actions (v4.0)
 * Simplified to 4 core positions (removed FlankLeft/FlankRight for faster learning)
 */
UENUM(BlueprintType)
enum class ETacticalPosition : uint8
{
	Hold          UMETA(DisplayName = "Hold Position"),
	ForwardCover  UMETA(DisplayName = "Advance to Forward Cover"),
	Retreat       UMETA(DisplayName = "Retreat to Safe Position"),
	Advance       UMETA(DisplayName = "Advance (No Cover)")
};

/**
 * Fire discipline modes (v4.0)
 */
UENUM(BlueprintType)
enum class EFireMode : uint8
{
	HoldFire   UMETA(DisplayName = "Hold Fire"),
	Fire       UMETA(DisplayName = "Fire at Target"),
	Suppress   UMETA(DisplayName = "Suppressive Fire")
};

/**
 * Ally context for Support strategy (v5.0)
 * Provides information about the ally most in need of assistance
 */
USTRUCT(BlueprintType)
struct FAllyContext
{
	GENERATED_BODY()

	/** Whether any ally needs immediate help (health < 50% or surrounded) */
	UPROPERTY(BlueprintReadWrite, Category = "Support")
	bool bAllyNeedsHelp = false;

	/** Normalized health of the ally most in need [0, 1] */
	UPROPERTY(BlueprintReadWrite, Category = "Support")
	float AllyHealth = 1.0f;

	/** Distance to ally in need (normalized by max range) [0, 1] */
	UPROPERTY(BlueprintReadWrite, Category = "Support")
	float AllyDistance = 0.0f;

	/** Direction to ally (normalized 2D vector) */
	UPROPERTY(BlueprintReadWrite, Category = "Support")
	FVector2D AllyDirection = FVector2D::ZeroVector;

	

	/** Convert to feature array for neural network (4 features) */
	TArray<float> ToFeatureVector() const
	{
		return {
			bAllyNeedsHelp ? 1.0f : 0.0f,
			AllyHealth,
			AllyDistance,
			static_cast<float>(FMath::Atan2(AllyDirection.Y, AllyDirection.X) / PI)  // Normalized angle [-1, 1]
		};
	}
};

/**
 * Macro action space for v5.0 multi-head squad tactics
 * High-level decisions: WHERE to go, WHO to shoot, WHEN to fire
 * Engine handles physics: NavMesh pathfinding, auto-aim
 */
USTRUCT(BlueprintType)
struct FMacroAction
{
	GENERATED_BODY()

	/** Position: Which tactical location to move to (EQS candidate index) */
	UPROPERTY(BlueprintReadWrite, Category = "Action|Tactical")
	ETacticalPosition PositionChoice = ETacticalPosition::Hold;

	/** Target: Which enemy to engage (-1 = none, 0+ = enemy index) */
	UPROPERTY(BlueprintReadWrite, Category = "Action|Tactical")
	int32 TargetIndex = -1;

	/** Fire Mode: How to engage target */
	UPROPERTY(BlueprintReadWrite, Category = "Action|Tactical")
	EFireMode FireMode = EFireMode::HoldFire;

	FMacroAction()
		: PositionChoice(ETacticalPosition::Hold)
		, TargetIndex(-1)
		, FireMode(EFireMode::HoldFire)
	{
	}

	FMacroAction(ETacticalPosition Pos, int32 Target, EFireMode Fire)
		: PositionChoice(Pos)
		, TargetIndex(Target)
		, FireMode(Fire)
	{
	}
};

/**
 * Tactical action space (v5.0 multi-head macro actions)
 * High-level tactical decisions: WHERE to go, WHO to shoot, HOW to engage
 */
USTRUCT(BlueprintType)
struct FTacticalAction
{
	GENERATED_BODY()

	/** High-level tactical decision */
	UPROPERTY(BlueprintReadWrite, Category = "Action")
	FMacroAction MacroAction;

	/** Active strategy for this action (determines which head was used) */
	UPROPERTY(BlueprintReadWrite, Category = "Action")
	EStrategyType ActiveStrategy = EStrategyType::Assault;

	FTacticalAction()
		: MacroAction()
		, ActiveStrategy(EStrategyType::Assault)
	{
	}

	explicit FTacticalAction(const FMacroAction& Macro, EStrategyType Strategy = EStrategyType::Assault)
		: MacroAction(Macro)
		, ActiveStrategy(Strategy)
	{
	}
};

USTRUCT(BlueprintType)
struct FActionSequence
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FTacticalAction> Actions;
};


/**
 * RL training statistics
 */
USTRUCT(BlueprintType)
struct FRLTrainingStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "RL")
	int32 TotalExperiences;

	UPROPERTY(BlueprintReadWrite, Category = "RL")
	int32 EpisodesCompleted;

	UPROPERTY(BlueprintReadWrite, Category = "RL")
	float AverageReward;

	UPROPERTY(BlueprintReadWrite, Category = "RL")
	float AverageEpisodeLength;

	UPROPERTY(BlueprintReadWrite, Category = "RL")
	float LastEpisodeReward;

	UPROPERTY(BlueprintReadWrite, Category = "RL")
	float BestEpisodeReward;

	UPROPERTY(BlueprintReadWrite, Category = "RL")
	float TrainingTimeSeconds;

	/** Per-strategy episode counts (v5.0) */
	UPROPERTY(BlueprintReadWrite, Category = "RL")
	TMap<EStrategyType, int32> StrategyEpisodeCounts;

	FRLTrainingStats()
		: TotalExperiences(0)
		, EpisodesCompleted(0)
		, AverageReward(0.0f)
		, AverageEpisodeLength(0.0f)
		, LastEpisodeReward(0.0f)
		, BestEpisodeReward(-MAX_FLT)
		, TrainingTimeSeconds(0.0f)
	{
	}
};

/**
 * Multi-head policy configuration (v5.0)
 *
 * Observation: 64 features (streamlined from 74)
 *   - Agent State (7): pos(3), vel(3), health(1)
 *   - Combat (1): enemy_dist(1)
 *   - Perception (32): raycasts(16), hit_types(16)
 *   - Enemy Info (16): count(1), nearby(15)
 *   - Tactical (4): has_cover(1), cover_dist(1), cover_dir(2)
 *   - Support Context (4): ally_needs(1), ally_health(1), ally_dist(1), ally_dir(1)
 *
 * Removed (v5.0): rotation(3), shield(1), ammo(1), cooldown(1), weapon(1), terrain(1), objective_embedding(4)
 *
 * Output: 4 strategy-specific heads, each with MultiDiscrete([4, N+1, 3])
 */
USTRUCT(BlueprintType)
struct FMultiHeadPolicyConfig
{
	GENERATED_BODY()

	/** Number of input features (64 streamlined observation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	int32 InputSize = 64;

	/** Hidden layer sizes for shared trunk */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	TArray<int32> SharedTrunkLayers = {128, 128, 64};

	/** Hidden layer sizes for each strategy head */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	TArray<int32> HeadLayers = {32};

	/** Number of position choices */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	int32 NumPositions = 4;

	/** Maximum number of targetable enemies */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	int32 MaxTargets = 10;

	/** Number of fire modes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	int32 NumFireModes = 3;

	/** Total output size per head: Position + Target + Fire (default: 4+6+3=13) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	int32 OutputSize = 52;  // 4 heads * 13 logits = 52 (or 13 if model does head selection)

	/** Path to ONNX model file (single model with 4 heads) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	FString ModelPath = TEXT("");

	/** Learning rate for training */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	float LearningRate = 0.0003f;

	/** Discount factor (gamma) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	float DiscountFactor = 0.99f;

	/** Epsilon for epsilon-greedy exploration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	float Epsilon = 0.1f;

	/** Epsilon decay rate per episode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	float EpsilonDecay = 0.995f;

	/** Minimum epsilon value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	float MinEpsilon = 0.01f;
};

/**
 * Strategy-specific reward weights (v5.0)
 */
USTRUCT(BlueprintType)
struct FStrategyRewardWeights
{
	GENERATED_BODY()

	// === ASSAULT REWARDS ===
	static constexpr float ASSAULT_KILL = 15.0f;
	static constexpr float ASSAULT_DAMAGE = 0.1f;          // per 10 damage
	static constexpr float ASSAULT_ADVANCE = 0.5f;         // per second of advance
	static constexpr float ASSAULT_OBJECTIVE = 20.0f;
	static constexpr float ASSAULT_DEATH = -8.0f;          // Lower penalty (expected risk)

	// === DEFEND REWARDS ===
	static constexpr float DEFEND_KILL = 10.0f;
	static constexpr float DEFEND_HOLD_POSITION = 0.3f;    // per second at objective
	static constexpr float DEFEND_SUPPRESS = 3.0f;
	static constexpr float DEFEND_ABANDON = -2.0f;         // per second away from position
	static constexpr float DEFEND_DEATH = -12.0f;          // Higher penalty (anchor loss)

	// === SUPPORT REWARDS ===
	static constexpr float SUPPORT_ALLY_SURVIVES = 15.0f;
	static constexpr float SUPPORT_THREAT_KILL = 12.0f;    // Kill enemy threatening ally
	static constexpr float SUPPORT_POSITION = 0.2f;        // per second at optimal distance
	static constexpr float SUPPORT_ALLY_DIES = -20.0f;     // Failed mission
	static constexpr float SUPPORT_DRAW_AGGRO = 5.0f;      // Successfully distracted enemy

	// === RETREAT REWARDS ===
	static constexpr float RETREAT_DISTANCE = 0.3f;        // per second increasing distance
	static constexpr float RETREAT_SAFE_ZONE = 10.0f;
	static constexpr float RETREAT_COVER_FIRE = 3.0f;
	static constexpr float RETREAT_SURVIVAL = 5.0f;        // per episode
	static constexpr float RETREAT_DEATH = -15.0f;         // Failed retreat

	// === SHARED REWARDS ===
	static constexpr float COMBINED_FIRE = 10.0f;
	static constexpr float REACH_COVER = 5.0f;
	static constexpr float WASTED_AMMO = -1.0f;
};

/**
 * MCTS individual assignment score modifiers (v5.0)
 */
USTRUCT(BlueprintType)
struct FAssignmentScoreConfig
{
	GENERATED_BODY()

	// Health thresholds
	static constexpr float HEALTHY_THRESHOLD = 0.7f;       // Above this → prefer Assault
	static constexpr float WOUNDED_THRESHOLD = 0.3f;       // Below this → prefer Retreat
	static constexpr float CRITICAL_THRESHOLD = 0.15f;     // Below this → force Retreat

	// Assignment bonuses
	static constexpr float ASSAULT_HEALTHY_BONUS = 0.3f;
	static constexpr float ASSAULT_AMMO_BONUS = 0.2f;
	static constexpr float RETREAT_WOUNDED_BONUS = 0.4f;
	static constexpr float SUPPORT_ALLY_CRITICAL_BONUS = 0.3f;
	static constexpr float DEFEND_COVER_BONUS = 0.2f;

	// Support triggers
	static constexpr float ALLY_NEEDS_HELP_HEALTH = 0.5f;  // Health threshold
	static constexpr float ALLY_SURROUNDED_COUNT = 3;      // Enemies within range
	static constexpr float ALLY_SURROUNDED_RANGE = 1000.0f; // 10m in UE units
};

