#include "AI/MCTS/ConfidenceUCB.h"
#include "AI/MCTS/TeamTreeNode.h"

float ConfidenceUCB::CalculateScore(const FTeamTreeNode* Node, float ParentVisits)
{
    if (!Node) return -FLT_MAX;

    // 1. Exploitation: Q(s,a) = Average value
    float Q = 0.0f;
    if (Node->VisitCount > 0)
    {
        Q = Node->TotalValue / (static_cast<float>(Node->VisitCount) + EPSILON);
    }

    // 2. Exploration: U(s,a) = C * sqrt(ln(N_parent) / N_child)
    float U = C_PUCT * FMath::Sqrt(
        FMath::Loge(ParentVisits + 1.0f) /
        (static_cast<float>(Node->VisitCount) + EPSILON)
    );

    // 3. Uncertainty Penalty: Penalize low-confidence predictions
    float RiskPenalty = (1.0f - Node->PredictionConfidence) * K_RISK;

    return Q + U - RiskPenalty;
}