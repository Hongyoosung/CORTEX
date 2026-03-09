#pragma once

#include "CoreMinimal.h"

class FDETeamTreeNode;

/**
 * Confidence-Aware UCB1 Strategy
 *
 * Calculates node selection scores for MCTS tree traversal.
 * Formula: Q(s,a) + C * sqrt(ln(N_parent) / N_child) - K_risk * (1 - Confidence)
 *
 * Used by FDETeamTreeNode for centralized Squad Commander MCTS.
 */
class GAMEAI_PROJECT_API DEConfidenceUCB
{
public:
    static constexpr float C_PUCT = 1.41f;
    static constexpr float K_RISK = 2.5f;
    static constexpr float EPSILON = 1e-6f;

    /**
     * Calculate UCB score for MCTS node selection
     *
     * @param Node - Child node to evaluate
     * @param ParentVisits - Parent's total visit count (N)
     * @return UCB score (higher = better)
     */
    static float CalculateScore(const FDETeamTreeNode* Node, float ParentVisits);
};