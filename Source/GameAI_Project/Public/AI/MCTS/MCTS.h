// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Observation/ObservationElement.h"
#include "AI/MCTS/TeamMCTSNode.h"
#include "AI/MCTS/MCTSAsyncTask.h"
#include "Observation/TeamObservation.h"
#include "RL/RLTypes.h"
#include "MCTS.generated.h"

// Forward declarations
class AObjectiveActor;

/**
 * v8.0: Monte Carlo Tree Search (MCTS) for team-level strategy assignment
 *
 * v8.0 Architecture:
 * - MCTS solves STRATEGY ASSIGNMENT (which agents → which strategies + objectives)
 * - RL handles TACTICAL PARAMETERS (continuous control: aggression, cover, spread, risk)
 * - EQS handles SPATIAL REASONING (position selection using RL-modulated weights)
 * - Rules handle COMBAT EXECUTION (auto-targeting + auto-firing)
 *
 * Key Changes from v7.0:
 * - v7.0: MCTS assigned missions (removed in v8.0 - redundant with strategies)
 * - v8.0: MCTS assigns strategies (Assault/Defend/Support/Retreat) + target objectives
 * - RL learns tactical parameter control (4 continuous outputs, not discrete strategy)
 * - Action space: 4 agents × 4 strategies × 2 objectives = simpler than v7.0
 *
 * Evaluation Function (v8.0):
 * - PRIMARY: RL value estimates for each agent-strategy-objective combination
 * - SECONDARY: Coordination heuristics (team composition, objective coverage)
 * - Leverages RL critic head for learned value function
 *
 * Performance:
 * - 30-50ms per search (500 simulations)
 * - ~2-4ms per RL value query (batched for multiple agents)
 * - Async execution (doesn't block RL tactical parameter updates)
 */


UCLASS()
class GAMEAI_PROJECT_API UMCTS : public UObject
{
	GENERATED_BODY()

public:
    UMCTS();

    //--------------------------------------------------------------------------
    // TEAM-LEVEL INTERFACE (New Architecture)
    //--------------------------------------------------------------------------
    /**
     * Initialize MCTS for team-level strategic decision making
     * @param InMaxSimulations - Number of MCTS simulations to run
     * @param InExplorationParam - UCT exploration parameter (default: 1.41)
     */
    void InitializeTeamMCTS(int32 InMaxSimulations = 500, float InExplorationParam = 1.41f);

    //--------------------------------------------------------------------------
    // v8.0 API: STRATEGY ASSIGNMENT
    //--------------------------------------------------------------------------

    /**
     * Run MCTS to find best agent-to-strategy assignment (v8.0)
     * @param Agents - Available agents
     * @param Objectives - Available objectives (physical bases: AObjectiveActor)
     * @param Simulations - Number of MCTS simulations (default: 500)
     * @param CachedObservations - Pre-cached observations (for thread safety in async execution)
     * @return Best strategy assignments (agent -> FStrategyAssignment map)
     */
    UFUNCTION(BlueprintCallable, Category = "MCTS|v8")
    TMap<AActor*, FStrategyAssignment> RunStrategyAssignment(
        const TArray<AActor*>& Agents,
        const TArray<AObjectiveActor*>& Objectives,
        int32 Simulations,
        const TMap<AActor*, FObservationElement>& InCachedObservations
    );

private:
    //--------------------------------------------------------------------------
    // v8.0: MCTS CORE PHASES
    //--------------------------------------------------------------------------

    /**
     * MCTS Selection Phase: Traverse tree using UCT until leaf node (v8.0)
     */
    TSharedPtr<FTeamMCTSNode> Selection(TSharedPtr<FTeamMCTSNode> Root);

    /**
     * MCTS Expansion Phase: Add new child node (v8.0)
     */
    TSharedPtr<FTeamMCTSNode> Expansion(TSharedPtr<FTeamMCTSNode> Node);

    /**
     * MCTS Simulation Phase: Evaluate strategy assignment (v8.0)
     */
    float Simulation(TSharedPtr<FTeamMCTSNode> Node);

