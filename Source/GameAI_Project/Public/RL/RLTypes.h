// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RLTypes.generated.h"

// Forward declarations (v6.0 - avoid circular dependency)
struct FObservationElement;

/**
 * RLConfig Namespace (v6.0)
 * Single source of truth for RL training parameters
 * CRITICAL: These values MUST match Python training environment
 * Run sync script before training: python tools/sync_config_from_cpp.py
 */
namespace RLConfig {
	// === CRITICAL: These values MUST match Python training environment ===

	// Movement (must match UE5 CharacterMovement)
	constexpr float AGENT_WALK_SPEED = 600.0f;      // cm/s
	constexpr float AGENT_RUN_SPEED = 900.0f;
	constexpr float AGENT_SPRINT_SPEED = 1200.0f;

	// Perception (must match UE5 AIPerception)
	constexpr float PERCEPTION_RADIUS = 3000.0f;    // cm
	constexpr int32 RAYCAST_COUNT = 16;
	constexpr float RAYCAST_LENGTH = 2000.0f;       // cm
	constexpr float RAYCAST_ANGLE_SPREAD = 180.0f;  // degrees

	// Combat (must match UE5 damage system)
	constexpr float BASE_DAMAGE = 10.0f;
	constexpr float MAX_HEALTH = 100.0f;
	constexpr float FIRE_RATE = 0.1f;               // seconds per shot

	// Observation Normalization
	constexpr float MAX_DISTANCE_NORMALIZATION = 5000.0f;  // cm
	constexpr float MAX_VELOCITY_NORMALIZATION = 1200.0f;

	// Action Space (v8.0: Tactical Parameters + Combat Parameters)
	constexpr int32 NUM_STRATEGIES = 4;  // Assault, Defend, Support, Retreat (MCTS-assigned, not RL)
	constexpr int32 NUM_TACTICAL_PARAMS = 4;  // Aggression, CoverPreference, SpreadDistance, RiskTolerance
	constexpr int32 NUM_COMBAT_CHOICES = 2;   // TargetPriority: Closest, LowestHP

	// Observation Space (v9.0: Added objective context)
	constexpr int32 OBSERVATION_BASE_SIZE = 52;  // Base observation features (46 + 6 objective context)
	constexpr int32 OBSERVATION_SIZE = 56;       // 52 base + 4 strategy context (one-hot from MCTS)

	// === END CRITICAL SECTION ===
}

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
 * Position selection for macro actions (v5.0)
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

	/** Reference to the closest ally actor (for targeting decisions) */
	UPROPERTY(BlueprintReadWrite, Category = "Support")
	TObjectPtr<AActor> ClosestAlly = nullptr;



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

// ============================================
// v8.0: Strategy Assignment (MCTS Output)
// MCTS now assigns Strategies directly (not Missions)
// ============================================

/**
 * Strategy assignment from MCTS (v8.0)
 * MCTS assigns both the strategy and the target objective for each agent
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FStrategyAssignment
{
	GENERATED_BODY()

	/** Assigned agent */
	UPROPERTY(BlueprintReadWrite, Category = "Assignment")
	TObjectPtr<AActor> Agent = nullptr;

	/** Assigned strategy type */
	UPROPERTY(BlueprintReadWrite, Category = "Assignment")
	EStrategyType Strategy = EStrategyType::Assault;

	/** Assignment priority [0-10] */
	UPROPERTY(BlueprintReadWrite, Category = "Assignment")
	int32 Priority = 5;

	/** MCTS-estimated value of this assignment [-1, 1] */
	UPROPERTY(BlueprintReadWrite, Category = "Assignment")
	float ExpectedValue = 0.0f;

	/** Total MCTS visit count (confidence) */
	UPROPERTY(BlueprintReadWrite, Category = "Assignment")
	int32 VisitCount = 0;

	/** Timestamp of assignment */
	UPROPERTY(BlueprintReadWrite, Category = "Assignment")
	float Timestamp = 0.0f;
};

/**
 * Objective context for RL observation (v8.0)
 * Provides information about the target objective for strategy execution
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FObjectiveContext
{
	GENERATED_BODY()

	/** Target objective actor */
	UPROPERTY(BlueprintReadWrite, Category = "Objective")
	TObjectPtr<class AObjectiveActor> TargetObjective = nullptr;

	/** Normalized distance to objective [0, 1] */
	UPROPERTY(BlueprintReadWrite, Category = "Objective")
	float Distance = 0.0f;

	/** Normalized 2D direction to objective */
	UPROPERTY(BlueprintReadWrite, Category = "Objective")
	FVector2D Direction = FVector2D::ZeroVector;

	/** Convert to feature vector for neural network (4 features total) */
	TArray<float> ToFeatureVector() const
	{
		// For v8.0: Return objective context as 4 features
		// [type_encoded, distance, direction.x, direction.y]
		// Type encoding: 0.0 = friendly objective, 1.0 = hostile objective
		return {
			0.0f,  // Type encoding (to be set by caller based on team ownership)
			Distance,
			static_cast<float>(Direction.X),
			static_cast<float>(Direction.Y)
		};
	}
};

