#include "ConfidenceUCB.h"
#include "AI/MCTS/TreeNode.h" // FTreeNode의 실제 정의가 있는 헤더

// FTreeNode의 최소 정의 (컴파일을 위해 구조를 가정함)
// 실제 프로젝트에서는 별도 헤더에 정의되어 있어야 합니다.
/*
struct FTreeNode {
    float TotalValue;           // W: 누적 가치
    int32 Visits;               // N: 방문 횟수
    float PredictionConfidence; // P_conf: 월드 모델의 예측 신뢰도 (0.0 ~ 1.0)
    // ... 기타 속성 (Prior Probability 등)
};
*/

float ConfidenceUCB::CalculateScore(const FTreeNode* Node, float ParentVisits)
{
    if (!Node) return -FLT_MAX;

    // 1. Exploitation (평균 가치) Q(s, a)
    // 방문하지 않은 노드는 0으로 처리하거나, FPU(First Play Urgency) 로직을 추가할 수 있음
    float Q = 0.0f;
    if (Node->Visits > 0)
    {
        Q = Node->TotalValue / (static_cast<float>(Node->Visits) + EPSILON);
    }

    // 2. Exploration (탐험) U(s, a)
    // 방문 횟수가 적은 노드에 가산점
    float U = C_PUCT * FMath::Sqrt(FMath::Loge(ParentVisits + 1.0f) / (static_cast<float>(Node->Visits) + EPSILON));

    // 3. Uncertainty Penalty (불확실성 페널티) - v10.1 핵심 혁신
    // 모델 신뢰도가 낮을수록(Confidence < 1.0), 점수를 깎아 선택을 기피하게 만듦.
    // Hallucination(환각) 방지용.
    float RiskPenalty = (1.0f - Node->PredictionConfidence) * K_RISK;

    // 최종 점수 반환
    return Q + U - RiskPenalty;
}