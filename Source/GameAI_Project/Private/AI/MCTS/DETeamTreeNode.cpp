#include "AI/MCTS/DETeamTreeNode.h"
#include "AI/MCTS/DEConfidenceUCB.h" // UCB calculation logic

FDETeamTreeNode::FDETeamTreeNode()
	: ActionFromParent(ETacticalPlay::StandardComp)
	, TotalValue(0.0f)
	, VisitCount(0)
	, VirtualLoss(0)
	, PredictionConfidence(1.0f)
	, PriorProbability(1.0f)
{
}

void FDETeamTreeNode::Initialize(
	TWeakPtr<FDETeamTreeNode> InParent,
	ETacticalPlay InAction,
	const FDETeamWorldState& InState,
	float InConfidence
)
{
	Parent = InParent;
	ActionFromParent = InAction;
	State = InState;
	PredictionConfidence = InConfidence;

	// Initialize statistics
	TotalValue = 0.0f;
	VisitCount = 0;
	VirtualLoss = 0;
	PriorProbability = 1.0f; // Can be replaced with policy network prior

	Children.Empty();
}

bool FDETeamTreeNode::IsLeaf() const
{
	// Leaf node has no children
	return Children.Num() == 0;
}

bool FDETeamTreeNode::IsRoot() const
{
	return !Parent.IsValid();
}

TSharedPtr<FDETeamTreeNode> FDETeamTreeNode::SelectBestChild() const
{
	// Thread-safe: Prevent structure changes during traversal
	FScopeLock Lock(&NodeMutex);

	if (Children.Num() == 0)
	{
		return nullptr;
	}

	TSharedPtr<FDETeamTreeNode> BestChild = nullptr;
	float BestScore = -FLT_MAX;

	// Parent visit count for UCB calculation (includes virtual loss)
	float ParentVisits = static_cast<float>(GetVisitCount());

	for (const TSharedPtr<FDETeamTreeNode>& Child : Children)
	{
		if (!Child.IsValid()) continue;

		// Confidence-Aware UCB1 score calculation
		float Score = DEConfidenceUCB::CalculateScore(Child.Get(), ParentVisits);

		if (Score > BestScore)
		{
			BestScore = Score;
			BestChild = Child;
		}
	}

	return BestChild;
}

void FDETeamTreeNode::Expand(const TArray<TTuple<ETacticalPlay, FDETeamWorldState, float>>& NextStates)
{
	FScopeLock Lock(&NodeMutex);

	// Prevent duplicate expansion
	if (Children.Num() > 0)
	{
		return;
	}

	for (const auto& Tuple : NextStates)
	{
		ETacticalPlay Play = Tuple.Get<0>();
		const FDETeamWorldState& NextState = Tuple.Get<1>();
		float Confidence = Tuple.Get<2>();

		TSharedPtr<FDETeamTreeNode> NewChild = MakeShared<FDETeamTreeNode>();
		NewChild->Initialize(AsShared(), Play, NextState, Confidence);

		Children.Add(NewChild);
	}
}

void FDETeamTreeNode::Backpropagate(float Value)
{
	// 1. Update current node (thread-safe)
	{
		FScopeLock Lock(&NodeMutex);
		VisitCount++;
		TotalValue += Value;
	}

	// 2. Propagate to parent
	TSharedPtr<FDETeamTreeNode> ParentPinned = Parent.Pin();
	if (ParentPinned.IsValid())
	{
		// Recursive call
		// Apply discount factor here if needed: Value * gamma
		ParentPinned->Backpropagate(Value);
	}
}

void FDETeamTreeNode::ApplyVirtualLoss()
{
	FScopeLock Lock(&NodeMutex);
	// Virtual loss: Make this node appear visited to other threads
	// Prevents duplicate selection during batch collection
	VirtualLoss++;
}

void FDETeamTreeNode::RemoveVirtualLoss()
{
	FScopeLock Lock(&NodeMutex);
	if (VirtualLoss > 0)
	{
		VirtualLoss--;
	}
}

int32 FDETeamTreeNode::GetVisitCount() const
{
	// Effective visit count includes virtual loss
	return VisitCount + VirtualLoss;
}
