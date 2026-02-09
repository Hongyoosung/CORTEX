#pragma once

#include "CoreMinimal.h"
#include "Types/MocTypes.h"
#include "Team/TeamState.h"
#include "Templates/SharedPointer.h"

class FTeamTreeNode;

/**
 * FTeamTreeNode - MCTS Tree Node for Team-Level Planning (v10.2)
 *
 * Represents a team state in the centralized MCTS tree.
 *
 * Key Differences from FTreeNode (v10.1):
 * - State: FObservation (56-dim agent) → FTeamState (60-dim global)
 * - Action: FTacticalOption (target position) → ETacticalPlay (team composition)
 * - Scope: Individual agent planning → Squad-level tactical decisions
 *
 * MCTS Statistics (unchanged):
 * - TotalValue (W): Cumulative value from backpropagation
 * - VisitCount (N): Number of times node visited
 * - VirtualLoss: Temporary visit penalty for batch processing
 * - PredictionConfidence: MocTeamWorldModel confidence score [0-1]
 *
 * UCB Logic (unchanged):
 * - Confidence-aware UCB1 for child selection
 * - Virtual loss for parallel batch expansion
 * - Thread-safe operations via mutex
 */
class GAMEAI_PROJECT_API FTeamTreeNode : public TSharedFromThis<FTeamTreeNode>
{
public:
	FTeamTreeNode();

	/**
	 * Initialize node with state and action
	 *
	 * @param InParent - Parent node (nullptr for root)
	 * @param InAction - Tactical play that led to this state
	 * @param InState - Predicted or current team state (60-dim)
	 * @param InConfidence - TeamWorldModel prediction confidence [0-1]
	 */
	void Initialize(
		TWeakPtr<FTeamTreeNode> InParent,
		ETacticalPlay InAction,
		const FTeamState& InState,
		float InConfidence
	);

	/**
	 * Check if this is a leaf node (no children yet)
	 * Leaf nodes are candidates for expansion
	 */
	bool IsLeaf() const;

	/**
	 * Check if this is the root node
	 */
	bool IsRoot() const;

	/**
	 * Select best child using Confidence-Aware UCB1
	 *
	 * UCB1 Formula: Q(s,a) + C * sqrt(ln(N_parent) / N_child) * Confidence
	 * - Q(s,a): Average value (TotalValue / VisitCount)
	 * - C: Exploration constant (typically ~1.414)
	 * - Confidence: Penalizes uncertain predictions
	 *
	 * @return Child with highest UCB score (nullptr if no children)
	 */
	TSharedPtr<FTeamTreeNode> SelectBestChild() const;

	/**
	 * Expand node with predicted next states
	 *
	 * Creates child nodes from TeamWorldModel batch predictions.
	 * Each child represents (State, TacticalPlay, NextState, Reward, Confidence).
	 *
	 * @param NextStates - Array of (TacticalPlay, NextState, Confidence) tuples
	 */
	void Expand(const TArray<TTuple<ETacticalPlay, FTeamState, float>>& NextStates);

	/**
	 * Backpropagate value up the tree
	 *
	 * Updates this node and all ancestors:
	 * - VisitCount += 1
	 * - TotalValue += Value
	 *
	 * Recursively propagates to parent until root is reached.
	 *
	 * @param Value - Scalarized reward from TeamReward
	 */
	void Backpropagate(float Value);

	/**
	 * Apply virtual loss (for batch MCTS)
	 *
	 * Temporarily increases VisitCount to discourage other threads
	 * from selecting the same node during batch collection.
	 */
	void ApplyVirtualLoss();

	/**
	 * Remove virtual loss after batch processing completes
	 */
	void RemoveVirtualLoss();

	/**
	 * Get effective visit count (includes virtual loss)
	 * Used for UCB calculation during batch selection
	 */
	int32 GetVisitCount() const;

public:
	// ===== Tree Structure =====
	TWeakPtr<FTeamTreeNode> Parent;
	TArray<TSharedPtr<FTeamTreeNode>> Children;

	// ===== State & Action =====
	/** Tactical play that led to this state */
	ETacticalPlay ActionFromParent;

	/** Team state at this node (60-dim global state) */
	FTeamState State;

	// ===== MCTS Statistics =====
	/** Cumulative value (W) */
	float TotalValue;

	/** Visit count (N) */
	int32 VisitCount;

	/** Virtual loss for parallel batch processing */
	int32 VirtualLoss;

	/** TeamWorldModel prediction confidence [0-1] */
	float PredictionConfidence;

	/** Policy network prior probability (optional, default 1.0) */
	float PriorProbability;

	/** Thread safety for batch MCTS */
	mutable FCriticalSection NodeMutex;
};