// ============================================
// v8.0: Tactical Parameters (RL → EQS Modulation)
// ============================================

/**
 * Target priority for combat decisions (v8.0)
 */
UENUM(BlueprintType)
enum class ETargetPriority : uint8
{
	Closest   UMETA(DisplayName = "Closest Enemy"),
	LowestHP  UMETA(DisplayName = "Lowest HP Enemy"),
	COUNT     UMETA(Hidden)
};

/**
 * Tactical parameters for EQS weight modulation (v8.0)
 * RL outputs these continuous values to control tactical positioning behavior
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FTacticalParameters
{
	GENERATED_BODY()

	/** Aggression level [0,1]: 0=passive, 1=aggressive */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float Aggression = 0.5f;

	/** Cover preference [0,1]: 0=ignore cover, 1=prioritize cover */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float CoverPreference = 0.5f;

	/** Formation spread [0,1]: 0=tight formation, 1=spread out */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float SpreadDistance = 0.5f;

	/** Risk tolerance [0,1]: 0=retreat early, 1=fight to death */
	UPROPERTY(BlueprintReadWrite, Category = "Tactical")
	float RiskTolerance = 0.5f;

	FTacticalParameters() = default;
	FTacticalParameters(float InAggression, float InCover, float InSpread, float InRisk)
		: Aggression(InAggression)
		, CoverPreference(InCover)
		, SpreadDistance(InSpread)
		, RiskTolerance(InRisk)
	{}

	/** Clamp all values to [0,1] range */
	void Clamp()
	{
		Aggression = FMath::Clamp(Aggression, 0.0f, 1.0f);
		CoverPreference = FMath::Clamp(CoverPreference, 0.0f, 1.0f);
		SpreadDistance = FMath::Clamp(SpreadDistance, 0.0f, 1.0f);
		RiskTolerance = FMath::Clamp(RiskTolerance, 0.0f, 1.0f);
	}
};

/**
 * Combat parameters for target selection and engagement (v8.0)
 * RL selects discrete combat actions (no learned aiming, only target priority)
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FCombatParameters
{
	GENERATED_BODY()

	/** Target priority selection (2 discrete choices) */
	UPROPERTY(BlueprintReadWrite, Category = "Combat")
	ETargetPriority Priority = ETargetPriority::Closest;

	FCombatParameters() = default;
	explicit FCombatParameters(ETargetPriority InPriority)
		: Priority(InPriority)
	{}
};

/**
 * Macro action space for v8.0 (Tactical Parameters + Combat Parameters)
 * v8.0: MCTS assigns strategies, RL outputs tactical and combat parameters
 */
USTRUCT(BlueprintType)
struct FMacroAction
{
	GENERATED_BODY()

	/** v8.0: Tactical parameters for EQS weight modulation (4 continuous values) */
	UPROPERTY(BlueprintReadWrite, Category = "Action")
	FTacticalParameters TacticalParams;

	/** v8.0: Combat parameters for target selection (2 discrete choices) */
	UPROPERTY(BlueprintReadWrite, Category = "Action")
	FCombatParameters CombatParams;

	FMacroAction() = default;
	FMacroAction(FTacticalParameters InTactical, FCombatParameters InCombat)
		: TacticalParams(InTactical)
		, CombatParams(InCombat)
	{}
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

// ============================================
// v6.0: RL Policy Configuration (Single-Head)
// ============================================

/**
 * RL Policy configuration (v6.0 - Single Head)
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FRLPolicyConfig
{
	GENERATED_BODY()

	/** Input size (observation features) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	int32 InputSize = 56;  // v9.0: 52 base + 4 strategy context

	/** Policy output size (strategy logits) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	int32 PolicyOutputSize = 4;  // Assault, Defend, Support, Retreat

	/** Hidden layer sizes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	TArray<int32> HiddenLayers = {256, 256, 128};

	/** Use ONNX model (vs fallback heuristic) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	bool bUseONNXModel = true;

	/** ONNX model path */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	FString ModelPath = TEXT("Models/cortex_policy_v6.onnx");
};
