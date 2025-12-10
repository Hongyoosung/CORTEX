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
 * Monte Carlo Tree Search (MCTS) for team-level strategic decision making
 *
 * Implements MCTS to assign strategic objectives to follower agents
 * based on the current team observation. Uses UCT for node selection,
 * random simulations for rollout, and backpropagation of rewards.
 *
 * New Architecture (v3.0 Combat Refactoring):
 * - Focuses on assigning high-level objectives rather than low-level commands
 * - Integrates with ObjectiveManager to create/manage objectives
 * * - Uses Value Network for leaf evaluation instead of heuristics
 * * - Supports RL Policy Network for action priors during selection
 * *
 * * Sprint 2:
 * * - Integrates World Model for state prediction during simulations
 * *
 * * Sprint 3:
 * * - Exports MCTS statistics for curriculum learning
 * *
 * * Sprint 4:
 * * - Incorporates RL Policy Network to guide tree search with priors
 * *
 * * Sprint 5:
 * * - Enhanced objective scoring and synergy calculations for better assignments
 * *
 * * Performance Optimizations (v3.2):
 * * - Root parallelization for 2-4x speedup on multi-core CPUs
 * * - Thread-safe tree updates using FCriticalSection
 * * - FAsyncTask wrapper for robust background execution
 * */


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



    /**
     * Run team-level MCTS with objectives (v3.0 Combat Refactoring)
     * @param TeamObservation - Current team observation
     * @param Followers - List of follower actors
     * @param ObjectiveManager - Manager to create/assign objectives
     * @return Map of follower to objective assignment
     */
    TMap<AActor*, class UObjective*> RunTeamMCTSWithObjectives(
        const FTeamObservation& TeamObservation,
        const TArray<AActor*>& Followers,
        class UObjectiveManager* ObjectiveManager
    );


private:
    //--------------------------------------------------------------------------
    // TEAM-LEVEL METHODS
    //--------------------------------------------------------------------------


    /**
     * Run full MCTS tree search with objectives (v3.0 Combat Refactoring)
     * @param TeamObs - Current team observation
     * @param Followers - List of followers
     * @param ObjectiveManager - Manager to create objectives
     * @return Best objective assignment found
     */
    TMap<AActor*, class UObjective*> RunTeamMCTSTreeSearchWithObjectives(
        const FTeamObservation& TeamObs,
        const TArray<AActor*>& Followers,
        class UObjectiveManager* ObjectiveManager
    );

    /**
     * MCTS Selection Phase: Traverse tree using UCT until leaf node
     */
    TSharedPtr<FTeamMCTSNode> SelectNode(TSharedPtr<FTeamMCTSNode> Node);

    /**
     * MCTS Expansion Phase: Create child node with untried action
     */
    TSharedPtr<FTeamMCTSNode> ExpandNode(TSharedPtr<FTeamMCTSNode> Node, const TArray<AActor*>& Followers);

    /**
     * MCTS Simulation Phase: Rollout from node to estimate reward
     */
    float SimulateNode(TSharedPtr<FTeamMCTSNode> Node, const FTeamObservation& TeamObs);

    /**
     * Run single MCTS simulation (select → expand → simulate → backpropagate)
     * Used by both sequential and parallel implementations
     */
    void RunSingleSimulation(
        TSharedPtr<FTeamMCTSNode> Root,
        const TArray<AActor*>& Followers,
        const FTeamObservation& TeamObs
    );

    /**
     * Generate possible objective assignments for expansion (v3.0 Combat Refactoring)
     * Much smaller action space: 7 objective types × N agents ≈ 50 combinations (vs 14,641)
     * @param Followers - List of follower actors
     * @param TeamObs - Current team observation
     * @param ObjectiveManager - Manager to create objectives
     * @param MaxCombinations - Maximum number of combinations to generate
     */
    TArray<TMap<AActor*, class UObjective*>> GenerateObjectiveAssignments(
        const TArray<AActor*>& Followers,
        const FTeamObservation& TeamObs,
        class UObjectiveManager* ObjectiveManager,
        int32 MaxCombinations = 20
    ) const;

    /**
     * Calculate objective score for follower-objective pair (Sprint 5)
     * Scores based on context: health, distance, threat level, etc.
     * @param Follower - The follower actor
     * @param ObjType - The objective type to score
     * @param TeamObs - Current team observation for context
     * @return Score value (higher = better fit)
     */
    float CalculateObjectiveScore(AActor* Follower, EObjectiveType ObjType, const FTeamObservation& TeamObs) const;

    /**
     * Calculate synergy bonus between objectives (Sprint 5)
     * Rewards tactical diversity and coordinated actions
     * @param ObjType - New objective being considered
     * @param ExistingObjectives - Already assigned objectives
     * @param TeamObs - Current team observation for context
     * @return Synergy bonus (positive = good synergy, negative = conflict)
     */
    float CalculateObjectiveSynergy(EObjectiveType ObjType, const TMap<AActor*, EObjectiveType>& ExistingObjectives, const FTeamObservation& TeamObs) const;

    /**
     * Calculate base team-level reward (no command-specific bonuses)
     * Considers team health, formation, objectives, combat effectiveness
     */
    float CalculateTeamReward(const FTeamObservation& TeamObs) const;


    /**
     * Calculate team-level reward for objective assignments (v3.0 Combat Refactoring)
     * Evaluates strategic value of assigning followers to specific objectives
     */
    float CalculateTeamReward(const FTeamObservation& TeamObs, const TMap<AActor*, class UObjective*>& Objectives) const;



public:
    //--------------------------------------------------------------------------
    // MCTS STATISTICS EXPORT (Sprint 3 - Curriculum Learning)
    //--------------------------------------------------------------------------

    /**
     * Extract MCTS statistics from tree search for curriculum learning
     * Called after RunTeamMCTSTreeSearchWithObjectives to get uncertainty metrics
     * @param OutValueVariance - Standard deviation of child node values
     * @param OutPolicyEntropy - Entropy of visit count distribution (action uncertainty)
     * @param OutAverageValue - Mean value estimate from root node
     */
    void GetMCTSStatistics(float& OutValueVariance, float& OutPolicyEntropy, float& OutAverageValue) const;

    /**
     * Get visit count for root node (indicates search depth)
     */
    int32 GetRootVisitCount() const;


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

    /** Cached ObjectiveManager for objective-based MCTS (v3.0) */
    UPROPERTY()
    TObjectPtr<class UObjectiveManager> CachedObjectiveManager;

    /** RL Policy Network for action selection and value estimation
     * Uses PPO's actor (action priors) + critic (state value) for MCTS guidance
     * Trained in real-time via RLlib (no offline self-play needed)
     */
    UPROPERTY()
    TObjectPtr<class URLPolicyNetwork> RLPolicyNetwork;
};
