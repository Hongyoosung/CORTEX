// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Team/TeamTypes.h"

class FTeamMCTSNode;
class UObjective;

/**
 * Team-level MCTS Node for strategic command search
 *
 * Represents a state in the team command space where each follower
 * has been assigned a strategic command. The MCTS algorithm searches
 * this space to find optimal command combinations.
 */
class GAMEAI_PROJECT_API FTeamMCTSNode : public TSharedFromThis<FTeamMCTSNode>
{
public:
	FTeamMCTSNode();

	/** Initialize node with parent and objective assignment (v3.0) */
	void Initialize(TSharedPtr<FTeamMCTSNode> InParent, const TMap<AActor*, UObjective*>& InObjectives);

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

	/** Get the objective assignment for this node (v3.0) */
	TMap<AActor*, UObjective*> GetObjectives() const { return Objectives; }

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

	/** Objective assignment for this node (follower -> objective) (v3.0) */
	TMap<AActor*, UObjective*> Objectives;

	/** Total reward accumulated from simulations */
	float TotalReward;

	/** Number of times this node has been visited */
	int32 VisitCount;

	/** Depth of this node in the tree (0 = root) */
	int32 Depth;

	/** List of untried objective combinations (v3.0) */
	TArray<TMap<AActor*, UObjective*>> UntriedActions;

	/** Action priors from RL policy (v3.0 Sprint 4)
	 * Parallel array to UntriedActions - guides MCTS exploration
	 * Higher prior = more promising action, should be expanded first
	 */
	TArray<float> ActionPriors;
};
