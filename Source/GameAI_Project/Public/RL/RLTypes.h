// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Observation/ObservationElement.h"
#include "RLTypes.generated.h"

/**
 * Position selection for macro actions (v4.0)
 */
UENUM(BlueprintType)
enum class ETacticalPosition : uint8
{
	Hold          UMETA(DisplayName = "Hold Position"),
	ForwardCover  UMETA(DisplayName = "Advance to Forward Cover"),
	Retreat       UMETA(DisplayName = "Retreat to Safe Position"),
	FlankLeft     UMETA(DisplayName = "Flank Left"),
	FlankRight    UMETA(DisplayName = "Flank Right"),
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
 * Stance options (v4.0)
 */
UENUM(BlueprintType)
enum class EStance : uint8
{
	Stand  UMETA(DisplayName = "Standing"),
	Crouch UMETA(DisplayName = "Crouching"),
	Prone  UMETA(DisplayName = "Prone")
};

/**
 * Macro action space for v4.0 squad tactics
 * High-level decisions: WHERE to go, WHO to shoot, HOW to engage
 * Engine handles physics: NavMesh pathfinding, auto-aim, stance animation
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

	// Stance: Body posture for visibility vs mobility trade-off
	UPROPERTY(BlueprintReadWrite, Category = "Action|Tactical")
	EStance Stance = EStance::Stand;

	FMacroAction()
		: PositionChoice(ETacticalPosition::Hold)
		, TargetIndex(-1)
		, FireMode(EFireMode::HoldFire)
		, Stance(EStance::Stand)
	{
	}

	FMacroAction(ETacticalPosition Pos, int32 Target, EFireMode Fire, EStance St)
		: PositionChoice(Pos)
		, TargetIndex(Target)
		, FireMode(Fire)
		, Stance(St)
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
 * Action space mask for spatial awareness
 * Constrains action space based on environment (indoor, cover, edges)
 * Prevents invalid actions (sprinting into walls, falling off cliffs)
 */
USTRUCT(BlueprintType)
struct FActionSpaceMask
{
	GENERATED_BODY()

	// Movement constraints
	UPROPERTY(BlueprintReadWrite, Category = "Mask|Movement")
	bool bLockMovementX = false;  // Block lateral movement (narrow corridor)

	UPROPERTY(BlueprintReadWrite, Category = "Mask|Movement")
	bool bLockMovementY = false;  // Block forward/back movement (cliff edge)

	UPROPERTY(BlueprintReadWrite, Category = "Mask|Movement")
	float MaxSpeed = 1.0f;  // Speed limit (0.3 = walk only, 1.0 = sprint allowed)

	// Aiming constraints (degrees)
	UPROPERTY(BlueprintReadWrite, Category = "Mask|Aiming")
	float MinYaw = -180.0f;  // Minimum horizontal aim angle

	UPROPERTY(BlueprintReadWrite, Category = "Mask|Aiming")
	float MaxYaw = 180.0f;  // Maximum horizontal aim angle

	UPROPERTY(BlueprintReadWrite, Category = "Mask|Aiming")
	float MinPitch = -90.0f;  // Minimum vertical aim angle

	UPROPERTY(BlueprintReadWrite, Category = "Mask|Aiming")
	float MaxPitch = 90.0f;  // Maximum vertical aim angle

	// Action availability
	UPROPERTY(BlueprintReadWrite, Category = "Mask|Actions")
	bool bCanSprint = true;  // Allow sprinting (open area)

	UPROPERTY(BlueprintReadWrite, Category = "Mask|Actions")
	bool bForceCrouch = false;  // Force crouch (low ceiling)

	UPROPERTY(BlueprintReadWrite, Category = "Mask|Actions")
	bool bSafetyLock = false;  // Disable firing (friendly fire risk)

	FActionSpaceMask()
		: bLockMovementX(false)
		, bLockMovementY(false)
		, MaxSpeed(1.0f)
		, MinYaw(-180.0f)
		, MaxYaw(180.0f)
		, MinPitch(-90.0f)
		, MaxPitch(90.0f)
		, bCanSprint(true)
		, bForceCrouch(false)
		, bSafetyLock(false)
	{
	}
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
		: InputSize(78)  // 71 observation + 7 objective embedding
		, OutputSize(4)  // v4.0: 4 macro action heads (position, target, fire, stance)
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
