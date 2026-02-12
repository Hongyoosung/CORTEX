#pragma once

#include "CoreMinimal.h"
#include "Types/StrategyTypes.h"
#include "AI/Models/TeamWorldModelTypes.h"
#include "Types/RewardTypes.h"
#include "Team/TeamWorldState.h"
#include "AI/MCTS/TeamTreeNode.h"
#include "TeamMCTS.generated.h"

/**
 * FTeamMCTSConfig - Configuration for centralized team-level MCTS
 * Same time budget as agent MCTS (15ms) but operates on team action space
 */
struct FTeamMCTSConfig
{
	float TimeBudgetSeconds = 0.015f; // 15ms budget (same as v10.1 per-agent)
	int32 BatchSize = 8;              // Batch expansion size
	int32 MaxIterations = 50;         // Safety limit for iterations
};

/**
 * UTeamMCTS - MOC v10.2 Centralized Team-Level MCTS Planner
 *
 * Architecture Change (v10.1 → v10.2):
 * - OLD: 5 agents × MCTS (15ms each) = 75ms total
 * - NEW: 1 centralized MCTS (15ms) = 5× reduction
 *
 * Key Differences from UModelBasedMCTS:
 * - State Space: FObservation (56-dim) → FTeamState (60-dim)
 * - Action Space: FTacticalOption (3 per agent) → ETacticalPlay (10 total)
 * - Scope: Individual survival → Team win rate + coordination
 * - World Model: UMocAgentWorldModel → UTeamWorldModel (pure neural network)
 *
 * Usage:
 * ```cpp
 * UTeamMCTS* Planner = NewObject<UTeamMCTS>();
 * Planner->Setup(TeamWorldModel, Config);
 * ETacticalPlay BestPlay = Planner->FindBestTacticalPlay(CurrentState);
 * ```
 *
 * Performance:
 * - Time Budget: 15ms per call
 * - Action Space: 10 tactical plays (vs 243 combinations in decentralized)
 * - Batch Inference: 8 leaves simultaneously via TeamWorldModel
 */
UCLASS()
class GAMEAI_PROJECT_API UTeamMCTS : public UObject
{
	GENERATED_BODY()

public:
	UTeamMCTS();

	/**
	 * Initialize MCTS planner with team world model
	 * @param InTeamWorldModel - Team-level simulator for predictions (UTeamWorldModel)
	 * @param InConfig - MCTS configuration (time budget, batch size)
	 */
	void Setup(class UTeamWorldModel* InTeamWorldModel, const FTeamMCTSConfig& InConfig);

	/**
	 * Main entry point: Find best tactical play for current team state
	 *
	 * Runs centralized MCTS planning within 15ms budget:
	 * 1. Create root node from current state
	 * 2. Selection: Traverse tree using UCB to leaf nodes
	 * 3. Expansion: Generate tactical plays and batch predict via TeamWorldModel
	 * 4. Backpropagation: Update tree values
	 * 5. Select most visited child as final decision
	 *
	 * @param CurrentState - Current global team state (60-dim)
	 * @return Best tactical play selected by MCTS
	 *
	 * Performance: ~15ms (configurable via FTeamMCTSConfig)
	 */
	ETacticalPlay FindBestTacticalPlay(const FTeamWorldState& CurrentState);

	/**
	 * Get statistics from last planning cycle
	 * @return Number of MCTS iterations executed
	 */
	int32 GetLastIterationCount() const { return LastIterationCount; }

	/**
	 * Get planning time from last cycle
	 * @return Planning time in milliseconds
	 */
	float GetLastPlanningTime() const { return LastPlanningTimeMs; }

private:
	// ===== Core MCTS Phases =====

	/**
	 * Selection Phase: Traverse tree from root to leaf using UCB1
	 *
	 * Applies virtual loss during selection to prevent duplicate exploration
	 * in batch processing.
	 *
	 * @param Node - Starting node (typically root)
	 * @return Selected leaf node for expansion
	 */
	TSharedPtr<FTeamTreeNode> SelectNode(TSharedPtr<FTeamTreeNode> Node);

	/**
	 * Generate candidate tactical plays for expansion
	 *
	 * Returns all 10 tactical plays:
	 * - AllOutRush, AggressivePush, Phalanx, StandardComp, FortressDefense
	 * - TurtleFormation, BaitStrategy, PincerManeuver, HealerComp, ResourceDeny
	 *
	 * Optional: Filter based on state (e.g., no AllOutRush if low health)
	 *
	 * @param State - Current team state for context
	 * @return Array of tactical plays to consider
	 */
	TArray<ETacticalPlay> GenerateTacticalPlays(const FTeamWorldState& State);

	/**
	 * Batch Inference & Expansion Phase
	 *
	 * Process collected (Node, Play) pairs:
	 * 1. Build FTeamBatchInput from pending batch
	 * 2. Call TeamWorldModel->PredictBatch()
	 * 3. Create child nodes from predictions
	 * 4. Backpropagate values up the tree
	 * 5. Remove virtual loss
	 *
	 * @param NodesToExpand - Array of (ParentNode, TacticalPlay) pairs
	 */
	void ProcessBatch(TArray<TPair<TSharedPtr<FTeamTreeNode>, ETacticalPlay>>& NodesToExpand);

	/**
	 * Scalarize team reward into single value for MCTS
	 *
	 * Applies MCTS-specific weights to multi-objective reward components
	 * for backpropagation in the search tree
	 *
	 * @param Reward - Multi-objective team reward
	 * @return Scalar value for backpropagation
	 */
	float ScalarizeReward(const FCompositeReward& Reward) const;

private:
	/** Team world model for state prediction */
	UPROPERTY()
	class UTeamWorldModel* TeamWorldModel;

	/** MCTS configuration */
	FTeamMCTSConfig Config;

	/** Reusable batch buffer (reduces GC pressure) */
	TArray<TPair<TSharedPtr<FTeamTreeNode>, ETacticalPlay>> PendingBatch;

	/** Statistics from last planning cycle */
	int32 LastIterationCount;
	float LastPlanningTimeMs;
};
