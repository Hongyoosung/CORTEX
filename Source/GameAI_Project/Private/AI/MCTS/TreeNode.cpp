#include "AI/MCTS/TreeNode.h"
#include "AI/MCTS/ConfidenceUCB.h" // 앞서 작성한 UCB 계산 로직 포함

FTreeNode::FTreeNode()
    : TotalValue(0.0f)
    , VisitCount(0)
    , VirtualLoss(0)
    , PredictionConfidence(1.0f)
    , PriorProbability(1.0f)
{
}

void FTreeNode::Initialize(TSharedPtr<FTreeNode> InParent, const FTacticalOption& InAction, const FObservation& InState, float InConfidence)
{
    Parent = InParent;
    ActionFromParent = InAction;
    State = InState;
    PredictionConfidence = InConfidence;
    
    // 초기화
    TotalValue = 0.0f;
    VisitCount = 0;
    VirtualLoss = 0;
    PriorProbability = 1.0f; // 필요 시 Policy Network 출력값으로 대체 가능
    
    Children.Empty();
}

bool FTreeNode::IsLeaf() const
{
    // 자식이 없으면 리프 노드
    return Children.Num() == 0;
}

bool FTreeNode::IsRoot() const
{
    return !Parent.IsValid();
}

TSharedPtr<FTreeNode> FTreeNode::SelectBestChild() const
{
    // Thread-safe: 자식 순회 중 구조 변경 방지
    FScopeLock Lock(&NodeMutex);

    if (Children.Num() == 0)
    {
        return nullptr;
    }

    TSharedPtr<FTreeNode> BestChild = nullptr;
    float BestScore = -FLT_MAX;

    // 부모의 방문 횟수 (현재 노드의 방문 횟수)
    // Virtual Loss를 포함하여 계산해야 병렬 탐색 시 다른 경로를 탐색하도록 유도함
    float ParentVisits = static_cast<float>(GetVisitCount());

    for (const TSharedPtr<FTreeNode>& Child : Children)
    {
        if (!Child.IsValid()) continue;

        // Confidence-Aware UCB1 점수 계산 (외부 정적 함수 사용)
        float Score = ConfidenceUCB::CalculateScore(Child.Get(), ParentVisits);

        if (Score > BestScore)
        {
            BestScore = Score;
            BestChild = Child;
        }
    }

    return BestChild;
}

void FTreeNode::Expand(const TArray<TTuple<FTacticalOption, FObservation, float>>& NextStates)
{
    FScopeLock Lock(&NodeMutex);

    // 이미 확장된 경우 중복 확장 방지
    if (Children.Num() > 0)
    {
        return;
    }

    for (const auto& Tuple : NextStates)
    {
        const FTacticalOption& Option = Tuple.Get<0>();
        const FObservation& PredState = Tuple.Get<1>();
        float Confidence = Tuple.Get<2>();

        TSharedPtr<FTreeNode> NewChild = MakeShared<FTreeNode>();
        NewChild->Initialize(AsShared(), Option, PredState, Confidence);
        
        Children.Add(NewChild);
    }
}

void FTreeNode::Backpropagate(float Value)
{
    // 1. 현재 노드 업데이트 (Thread-safe)
    {
        FScopeLock Lock(&NodeMutex);
        VisitCount++;
        TotalValue += Value;
    }

    // 2. 부모로 전파
    TSharedPtr<FTreeNode> ParentPinned = Parent.Pin();
    if (ParentPinned.IsValid())
    {
        // 재귀 호출
        // 감쇠율(Discount Factor)이 필요하다면 여기에 적용: Value * gamma
        ParentPinned->Backpropagate(Value);
    }
}

void FTreeNode::ApplyVirtualLoss()
{
    FScopeLock Lock(&NodeMutex);
    // Virtual Loss: 다른 스레드가 이 노드를 '이미 방문한 것'처럼 취급하게 만듦
    // 일반적으로 +1 방문, -1 가치(혹은 큰 페널티)를 부여
    VirtualLoss++;
}

void FTreeNode::RemoveVirtualLoss()
{
    FScopeLock Lock(&NodeMutex);
    if (VirtualLoss > 0)
    {
        VirtualLoss--;
    }
}

int32 FTreeNode::GetVisitCount() const
{
    // 실제 방문 횟수 + 가상 방문(진행 중인 스레드 수)
    return VisitCount + VirtualLoss;
}