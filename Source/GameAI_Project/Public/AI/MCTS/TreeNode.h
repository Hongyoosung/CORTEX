#pragma once

#include "CoreMinimal.h"
#include "AI/Options/TacticalOption.h"
#include "AI/Models/LearnedWorldModel.h" // FObservation, FCompositeReward 정의 포함
#include "Templates/SharedPointer.h"

class FTreeNode;

/**
 * CORTEX v10.1: Model-Based MCTS Node
 * * Represents a state in the decision tree.
 * - Stores the State (Observation) predicted by the World Model.
 * - Stores the Action (TacticalOption) that led to this state.
 * - Tracks Statistics for AlphaZero-style planning (Visits, Value, Confidence).
 */
class GAMEAI_PROJECT_API FTreeNode : public TSharedFromThis<FTreeNode>
{
public:
    FTreeNode();

    /**
     * 초기화
     * @param InParent 부모 노드 (Root인 경우 nullptr)
     * @param InAction 이 상태로 오기 위해 수행한 액션
     * @param InState 월드 모델이 예측한(혹은 현재) 상태
     * @param InConfidence 월드 모델의 예측 신뢰도 (Root는 1.0)
     */
    void Initialize(TSharedPtr<FTreeNode> InParent, const FTacticalOption& InAction, const FObservation& InState, float InConfidence);

    /** 리프 노드 여부 확인 (자식이 없으면 리프) */
    bool IsLeaf() const;

    /** 루트 노드 여부 확인 */
    bool IsRoot() const;

    /** * Confidence-Aware UCB1을 사용하여 가장 유망한 자식 노드 선택 
     * @param ParentVisits 부모 노드의 방문 횟수 (Log 계산용)
     */
    TSharedPtr<FTreeNode> SelectBestChild() const;

    /**
     * 노드 확장 (Expansion)
     * 월드 모델이 예측한 가능한 미래 상태들을 자식으로 추가합니다.
     * @param NextStates (Action, NextState, Confidence) 튜플 리스트
     */
    void Expand(const TArray<TTuple<FTacticalOption, FObservation, float>>& NextStates);

    /**
     * 역전파 (Backpropagation)
     * Value Network가 평가한 가치를 트리에 반영합니다.
     * @param Value 평가된 승률 또는 가치 (Scalarized Reward)
     */
    void Backpropagate(float Value);

    /**
     * Virtual Loss 적용 (Batch MCTS용)
     * 멀티스레드/배치 수집 단계에서 동일한 노드가 중복 선택되는 것을 방지하기 위해 가상의 방문 횟수를 더함
     */
    void ApplyVirtualLoss();
    void RemoveVirtualLoss();

    /** 방문 횟수 반환 (Virtual Loss 포함 여부 확인 가능) */
    int32 GetVisitCount() const;

public:
    // ===== Tree Structure =====
    TWeakPtr<FTreeNode> Parent;
    TArray<TSharedPtr<FTreeNode>> Children;

    // ===== State & Action =====
    /** 이 노드에 도달하기 위해 선택했던 옵션 */
    FTacticalOption ActionFromParent;

    /** 이 노드가 나타내는 게임 상태 (World Model의 입력/출력) */
    FObservation State;

    // ===== MCTS Statistics =====
    /** 총 누적 가치 (W) */
    float TotalValue;

    /** 방문 횟수 (N) */
    int32 VisitCount;

    /** Virtual Loss (병렬 탐색 중 임시 방문 횟수) */
    int32 VirtualLoss;

    /** * World Model의 예측 신뢰도 (0.0 ~ 1.0)
     * 낮을수록 '환각' 가능성이 높으므로 UCB에서 페널티를 받음
     */
    float PredictionConfidence;

    /** Policy Network의 사전 확률 (P) - v10.1에서는 옵션으로 사용 (기본 1.0) */
    float PriorProbability;

    /** 스레드 안전성 확보를 위한 뮤텍스 */
    mutable FCriticalSection NodeMutex;
};