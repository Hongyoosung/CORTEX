// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Team/TeamState.h"
#include "Types/MocTypes.h"
#include "AI/Models/MocAgentWorldModel.h"
#include "MocTeamWorldModel.generated.h"

/**
 * FTeamReward - Team-level multi-objective reward structure
 *
 * Aggregates individual agent rewards into team-wide performance metrics.
 * Used by centralized MCTS for evaluating tactical plays.
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
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FTeamStatePrediction
{
	GENERATED_BODY()

	/** Predicted next team state after action */
	UPROPERTY(BlueprintReadWrite, Category = "TeamPrediction")
	FTeamState NextState;

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

/**
 * FTeamBatchInput - Batch input for MCTS leaf expansion
 *
 * Allows predicting multiple team state transitions simultaneously.
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FTeamBatchInput
{
	GENERATED_BODY()

	/** Array of current team states [BatchSize] */
	UPROPERTY(BlueprintReadWrite, Category = "TeamBatch")
	TArray<FTeamState> CurrentStates;

	/** Array of selected tactical plays [BatchSize] */
	UPROPERTY(BlueprintReadWrite, Category = "TeamBatch")
	TArray<ETacticalPlay> SelectedPlays;

	FTeamBatchInput() = default;

	int32 GetBatchSize() const { return CurrentStates.Num(); }

	bool IsValid() const
	{
		return CurrentStates.Num() == SelectedPlays.Num() && CurrentStates.Num() > 0;
	}
};

/**
 * FTeamBatchOutput - Batch output for MCTS leaf expansion
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FTeamBatchOutput
{
	GENERATED_BODY()

	/** Predicted next states [BatchSize] */
	UPROPERTY(BlueprintReadWrite, Category = "TeamBatch")
	TArray<FTeamState> PredictedStates;

	/** Team rewards [BatchSize] */
	UPROPERTY(BlueprintReadWrite, Category = "TeamBatch")
	TArray<FTeamReward> Rewards;

	/** Confidence scores [BatchSize] */
	UPROPERTY(BlueprintReadWrite, Category = "TeamBatch")
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
 * UMocTeamWorldModel - MOC v10.2 Team-Level World Model Orchestrator
 *
 * Architecture: Hybrid Wrapper Pattern
 * Wraps existing UMocAgentWorldModel (56-dim agent observations) and aggregates
 * predictions into team-level state transitions (60-dim FTeamState).
 *
 * Flow:
 * 1. FTeamState + ETacticalPlay → Decompose → 5 × EStrategyType
 * 2. Convert → 5 × FObservation (56-dim each)
 * 3. Generate → 5 × FTacticalOption (target positions per strategy)
 * 4. Batch Predict → UMocAgentWorldModel::PredictBatch()
 * 5. Aggregate → FTeamState (next state) + FTeamReward
 *
 * Performance Target: 3-5ms per prediction (within 15ms MCTS budget)
 *
 * Usage:
 * ```cpp
 * UMocTeamWorldModel* TeamModel = NewObject<UMocTeamWorldModel>();
 * TeamModel->Initialize(AgentWorldModel);
 *
 * FTeamStatePrediction Prediction = TeamModel->PredictTeamTransition(
 *     CurrentState,
 *     ETacticalPlay::AggressivePush
 * );
 * ```
 */
UCLASS(Blueprintable, BlueprintType)
class GAMEAI_PROJECT_API UMocTeamWorldModel : public UObject
{
	GENERATED_BODY()

public:
	UMocTeamWorldModel();

	//========================================
	// Initialization
	//========================================

	/**
	 * Initialize with agent-level world model
	 * @param InAgentWorldModel - Pre-initialized UMocAgentWorldModel for individual agents
	 * @return True if initialization successful
	 */
	UFUNCTION(BlueprintCallable, Category = "TeamWorldModel")
	bool Initialize(UMocAgentWorldModel* InAgentWorldModel);

	//========================================
	// Main Prediction Interface
	//========================================

	/**
	 * Predict team state transition from tactical play
	 *
	 * @param CurrentState - Current global team state (60-dim)
	 * @param SelectedPlay - Tactical play to evaluate
	 * @return Predicted next state + reward + confidence
	 *
	 * Performance: ~3-5ms per call
	 */
	UFUNCTION(BlueprintCallable, Category = "TeamWorldModel")
	FTeamStatePrediction PredictTeamTransition(
		const FTeamState& CurrentState,
		ETacticalPlay SelectedPlay
	);

	/**
	 * Batch prediction for MCTS leaf expansion
	 *
	 * @param BatchInput - Multiple (state, play) pairs
	 * @return Batch predictions for all inputs
	 *
	 * Performance: ~5-8ms for 8 predictions
	 */
	UFUNCTION(BlueprintCallable, Category = "TeamWorldModel")
	FTeamBatchOutput PredictBatch(const FTeamBatchInput& BatchInput);

	//========================================
	// Configuration
	//========================================

	/**
	 * Set time step for predictions (default 0.5s)
	 */
	UFUNCTION(BlueprintCallable, Category = "TeamWorldModel")
	void SetTimeStep(float InTimeStep) { TimeStepSeconds = InTimeStep; }

