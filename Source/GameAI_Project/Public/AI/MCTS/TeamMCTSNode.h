// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Team/TeamTypes.h"
#include "RL/RLTypes.h"

class FTeamMCTSNode;

/**
 * v8.0: Team-level MCTS Node for strategic assignment search
 *
 * Represents a state in the team command space where each follower
 * has been assigned a strategy (Assault/Defend/Support/Retreat) + target objective.
 * The MCTS algorithm searches this space to find optimal strategy assignments.
 */
class GAMEAI_PROJECT_API FTeamMCTSNode : public TSharedFromThis<FTeamMCTSNode>
{
public:
	FTeamMCTSNode();

	/** Initialize node with parent and strategy assignment (v8.0) */
	void Initialize(TSharedPtr<FTeamMCTSNode> InParent, const TMap<AActor*, FStrategyAssignment>& InAssignments);

	/** Check if this node is fully expanded (all actions tried) */
	bool IsFullyExpanded() const;

	/** Check if this is a terminal node (simulation depth limit reached) */
	bool IsTerminal() const;

	/** Select best child using UCT formula */
	TSharedPtr<FTeamMCTSNode> SelectBestChild(float ExplorationParam) const;

	/** Expand this node by adding a new child with untried action */
	TSharedPtr<FTeamMCTSNode> Expand(const TArray<AActor*>& Followers);

	/** Backpropagate reward from simulation */
	void Backpropagate(float Reward);

	/** Get the strategy assignment for this node (v8.0) */
	TMap<TObjectPtr<AActor>, FStrategyAssignment> GetStrategyAssignments() const { return StrategyAssignments; }

	/** Calculate UCT value for this node */
	float CalculateUCTValue(float ExplorationParam) const;

	/** Calculate UCT value with prior probability guidance (v3.0 Sprint 4) */
	float CalculateUCTValueWithPrior(float ExplorationParam, float Prior) const;

	/** Thread-safe backpropagation for parallel MCTS */
	void BackpropagateThreadSafe(float Reward);

	/** Thread-safe visit/reward update */
	void UpdateStatsThreadSafe(float Reward);

public:
	/** Parent node (nullptr for root) */
	TWeakPtr<FTeamMCTSNode> Parent;

	/** Child nodes (expanded actions) */
	TArray<TSharedPtr<FTeamMCTSNode>> Children;

	/** Critical section for thread-safe updates (parallel MCTS) */
	mutable FCriticalSection NodeMutex;

	/** Strategy assignment for this node (follower -> strategy assignment) (v8.0) */
	TMap<TObjectPtr<AActor>, FStrategyAssignment> StrategyAssignments;

	/** Total reward accumulated from simulations */
	float TotalReward;

	/** Number of times this node has been visited */
	int32 VisitCount;

	/** Depth of this node in the tree (0 = root) */
	int32 Depth;

	/** List of untried strategy assignment combinations (v8.0) */
	TArray<TMap<AActor*, FStrategyAssignment>> UntriedActions;

	/** Action priors from RL policy (v3.0 Sprint 4)
	 * Parallel array to UntriedActions - guides MCTS exploration
	 * Higher prior = more promising action, should be expanded first
	 */
	TArray<float> ActionPriors;
};
