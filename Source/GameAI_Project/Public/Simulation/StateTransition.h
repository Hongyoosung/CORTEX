// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Team/Mission.h"
#include "RL/RLTypes.h"
#include "StateTransition.generated.h"

/**
 * Individual agent state delta (predicted changes)
 */
USTRUCT(BlueprintType)
struct FAgentStateDelta
{
	GENERATED_BODY()

	// Health change (damage or healing)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	float HealthDelta = 0.0f;

	// Position change (movement delta)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	FVector PositionDelta = FVector::ZeroVector;

	// Rotation change
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	FRotator RotationDelta = FRotator::ZeroRotator;

	// Velocity change
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	FVector VelocityDelta = FVector::ZeroVector;

	// Ammo consumption
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	int32 AmmoDelta = 0;

	// Status effect changes (predicted)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	bool bIsStunnedNext = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	bool bIsSupressedNext = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	bool bIsDead = false;

	// Predicted action outcome confidence [0, 1]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	float Confidence = 1.0f;

	FAgentStateDelta()
		: HealthDelta(0.0f)
		, PositionDelta(FVector::ZeroVector)
		, RotationDelta(FRotator::ZeroRotator)
		, VelocityDelta(FVector::ZeroVector)
		, AmmoDelta(0)
		, bIsStunnedNext(false)
		, bIsSupressedNext(false)
		, bIsDead(false)
		, Confidence(1.0f)
	{}
};

/**
 * Team-level state delta (aggregate predicted changes)
 */
USTRUCT(BlueprintType)
struct FTeamStateDelta
{
	GENERATED_BODY()

	// Individual agent deltas (indexed by agent)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	TArray<FAgentStateDelta> AgentDeltas;

	// Team-level metrics changes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	float TeamHealthDelta = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	int32 AliveCountDelta = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	float TeamCohesionDelta = 0.0f;

	// Combat outcome predictions
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	int32 PredictedKills = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	int32 PredictedDeaths = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	float PredictedDamageDealt = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	float PredictedDamageTaken = 0.0f;

	// Mission progress delta
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	float MissionProgressDelta = 0.0f;

	// Tactical outcome predictions
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	float EngagementOutcome = 0.0f;  // Win probability [-1, 1]

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	float MissionProgress = 0.0f;  // Mission completion estimate [0, 1]

	// Time elapsed for this transition (seconds)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	float DeltaTime = 1.0f;

	// Model confidence in this prediction [0, 1]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Transition")
	float Confidence = 1.0f;

	FTeamStateDelta()
		: TeamHealthDelta(0.0f)
		, AliveCountDelta(0)
		, TeamCohesionDelta(0.0f)
		, PredictedKills(0)
		, PredictedDeaths(0)
		, PredictedDamageDealt(0.0f)
		, PredictedDamageTaken(0.0f)
		, MissionProgressDelta(0.0f)
		, EngagementOutcome(0.0f)
		, MissionProgress(0.0f)
		, DeltaTime(1.0f)
		, Confidence(1.0f)
	{}

	void Reset()
	{
		AgentDeltas.Empty();
		TeamHealthDelta = 0.0f;
		AliveCountDelta = 0;
		TeamCohesionDelta = 0.0f;
		PredictedKills = 0;
		PredictedDeaths = 0;
		PredictedDamageDealt = 0.0f;
		PredictedDamageTaken = 0.0f;
		MissionProgressDelta = 0.0f;
		EngagementOutcome = 0.0f;
		MissionProgress = 0.0f;
		DeltaTime = 1.0f;
		Confidence = 1.0f;
	}
};

/**
 * State transition training sample (for world model supervised learning)
 */
USTRUCT(BlueprintType)
struct FStateTransitionSample
{
	GENERATED_BODY()

	// State at time t (flattened team observation)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	TArray<float> StateBefore;

	/** v6.0: Simplified to strategy-only actions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	TArray<FMacroAction> Actions;

	// State at time t+1 (flattened team observation)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	TArray<float> StateAfter;

	// Actual delta (ground truth)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	FTeamStateDelta ActualDelta;

	// Timestamp
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	float Timestamp = 0.0f;

	// Game outcome (for weighting important transitions)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	float GameOutcome = 0.0f;

	FStateTransitionSample()
		: Timestamp(0.0f)
		, GameOutcome(0.0f)
	{}
};

/**
 * Action encoding for world model input (v3.0)
 */
USTRUCT(BlueprintType)
struct FActionEncoding
{
	GENERATED_BODY()

	// Mission type (one-hot encoded)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encoding")
	TArray<float> MissionTypeOneHot;

	// Mission parameters (normalized)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encoding")
	FVector TargetLocationNormalized = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encoding")
	bool bHasTargetActor = false;

	// Tactical action (one-hot encoded)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encoding")
	TArray<float> TacticalActionOneHot;

	FActionEncoding()
		: TargetLocationNormalized(FVector::ZeroVector)
		, bHasTargetActor(false)
	{}

	// Encode Mission to feature vector
	static FActionEncoding EncodeMission(const UMission* Mission);

	/** v6.0: Encode strategy action (simplified from 8D to 1D discrete) */
	static TArray<float> EncodeAction(const FMacroAction& Action);

	// Flatten to single feature vector
	TArray<float> Flatten() const;
};

/**
 * World model prediction result
 */
USTRUCT(BlueprintType)
struct FWorldModelPrediction
{
	GENERATED_BODY()

	// Predicted state delta
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prediction")
	FTeamStateDelta PredictedDelta;

	// Prediction confidence [0, 1]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prediction")
	float Confidence = 0.0f;

	// Inference time (ms)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prediction")
	float InferenceTimeMs = 0.0f;

	// Model version used
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prediction")
	int32 ModelVersion = 0;

	FWorldModelPrediction()
		: Confidence(0.0f)
		, InferenceTimeMs(0.0f)
		, ModelVersion(0)
	{}
};
