// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Observation/ObservationElement.h"
#include "AI/MCTS/TeamMCTSNode.h"
#include "AI/MCTS/MCTSAsyncTask.h"
#include "Observation/TeamObservation.h"
#include "Team/Objective.h"
#include "MCTS.generated.h"

/**
 * Monte Carlo Tree Search (MCTS) for team-level objective assignment (v6.0)
 *
 * v6.0 Architecture Change:
 * - MCTS now solves OBJECTIVE ASSIGNMENT (which agents → which objectives)
 * - RL handles STRATEGY SELECTION (how to execute assigned objective)
 * - Rules handle EXECUTION (deterministic position/target/fire logic)
 *
 * Key Differences from v5.0:
 * - v5.0: MCTS assigned strategies (Assault/Defend/Support/Retreat) to agents
 * - v6.0: MCTS assigns objectives (Capture A, Defend B, Support Agent3) to agents
 * - RL learns strategy adaptation based on objective + observation
 * - Smaller MCTS action space: 4 agents × 3 objectives vs. 4^4 strategy combinations
 *
 * Evaluation Function (v6.0):
 * - PRIMARY: RL value estimates (learned heuristic for each agent-objective pair)
 * - SECONDARY: Coordination heuristics (team cohesion, objective coverage, capability match)
 * - Replaces v5.0 hand-coded strategic heuristics with learned value function
 *
 * Performance:
 * - 30-50ms per search (500 simulations)
 * - ~2-4ms per RL value query (batched for multiple agents)
 * - Async execution (doesn't block RL strategy selection)
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
    // v6.0 API: OBJECTIVE ASSIGNMENT
    //--------------------------------------------------------------------------

    /**
     * Run MCTS to find best agent-to-objective assignment (v6.0)
     * @param Agents - Available agents
     * @param Objectives - Available objectives
     * @param Simulations - Number of MCTS simulations (default: 500)
     * @return Best assignment found (with expected value and visit count)
     */
    UFUNCTION(BlueprintCallable, Category = "MCTS|v6")
    FObjectiveAssignment RunObjectiveAssignment(
        const TArray<AActor*>& Agents,
        const TArray<UObjective*>& Objectives,
        int32 Simulations = 500
    );

private:
    //--------------------------------------------------------------------------
    // v6.0: MCTS CORE PHASES
    //--------------------------------------------------------------------------

    /**
     * MCTS Selection Phase: Traverse tree using UCT until leaf node (v6.0)
     */
    TSharedPtr<FTeamMCTSNode> Selection(TSharedPtr<FTeamMCTSNode> Root);

    /**
     * MCTS Expansion Phase: Add new child node (v6.0)
     */
    TSharedPtr<FTeamMCTSNode> Expansion(TSharedPtr<FTeamMCTSNode> Node);

    /**
     * MCTS Simulation Phase: Evaluate assignment (v6.0)
     */
    float Simulation(TSharedPtr<FTeamMCTSNode> Node);

    /**
     * MCTS Backpropagation Phase: Update ancestors (v6.0)
     */
    void Backpropagation(TSharedPtr<FTeamMCTSNode> Node, float Value);

    //--------------------------------------------------------------------------
    // v6.0: ASSIGNMENT EVALUATION (RL-GUIDED)
    //--------------------------------------------------------------------------

    /**
     * Evaluate assignment using RL value estimates + heuristics (v6.0)
     * @param Assignment - Agent-to-objective mapping
     * @return Value estimate [-1, 1]
     */
    float EvaluateAssignment(const FObjectiveAssignment& Assignment);

    /**
     * Generate possible assignments from current node (v6.0)
     */
    TArray<FObjectiveAssignment> GeneratePossibleAssignments(const FObjectiveAssignment& CurrentAssignment);

    //--------------------------------------------------------------------------
    // v6.0: COORDINATION HEURISTICS
    //--------------------------------------------------------------------------

    /**
     * Team cohesion score: Agents on same objective should be near each other (v6.0)
     * @return Score [0, 1] - Higher = better cohesion
     */
    float TeamCohesionScore(const FObjectiveAssignment& Assignment) const;

    /**
     * Objective coverage score: All high-priority objectives have agents (v6.0)
     * @return Score [0, 1] - Higher = better coverage
     */
    float ObjectiveCoverageScore(const FObjectiveAssignment& Assignment) const;

    /**
     * Capability match score: Right agents for the job (v6.0)
     * @return Score [0, 1] - Higher = better match
     */
    float CapabilityMatchScore(const FObjectiveAssignment& Assignment) const;

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

    /** RL Policy Network for heuristic action priors (v5.0 - DEPRECATED)
     * v6.0: Now used for GetStateValue() queries (actual RL value estimates)
     */
    UPROPERTY()
    TObjectPtr<class URLPolicyNetwork> RLPolicyNetwork;

    //--------------------------------------------------------------------------
    // v6.0: ASSIGNMENT STATE
    //--------------------------------------------------------------------------

    /** Current agents available for assignment (v6.0) */
    TArray<AActor*> AvailableAgents;

    /** Current objectives available for assignment (v6.0) */
    TArray<UObjective*> AvailableObjectives;
};