	/**
	 * Get last inference time in milliseconds
	 */
	UFUNCTION(BlueprintPure, Category = "TeamWorldModel")
	float GetLastInferenceTime() const { return LastInferenceTimeMs; }

	/**
	 * Enable/disable enemy uncertainty modeling
	 */
	UFUNCTION(BlueprintCallable, Category = "TeamWorldModel")
	void SetUseEnemyUncertainty(bool bEnabled) { bUseEnemyUncertainty = bEnabled; }

	//========================================
	// Configuration Properties
	//========================================

	/** Time step for world model predictions (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamWorldModel|Config")
	float TimeStepSeconds = 0.5f;

	/** Apply confidence decay to enemy positions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamWorldModel|Config")
	bool bUseEnemyUncertainty = true;

	/** Enemy confidence decay rate per timestep [0.0-1.0] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TeamWorldModel|Config")
	float EnemyConfidenceDecayRate = 0.2f;

protected:
	//========================================
	// Decompose & Convert (Team → Agent)
	//========================================

	/**
	 * Decompose tactical play into 5 agent strategies
	 *
	 * Example: ETacticalPlay::Phalanx → [Defend, Defend, Support, Support, Support]
	 *
	 * @param Play - Tactical play enum
	 * @return Array of 5 EStrategyType assignments
	 */
	TArray<EStrategyType> DecomposeTacticalPlay(ETacticalPlay Play) const;

	/**
	 * Convert team state + strategies to agent observations
	 *
	 * Extracts agent-centric 56-dim observations from 60-dim team state:
	 * - Self state (6-dim): Position, Health, Cooldown, Alive
	 * - Allies (20-dim): Relative positions + health of 4 teammates
	 * - Enemies (20-dim): Relative positions + confidences of 5 enemies
	 * - Map state (10-dim): Capture points + pickups
	 *
	 * @param TeamState - Global team state
	 * @param Strategies - Assigned strategies per agent
	 * @return Array of 5 FObservation (56-dim each)
	 */
	TArray<FObservation> ConvertTeamStateToAgentObservations(
		const FTeamState& TeamState,
		const TArray<EStrategyType>& Strategies
	) const;

	/**
	 * Extract single agent's observation from team state
	 *
	 * @param TeamState - Global team state
	 * @param AgentIndex - Which agent (0-4)
	 * @return 56-dim observation for that agent
	 */
	FObservation ExtractAgentObservation(
		const FTeamState& TeamState,
		int32 AgentIndex
	) const;

	/**
	 * Generate tactical option (target position) for agent based on strategy
	 *
	 * Strategy-based heuristics:
	 * - Assault: Target nearest visible enemy or map center
	 * - Defend: Target friendly-controlled objective or hold position
	 * - Support: Target weakest ally for healing
	 *
	 * @param AgentIndex - Agent to generate option for (0-4)
	 * @param Strategy - Assigned strategy
	 * @param TeamState - Current state for context
	 * @return FTacticalOption with target position + duration
	 */
	FTacticalOption GenerateTacticalOption(
		int32 AgentIndex,
		EStrategyType Strategy,
		const FTeamState& TeamState
	) const;

	//========================================
	// Aggregate (Agent → Team)
	//========================================

	/**
	 * Aggregate 5 agent predictions into single team state
	 *
	 * - Friendly positions/health: Extract from prediction features
	 * - Enemy data: Copy from original state + apply confidence decay
	 * - Map state: Copy from original state (map doesn't change in 0.5s)
	 *
	 * @param Predictions - 5 × FObservation from agent world model
	 * @param OriginalState - Original team state (for map/enemy data)
	 * @return Aggregated next FTeamState
	 */
	FTeamState AggregateToTeamState(
		const TArray<FObservation>& Predictions,
		const FTeamState& OriginalState
	) const;

	/**
	 * Aggregate individual rewards into team reward
	 *
	 * - WinProb: Average across agents
	 * - TeamHealthDelta: Sum of individual deltas
	 * - ObjectiveScore: Strategy-weighted average (Defend 1.5×, Assault 1.2×, Support 0.8×)
	 * - DiversityBonus: 1.0 if ≥3 unique, 0.5 if 2, 0.0 if 1
	 *
	 * @param IndividualRewards - 5 × FCompositeReward from agent predictions
	 * @param Strategies - Current strategy assignments (for diversity calculation)
	 * @return Aggregated FTeamReward
	 */
	FTeamReward AggregateRewards(
		const TArray<FCompositeReward>& IndividualRewards,
		const TArray<EStrategyType>& Strategies
	) const;

	/**
	 * Calculate diversity bonus from strategy distribution
	 *
	 * @param Strategies - Array of 5 strategies
	 * @return 1.0 if ≥3 unique, 0.5 if 2, 0.0 if 1
	 */
	float CalculateDiversityBonus(const TArray<EStrategyType>& Strategies) const;

	//========================================
	// Dependencies
	//========================================

	/** Agent-level world model (wrapped by this team model) */
	UPROPERTY()
	UMocAgentWorldModel* AgentWorldModel;

	//========================================
	// Runtime State
	//========================================

	/** Last inference time in milliseconds (for profiling) */
	float LastInferenceTimeMs;

	/** Thread safety for future async support */
	mutable FCriticalSection PredictionMutex;
};