    /**
     * MCTS Backpropagation Phase: Update ancestors (v8.0)
     */
    void Backpropagation(TSharedPtr<FTeamMCTSNode> Node, float Value);

    //--------------------------------------------------------------------------
    // v8.0: ASSIGNMENT EVALUATION (RL-GUIDED)
    //--------------------------------------------------------------------------

    /**
     * Evaluate strategy assignment using RL value estimates + heuristics (v8.0)
     * @param Assignments - Agent-to-strategy assignment map
     * @return Value estimate [-1, 1]
     */
    float EvaluateStrategyAssignment(const TMap<AActor*, FStrategyAssignment>& Assignments);

    /**
     * Generate possible strategy assignments from current node (v8.0)
     */
    TArray<TMap<AActor*, FStrategyAssignment>> GeneratePossibleStrategyAssignments(
        const TMap<AActor*, FStrategyAssignment>& CurrentAssignments
    );

    //--------------------------------------------------------------------------
    // v8.0: COORDINATION HEURISTICS
    //--------------------------------------------------------------------------

    /**
     * Team composition score: Balanced mix of strategies (v8.0)
     * @return Score [0, 1] - Higher = better composition (e.g., not all assault)
     */
    float TeamCompositionScore(const TMap<AActor*, FStrategyAssignment>& Assignments) const;

    /**
     * Objective coverage score: Both objectives have adequate coverage (v8.0)
     * @return Score [0, 1] - Higher = better coverage (both friendly/hostile covered)
     */
    float ObjectiveCoverageScore(const TMap<AActor*, FStrategyAssignment>& Assignments) const;

    /**
     * Strategy synergy score: Compatible strategies work together (v8.0)
     * @return Score [0, 1] - Higher = better synergy (e.g., Assault + Support together)
     */
    float StrategySynergyScore(const TMap<AActor*, FStrategyAssignment>& Assignments) const;

public:
    //--------------------------------------------------------------------------
    // CONFIGURATION (Team-Level)
    //--------------------------------------------------------------------------
    /** Maximum number of MCTS simulations to run */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCTS|Config")
    int32 MaxSimulations;

    /** Discount factor for future rewards (0-1) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCTS|Config")
    float DiscountFactor;

    /** UCT exploration parameter (sqrt(2) ≈ 1.41 recommended) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCTS|Config")
    float ExplorationParameter;

    /** Maximum command combinations to generate per expansion */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCTS|Config")
    int32 MaxCombinationsPerExpansion;

    /** Enable parallel MCTS simulations (2-4x speedup on multi-core CPUs) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCTS|Config")
    bool bEnableParallelSimulations = true;

    /** Batch size for parallel simulations (number of simulations per batch) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MCTS|Config")
    int32 ParallelBatchSize = 50;


private:
    //--------------------------------------------------------------------------
    // TEAM-LEVEL STATE
    //--------------------------------------------------------------------------
    /** Root node of team MCTS tree */
    TSharedPtr<FTeamMCTSNode> TeamRootNode;

    /** Cached team observation for simulation */
    UPROPERTY()
    FTeamObservation CachedTeamObservation;

public:
    /** RL Policy Network for heuristic action priors (v5.0 - DEPRECATED)
     * v6.0: Now used for GetStateValue() queries (actual RL value estimates)
     * Public for debug visualization access (v6.0 Phase 13)
     */
    UPROPERTY()
    TObjectPtr<class URLPolicyNetwork> RLPolicyNetwork;

private:

    //--------------------------------------------------------------------------
    // v8.0: ASSIGNMENT STATE
    //--------------------------------------------------------------------------

    /** Current agents available for assignment (v8.0) */
    TArray<AActor*> AvailableAgents;

    /** Current objectives available for assignment (v8.0) */
    TArray<AObjectiveActor*> AvailableObjectives;

    /** Pre-cached observations for thread-safe async execution (v8.0) */
    TMap<AActor*, FObservationElement> CachedObservations;
};
