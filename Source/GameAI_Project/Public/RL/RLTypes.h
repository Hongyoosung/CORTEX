// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Observation/ObservationElement.h"
#include "RLTypes.generated.h"

/**
 * Atomic action space for v3.0 combat system
 * Replaces 16 discrete tactical actions with 7-dimensional continuous space
 * Easier for world model prediction and more expressive
 */
USTRUCT(BlueprintType)
struct FTacticalAction
{
	GENERATED_BODY()

	// Movement (continuous, 2D) - normalized direction in agent's local space
	UPROPERTY(BlueprintReadWrite, Category = "Action|Movement")
	FVector2D MoveDirection = FVector2D::ZeroVector;  // [-1,1] x [-1,1]

	UPROPERTY(BlueprintReadWrite, Category = "Action|Movement")
	float MoveSpeed = 1.0f;  // [0,1] - percentage of max speed

	// Aiming (continuous, 2D) - normalized direction for look target
	UPROPERTY(BlueprintReadWrite, Category = "Action|Aiming")
	FVector2D LookDirection = FVector2D::ZeroVector;  // [-1,1] x [-1,1], normalized

	// Discrete actions (one-hot)
	UPROPERTY(BlueprintReadWrite, Category = "Action|Combat")
	bool bFire = false;

	UPROPERTY(BlueprintReadWrite, Category = "Action|Stance")
	bool bCrouch = false;

	// NOTE: Removed from action space (was dimension 7), kept for backwards compatibility
	UPROPERTY(BlueprintReadWrite, Category = "Action|Ability")
	bool bUseAbility = false;

	UPROPERTY(BlueprintReadWrite, Category = "Action|Ability")
	int32 AbilityID = 0;

	FTacticalAction()
		: MoveDirection(FVector2D::ZeroVector)
		, MoveSpeed(1.0f)
		, LookDirection(FVector2D::ZeroVector)
		, bFire(false)
		, bCrouch(false)
		, bUseAbility(false)
		, AbilityID(0)
	{
	}

	// Total dimensions: 7 (move_x, move_y, speed, look_x, look_y, fire, crouch)
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
 * Experience tuple for reinforcement learning (v3.0 - Atomic Actions)
 * Represents a single transition in the MDP
 */
USTRUCT(BlueprintType)
struct FRLExperience
{
	GENERATED_BODY()

	// Current state (71 features)
	UPROPERTY(BlueprintReadWrite, Category = "RL")
	FObservationElement State;

	// Action taken in this state (atomic action)
	UPROPERTY(BlueprintReadWrite, Category = "RL")
	FTacticalAction Action;

	// Current objective context (7-element one-hot)
	UPROPERTY(BlueprintReadWrite, Category = "RL")
	TArray<float> ObjectiveEmbedding;

	// Immediate reward received
	UPROPERTY(BlueprintReadWrite, Category = "RL")
	float Reward;

	// Next state after taking action (71 features)
	UPROPERTY(BlueprintReadWrite, Category = "RL")
	FObservationElement NextState;

	// Next objective context
	UPROPERTY(BlueprintReadWrite, Category = "RL")
	TArray<float> NextObjectiveEmbedding;

	// Is this a terminal state?
	UPROPERTY(BlueprintReadWrite, Category = "RL")
	bool bTerminal;

	// Timestamp of experience
	UPROPERTY(BlueprintReadWrite, Category = "RL")
	float Timestamp;

	// Additional context data (optional)
	UPROPERTY(BlueprintReadWrite, Category = "RL")
	TMap<FString, float> ContextData;

	// MCTS uncertainty metrics (v3.0 Sprint 3 - Curriculum Learning)
	// Higher values indicate MCTS struggled with this scenario → prioritize for training
	UPROPERTY(BlueprintReadWrite, Category = "RL|Curriculum")
	float MCTSValueVariance;

	UPROPERTY(BlueprintReadWrite, Category = "RL|Curriculum")
	float MCTSPolicyEntropy;

	UPROPERTY(BlueprintReadWrite, Category = "RL|Curriculum")
	float MCTSVisitCount;

	FRLExperience()
		: Reward(0.0f)
		, bTerminal(false)
		, Timestamp(0.0f)
		, MCTSValueVariance(0.0f)
		, MCTSPolicyEntropy(0.0f)
		, MCTSVisitCount(0.0f)
	{
		ObjectiveEmbedding.Init(0.0f, 7);
		NextObjectiveEmbedding.Init(0.0f, 7);
	}

	FRLExperience(const FObservationElement& InState, const FTacticalAction& InAction, float InReward, const FObservationElement& InNextState, bool bInTerminal)
		: State(InState)
		, Action(InAction)
		, Reward(InReward)
		, NextState(InNextState)
		, bTerminal(bInTerminal)
		, Timestamp(0.0f)
	{
		ObjectiveEmbedding.Init(0.0f, 7);
		NextObjectiveEmbedding.Init(0.0f, 7);
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

	// Number of output dimensions (7 atomic action dimensions)
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
		, OutputSize(7)  // 7 atomic action dimensions
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
