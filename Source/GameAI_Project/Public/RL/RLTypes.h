// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Team/Objective.h"
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

	// Action Space
	constexpr int32 NUM_STRATEGIES = 4;  // Assault, Defend, Support, Retreat
	constexpr int32 NUM_TARGETS = 11;    // 10 enemies + 1 no-target

	// Observation Space
	constexpr int32 OBSERVATION_SIZE = 68;  // 64 base + 4 objective context

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
// v6.0: Objective Context for RL Observation
// ============================================

// Forward declare UObjective (defined in Team/Objective.h)
class UObjective;

/**
 * Objective context provided to RL policy (v6.0)
 * Informs agent about their assigned objective from MCTS
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FObjectiveContext
{
	GENERATED_BODY()

	/** Assigned objective type */
	UPROPERTY(BlueprintReadWrite, Category = "Objective")
	EObjectiveType Type = EObjectiveType::None;

	/** Normalized distance to objective [0,1] */
	UPROPERTY(BlueprintReadWrite, Category = "Objective")
	float Distance = 0.0f;

	/** Normalized 2D direction to objective */
	UPROPERTY(BlueprintReadWrite, Category = "Objective")
	FVector2D Direction = FVector2D::ZeroVector;

	/** Target actor (enemy, capture point, ally, etc.) */
	UPROPERTY(BlueprintReadWrite, Category = "Objective")
	TObjectPtr<AActor> TargetActor = nullptr;

	/** Objective priority [0-10] */
	UPROPERTY(BlueprintReadWrite, Category = "Objective")
	int32 Priority = 5;

	/** Convert to feature array for neural network (4 features) */
	TArray<float> ToFeatureVector() const
	{
		// Encode type as normalized value [0, 0.25, 0.5, 0.75, 1.0]
		float TypeEncoded = static_cast<float>(Type) / static_cast<float>(EObjectiveType::Retreat);

		return {
			TypeEncoded,
			Distance,
			(float)Direction.X,
			(float)Direction.Y
		};
	}
};

// ============================================
// v6.0: MCTS Assignment Result
// ============================================

/**
 * Result of MCTS objective assignment (v6.0)
 * Maps agents to objectives with confidence metrics
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FObjectiveAssignment
{
	GENERATED_BODY()

	/** Agent-to-objective mapping */
	UPROPERTY(BlueprintReadWrite, Category = "Assignment")
	TMap<TObjectPtr<AActor>, TObjectPtr<UObjective>> AgentToObjective;

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
 * Macro action space for v6.0 (Strategy only, execution is rules-based)
 */
USTRUCT(BlueprintType)
struct FMacroAction
{
	GENERATED_BODY()

	/** Strategy: High-level approach to current objective */
	UPROPERTY(BlueprintReadWrite, Category = "Action")
	EStrategyType Strategy = EStrategyType::Assault;

	FMacroAction() : Strategy(EStrategyType::Assault) {}
	explicit FMacroAction(EStrategyType InStrategy) : Strategy(InStrategy) {}
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
	int32 InputSize = 68;  // v6.0: 64 base + 4 objective context

	/** Policy output size (strategy logits) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	int32 PolicyOutputSize = 4;  // Assault, Defend, Support, Retreat

	/** Hidden layer sizes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	TArray<int32> HiddenLayers = {128, 128, 64};

	/** Use ONNX model (vs fallback heuristic) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	bool bUseONNXModel = true;

	/** ONNX model path */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	FString ModelPath = TEXT("Models/cortex_policy_v6.onnx");
};
