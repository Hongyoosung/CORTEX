// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Observation/ObservationElement.h"
#include "RLTypes.generated.h"

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
 * Macro action space for v4.0 squad tactics (Simplified - Stance removed)
 * High-level decisions: WHERE to go, WHO to shoot, WHEN to fire
 * Engine handles physics: NavMesh pathfinding, auto-aim
 */
USTRUCT(BlueprintType)
struct FMacroAction
{
	GENERATED_BODY()

	// Position: Which tactical location to move to (EQS candidate index)
	UPROPERTY(BlueprintReadWrite, Category = "Action|Tactical")
	ETacticalPosition PositionChoice = ETacticalPosition::Hold;

	// Target: Which enemy to engage (-1 = none, 0+ = enemy index)
	UPROPERTY(BlueprintReadWrite, Category = "Action|Tactical")
	int32 TargetIndex = -1;

	// Fire Mode: How to engage target
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
 * Tactical action space (v4.0 macro actions only)
 * High-level tactical decisions: WHERE to go, WHO to shoot, HOW to engage
 */
USTRUCT(BlueprintType)
struct FTacticalAction
{
	GENERATED_BODY()

	/** High-level tactical decision */
	UPROPERTY(BlueprintReadWrite, Category = "Action")
	FMacroAction MacroAction;

	FTacticalAction()
		: MacroAction()
	{
	}

	/** Create from macro action */
	explicit FTacticalAction(const FMacroAction& Macro)
		: MacroAction(Macro)
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
 * RL policy configuration
 */
USTRUCT(BlueprintType)
struct FRLPolicyConfig
{
	GENERATED_BODY()

	// Number of input features (71 observation + 7 objective = 78)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	int32 InputSize;

	// Number of output dimensions (4 macro action heads: position, target, fire, stance)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	int32 OutputSize;

	// Hidden layer sizes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	TArray<int32> HiddenLayers;

	// Learning rate for training
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	float LearningRate;

	// Discount factor (gamma)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	float DiscountFactor;

	// Epsilon for epsilon-greedy exploration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	float Epsilon;

	// Epsilon decay rate
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	float EpsilonDecay;

	// Minimum epsilon value
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	float MinEpsilon;

	// Path to ONNX model file
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	FString ModelPath;

	FRLPolicyConfig()
		: InputSize(74)  // 70 observation + 4 objective embedding
		, OutputSize(3)  // v4.0: 3 macro action heads (position, target, fire)
		, HiddenLayers({128, 128, 64})
		, LearningRate(0.0003f)
		, DiscountFactor(0.99f)
		, Epsilon(1.0f)
		, EpsilonDecay(0.995f)
		, MinEpsilon(0.05f)
		, ModelPath(TEXT(""))
	{
	}
};

/**
 * Reward components for tactical actions
 */
USTRUCT(BlueprintType)
struct FTacticalRewards
{
	GENERATED_BODY()

	// Combat rewards
	static constexpr float KILL_ENEMY = 10.0f;
	static constexpr float DAMAGE_ENEMY = 5.0f;
	static constexpr float SUPPRESS_ENEMY = 3.0f;
	static constexpr float TAKE_DAMAGE = -5.0f;
	static constexpr float DIE = -10.0f;

	// Tactical rewards
	static constexpr float REACH_COVER = 5.0f;
	static constexpr float MAINTAIN_FORMATION = 3.0f;
	static constexpr float FOLLOW_COMMAND = 2.0f;
	static constexpr float BREAK_FORMATION = -3.0f;
	static constexpr float IGNORE_COMMAND = -5.0f;

	// Support rewards
	static constexpr float RESCUE_ALLY = 10.0f;
	static constexpr float COVERING_FIRE = 5.0f;
	static constexpr float SHARE_AMMO = 3.0f;

	// Efficiency penalties
	static constexpr float WASTED_AMMO = -1.0f;
	static constexpr float OUT_OF_POSITION = -2.0f;
	static constexpr float IDLE_TOO_LONG = -1.0f;
};
