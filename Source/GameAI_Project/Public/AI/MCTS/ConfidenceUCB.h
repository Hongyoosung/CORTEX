#pragma once

#include "CoreMinimal.h"

// MCTS 트리 노드 전방 선언 (순환 참조 방지)
struct FTreeNode;

/**
 * MOC v10.1: Confidence-Aware UCB1 Strategy
 * 표준 UCB 공식에 World Model의 불확실성(Confidence) 페널티를 추가한 버전입니다.
 */
class MOCAI_API ConfidenceUCB
{
public:
    // 하이퍼파라미터 (Design Doc v10.1 참조)
    static constexpr float C_PUCT = 1.41f;    // 탐험 상수
    static constexpr float K_RISK = 2.5f;     // 불확실성 페널티 가중치
    static constexpr float EPSILON = 1e-6f;   // 0으로 나누기 방지

    /**
     * 노드의 우선순위 점수를 계산합니다.
     * @param Node          평가할 자식 노드
     * @param ParentVisits  부모 노드의 총 방문 횟수 (N)
     * @return              UCB Score (높을수록 선택됨)
     */
    static float CalculateScore(const FTreeNode* Node, float ParentVisits);
};