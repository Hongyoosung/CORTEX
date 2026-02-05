// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Observation/ObservationElement.h"
#include "AI/MCTS/TeamMCTSNode.h"
#include "AI/MCTS/MCTSAsyncTask.h"
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



 /**
  * v8.20: Objective targeting type for batch prototypes
  * Determines which objective (Friendly/Hostile) agents should target
  */
UENUM(BlueprintType)
enum class EObjectiveType : uint8
{
    Friendly    UMETA(DisplayName = "Friendly"), // 아군 기지 방어 (Index 0)
    Hostile     UMETA(DisplayName = "Hostile"),  // 적군 기지 공격 (Index 1)
    Neutral     UMETA(DisplayName = "Neutral"),  // 중립/기본 (Index 0)
    Mixed       UMETA(DisplayName = "Mixed")     // 반반 나누기 (앞쪽 절반은 Friendly, 뒤쪽 절반은 Hostile)
};


 /**
  * v8.20: Team composition batch definition
  * Represents a fixed assignment of strategies to all 4 agents
  */
USTRUCT(BlueprintType)
struct FBatchPrototype
{
    GENERATED_BODY()

    FString Name;                                    // "TightAssault", "WideDefense", etc.
    TArray<EStrategyType> Strategies;               // [Assault, Assault, Assault, Support]
    FString Description;                            // For logging/debugging (e.g., "3 Assault + 1 Support, aggressive push")
    float EstimatedValue = 0.5f;                    // RL prior estimate
};

/**
 * v8.20: Batch performance tracking
 * Accumulates win/loss statistics for UCB1 selection
 */
USTRUCT(BlueprintType)
struct FBatchPerformance
{
    GENERATED_BODY()

    FString BatchKey;                                   // "A0→Assault,A1→Assault,A2→Defend,A3→Support"
    int32 Wins = 0;                                     // Episodes won with this batch
    int32 Losses = 0;
    int32 Draws = 0;
    int32 Trials = 0;                                   // Total episodes tried (N for this node)
    float AverageValue = 0.5f;                          // Wins / Trials
    double LastUsedTime = 0.0;                          // FPlatformTime::Seconds()

    float GetWinRate() const
    {
        if (Trials == 0) return 0.5f;

        // 무승부를 0.5승으로 계산
        float Points = Wins + (Draws * 0.5f);
        return Points / (float)Trials;
    }

    // 수정된 부분: TotalSystemTrials 인자 이름 수정 및 적용
    float GetUCBValue(float ExplorationParam, int32 TotalSystemTrials) const
    {
        if (Trials == 0) return FLT_MAX;  // Untried batches have infinite UCB

        float Exploitation = GetWinRate();

        // UCB1 Formula: Vi + C * sqrt(ln(N) / ni)
        // TotalSystemTrials: 전체 시도 횟수 (N)
        // Trials: 이 배치의 시도 횟수 (ni)
        // (+1은 log(0) 방지 및 스무딩용)
        float Exploration = ExplorationParam * FMath::Sqrt(FMath::Loge((float)(TotalSystemTrials + 1)) / (float)(Trials + 1));

        return Exploitation + Exploration;
    }
};


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


    // v8.20: New methods for batch-level strategy assignment

    /**
     * Generate all possible complete batch assignments (4 agents assigned)
     * @param Agents Available agents
     * @param Objectives Available objectives
     * @return Array of complete batches (size = 8)
     */
    TArray<TMap<AActor*, FStrategyAssignment>> GenerateCompleteBatches(
        const TArray<AActor*>& Agents,
        const TArray<AObjectiveActor*>& Objectives);

    /**
     * Select best batch using UCB1 with cached win rates
     * @param AllBatches All available batches (size = 8)
     * @return Best batch according to UCB1 formula
     */
    TMap<AActor*, FStrategyAssignment> SelectBatchByUCB1(
        const TArray<TMap<AActor*, FStrategyAssignment>>& AllBatches);

    /**
     * Update batch performance cache after episode completes
     * @param BatchAssignments The batch that was used
     * @param bEpisodeWon Whether the team won the episode
     */
    void UpdateBatchCache(
        const TMap<AActor*, FStrategyAssignment>& BatchAssignments,
        ETeamEpisodeResult Result);

    /**
     * Convert batch assignment map to unique string key for caching
     * @param BatchAssignments Map of agents to their assignments
     * @return String key like "A0→Assault→Obj1,A1→Assault→Obj1,..."
     */
    FString GetBatchKey(const TMap<AActor*, FStrategyAssignment>& BatchAssignments) const;

    /**
     * Save batch cache to JSON file (for warm start across runs)
     * @param SavePath Full file path
     * @return True if save successful
     */
    bool SaveBatchCache(const FString& SavePath);

    /**
     * Load batch cache from JSON file (warm start)
     * @param LoadPath Full file path
     * @return True if load successful
     */
    bool LoadBatchCache(const FString& LoadPath);

    /**
     * Print batch performance summary to logs
     * Shows top 5 batches by win rate
     */
    void LogBatchPerformance();



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

    UPROPERTY()
    TObjectPtr<class URLPolicyNetwork> RLPolicyNetwork;


private:
    //--------------------------------------------------------------------------
    // TEAM-LEVEL STATE
    //--------------------------------------------------------------------------
    /** Root node of team MCTS tree */
    TSharedPtr<FTeamMCTSNode> TeamRootNode;

    // v8.20: Batch cache for persistent performance tracking
    UPROPERTY()
    TMap<FString, FBatchPerformance> BatchCache;

    // v8.20: Pre-defined batch prototypes (set in InitializeTeamMCTS)
    UPROPERTY()
    TArray<FBatchPrototype> BatchPrototypes;

    // v8.20: Statistics
    int32 TotalBatchTrials = 0;
    int32 TotalBatchWins = 0;


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
