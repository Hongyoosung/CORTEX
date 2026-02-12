// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Team/TeamWorldState.h"
#include "Types/StrategyTypes.h"
#include "Types/RewardTypes.h"
#include "TeamWorldModelTypes.generated.h"

/**
 * Batch Input for Team-Level World Model
 * Used by centralized MCTS to predict outcomes of multiple tactical plays
 *
 * Used by UTeamWorldModel for batch inference during MCTS planning.
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FTeamBatchInput
{
	GENERATED_BODY()

	/**
	 * Current team states for each simulation
	 * Shape: [BatchSize, 60-dim]
	 */
	UPROPERTY(BlueprintReadWrite, Category = "WorldModel")
	TArray<FTeamWorldState> CurrentStates;

	/**
	 * Selected tactical plays for each simulation
	 * Shape: [BatchSize]
	 */
	UPROPERTY(BlueprintReadWrite, Category = "WorldModel")
	TArray<ETacticalPlay> TacticalPlays;

	FTeamBatchInput() = default;

	int32 GetBatchSize() const { return CurrentStates.Num(); }

	bool IsValid() const
	{
		return CurrentStates.Num() == TacticalPlays.Num() && CurrentStates.Num() > 0;
	}
};

/**
 * Batch Output for Team-Level World Model
 * Predicted next states and rewards for each simulation
 *
 * Returned by UTeamWorldModel after batch inference during MCTS planning.
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FTeamBatchOutput
{
	GENERATED_BODY()

	/**
	 * Predicted next team states after executing tactical plays
	 * Shape: [BatchSize, 60-dim]
	 */
	UPROPERTY(BlueprintReadWrite, Category = "WorldModel")
	TArray<FTeamWorldState> PredictedStates;

	/**
	 * Multi-objective rewards for team-level outcomes
	 * Shape: [BatchSize]
	 */
	UPROPERTY(BlueprintReadWrite, Category = "WorldModel")
	TArray<FCompositeReward> Rewards;

	/**
	 * Prediction confidence scores
	 * Shape: [BatchSize]
	 * 1.0 = high confidence, 0.0 = low confidence
	 */
	UPROPERTY(BlueprintReadWrite, Category = "WorldModel")
	TArray<float> Confidences;

	FTeamBatchOutput() = default;

	int32 GetBatchSize() const { return PredictedStates.Num(); }

	bool IsValid() const
	{
		return PredictedStates.Num() == Rewards.Num()
		    && Rewards.Num() == Confidences.Num()
		    && PredictedStates.Num() > 0;
	}
};

/**
 * FTeamReward - Team-level multi-objective reward structure
 *
 * Aggregates individual agent rewards into team-wide performance metrics.
 * Used for data collection and training, but not by the production inference path.
 *
 * NOTE: UTeamWorldModel uses FCompositeReward for inference.
 * FTeamReward is kept for backward compatibility with training data collectors.
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FTeamReward
{
	GENERATED_BODY()

	/** Win probability estimate [0.0-1.0] - averaged across agents */
	UPROPERTY(BlueprintReadWrite, Category = "TeamReward")
	float WinProb = 0.5f;

	/** Team-wide health change (sum of individual deltas) */
	UPROPERTY(BlueprintReadWrite, Category = "TeamReward")
	float TeamHealthDelta = 0.0f;

	/** Objective score (capture points, control, etc.) - strategy-weighted */
	UPROPERTY(BlueprintReadWrite, Category = "TeamReward")
	float ObjectiveScore = 0.0f;

	/** Diversity bonus for varied team composition
	 * 1.0 if ≥3 unique strategies, 0.5 if 2, 0.0 if 1 */
	UPROPERTY(BlueprintReadWrite, Category = "TeamReward")
	float DiversityBonus = 0.0f;

	/** Scalarized total reward for MCTS value calculation */
	UPROPERTY(BlueprintReadWrite, Category = "TeamReward")
	float TotalReward = 0.0f;

	FTeamReward() = default;

	/**
	 * Scalarize multi-objective reward into single value
	 * Weights: WinProb=2.0, TeamHealth=0.01, Objective=1.5, Diversity=0.5
	 */
	void Scalarize()
	{
		TotalReward = WinProb * 2.0f
		            + TeamHealthDelta * 0.01f
		            + ObjectiveScore * 1.5f
		            + DiversityBonus * 0.5f;
	}

	FString ToString() const
	{
		return FString::Printf(
			TEXT("TeamReward(WinProb=%.3f, HealthDelta=%.1f, ObjScore=%.2f, Diversity=%.2f, Total=%.2f)"),
			WinProb, TeamHealthDelta, ObjectiveScore, DiversityBonus, TotalReward
		);
	}
};

/**
 * FTeamStatePrediction - Output of team-level world model prediction
 *
 * Contains predicted next state, aggregated reward, and confidence.
 * Legacy structure kept for backward compatibility.
 *
 * NOTE: UTeamWorldModel uses FTeamBatchOutput for batch inference.
 * FTeamStatePrediction can be used for single predictions or legacy code.
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FTeamStatePrediction
{
	GENERATED_BODY()

	/** Predicted next team state after action */
	UPROPERTY(BlueprintReadWrite, Category = "TeamPrediction")
	FTeamWorldState NextState;

	/** Aggregated team-level reward */
	UPROPERTY(BlueprintReadWrite, Category = "TeamPrediction")
	FTeamReward Reward;

	/** Confidence score [0.0-1.0] - minimum of individual agent confidences */
	UPROPERTY(BlueprintReadWrite, Category = "TeamPrediction")
	float Confidence = 0.5f;

	FTeamStatePrediction() = default;

	FString ToString() const
	{
		return FString::Printf(
			TEXT("TeamPrediction(Confidence=%.2f, %s)"),
			Confidence, *Reward.ToString()
		);
	}
};
