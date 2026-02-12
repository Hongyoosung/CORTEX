#include "AI/MCTS/TeamTreeNode.h"
#include "AI/MCTS/ConfidenceUCB.h" // UCB calculation logic

FTeamTreeNode::FTeamTreeNode()
	: ActionFromParent(ETacticalPlay::StandardComp)
	, TotalValue(0.0f)
	, VisitCount(0)
	, VirtualLoss(0)
	, PredictionConfidence(1.0f)
	, PriorProbability(1.0f)
{
}

void FTeamTreeNode::Initialize(
	TWeakPtr<FTeamTreeNode> InParent,
	ETacticalPlay InAction,
	const FTeamWorldState& InState,
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

bool FTeamTreeNode::IsLeaf() const
{
	// Leaf node has no children
	return Children.Num() == 0;
}

bool FTeamTreeNode::IsRoot() const
{
	return !Parent.IsValid();
}

TSharedPtr<FTeamTreeNode> FTeamTreeNode::SelectBestChild() const
{
	// Thread-safe: Prevent structure changes during traversal
	FScopeLock Lock(&NodeMutex);

	if (Children.Num() == 0)
	{
		return nullptr;
	}

	TSharedPtr<FTeamTreeNode> BestChild = nullptr;
	float BestScore = -FLT_MAX;

	// Parent visit count for UCB calculation (includes virtual loss)
	float ParentVisits = static_cast<float>(GetVisitCount());

	for (const TSharedPtr<FTeamTreeNode>& Child : Children)
	{
		if (!Child.IsValid()) continue;

		// Confidence-Aware UCB1 score calculation
		float Score = ConfidenceUCB::CalculateScore(Child.Get(), ParentVisits);

		if (Score > BestScore)
		{
			BestScore = Score;
			BestChild = Child;
		}
	}

	return BestChild;
}

void FTeamTreeNode::Expand(const TArray<TTuple<ETacticalPlay, FTeamWorldState, float>>& NextStates)
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
		const FTeamWorldState& NextState = Tuple.Get<1>();
		float Confidence = Tuple.Get<2>();

		TSharedPtr<FTeamTreeNode> NewChild = MakeShared<FTeamTreeNode>();
		NewChild->Initialize(AsShared(), Play, NextState, Confidence);

		Children.Add(NewChild);
	}
}

void FTeamTreeNode::Backpropagate(float Value)
{
	// 1. Update current node (thread-safe)
	{
		FScopeLock Lock(&NodeMutex);
		VisitCount++;
		TotalValue += Value;
	}

	// 2. Propagate to parent
	TSharedPtr<FTeamTreeNode> ParentPinned = Parent.Pin();
	if (ParentPinned.IsValid())
	{
		// Recursive call
		// Apply discount factor here if needed: Value * gamma
		ParentPinned->Backpropagate(Value);
	}
}

void FTeamTreeNode::ApplyVirtualLoss()
{
	FScopeLock Lock(&NodeMutex);
	// Virtual loss: Make this node appear visited to other threads
	// Prevents duplicate selection during batch collection
	VirtualLoss++;
}

void FTeamTreeNode::RemoveVirtualLoss()
{
	FScopeLock Lock(&NodeMutex);
	if (VirtualLoss > 0)
	{
		VirtualLoss--;
	}
}

int32 FTeamTreeNode::GetVisitCount() const
{
	// Effective visit count includes virtual loss
	return VisitCount + VirtualLoss;
}
