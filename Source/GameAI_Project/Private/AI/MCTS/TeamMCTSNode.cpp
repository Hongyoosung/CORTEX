// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MCTS/TeamMCTSNode.h"
#include "Kismet/KismetMathLibrary.h"

FTeamMCTSNode::FTeamMCTSNode()
	: TotalReward(0.0f)
	, VisitCount(0)
	, Depth(0)
{
}

void FTeamMCTSNode::Initialize(TSharedPtr<FTeamMCTSNode> InParent, const TMap<AActor*, UObjective*>& InObjectives)
{
	Parent = InParent;
	Objectives = InObjectives;
	TotalReward = 0.0f;
	VisitCount = 0;

	TSharedPtr<FTeamMCTSNode> ParentPinned = InParent;
	Depth = ParentPinned.IsValid() ? ParentPinned->Depth + 1 : 0;

	Children.Empty();
	UntriedActions.Empty();
}

bool FTeamMCTSNode::IsFullyExpanded() const
{
	return UntriedActions.Num() == 0;
}

bool FTeamMCTSNode::IsTerminal() const
{
	// Terminal if reached maximum depth
	const int32 MaxDepth = 5;
	return Depth >= MaxDepth;
}

TSharedPtr<FTeamMCTSNode> FTeamMCTSNode::SelectBestChild(float ExplorationParam) const
{
	// Thread-safe: Copy Children array before iteration to prevent race condition
	// (Expand() can modify Children from another thread during parallel MCTS)
	TArray<TSharedPtr<FTeamMCTSNode>> ChildrenCopy;
	{
		FScopeLock Lock(&NodeMutex);
		if (Children.Num() == 0)
		{
			return nullptr;
		}
		ChildrenCopy = Children;
	}

	TSharedPtr<FTeamMCTSNode> BestChild = nullptr;
	float BestValue = -FLT_MAX;

	for (const TSharedPtr<FTeamMCTSNode>& Child : ChildrenCopy)
	{
		if (!Child.IsValid()) continue;

		float UCTValue = Child->CalculateUCTValue(ExplorationParam);

		if (UCTValue > BestValue)
		{
			BestValue = UCTValue;
			BestChild = Child;
		}
	}

	return BestChild;
}

float FTeamMCTSNode::CalculateUCTValue(float ExplorationParam) const
{
	if (VisitCount == 0) return FLT_MAX;

	TSharedPtr<FTeamMCTSNode> ParentPinned = Parent.Pin();
	if (!ParentPinned || ParentPinned->VisitCount == 0)
	{
		return TotalReward / VisitCount;
	}

	float Exploitation = TotalReward / VisitCount;
	float Exploration = ExplorationParam * FMath::Sqrt(FMath::Loge(static_cast<float>(ParentPinned->VisitCount)) / VisitCount);

	return Exploitation + Exploration;
}

float FTeamMCTSNode::CalculateUCTValueWithPrior(float ExplorationParam, float Prior) const
{
	// AlphaZero-style PUCT (Predictor + UCT)
	// U(s,a) = Q(s,a) + c * P(s,a) * sqrt(N(s)) / (1 + N(s,a))
	// where P(s,a) is the prior probability from policy network

	TSharedPtr<FTeamMCTSNode> ParentPinned = Parent.Pin();
	if (!ParentPinned)
	{
		// Root node or orphaned node
		return (VisitCount == 0) ? FLT_MAX : TotalReward / VisitCount;
	}

	// Q value (exploitation)
	float QValue = (VisitCount == 0) ? 0.0f : TotalReward / VisitCount;

	// U value (exploration with prior)
	float ParentVisits = static_cast<float>(ParentPinned->VisitCount);
	float PriorBonus = ExplorationParam * Prior * FMath::Sqrt(ParentVisits) / (1.0f + VisitCount);

	return QValue + PriorBonus;
}

TSharedPtr<FTeamMCTSNode> FTeamMCTSNode::Expand(const TArray<AActor*>& Followers)
{
	// Thread-safe expansion for parallel MCTS
	FScopeLock Lock(&NodeMutex);

	if (UntriedActions.Num() == 0) return nullptr;

	int32 SelectedIndex = 0;

	// v3.0 Sprint 4: Use priors to guide action selection
	if (ActionPriors.Num() == UntriedActions.Num() && ActionPriors.Num() > 0)
	{
		// Select action with highest prior (greedy selection for expansion)
		float MaxPrior = -FLT_MAX;
		for (int32 i = 0; i < ActionPriors.Num(); ++i)
		{
			if (ActionPriors[i] > MaxPrior)
			{
				MaxPrior = ActionPriors[i];
				SelectedIndex = i;
			}
		}

		UE_LOG(LogTemp, VeryVerbose, TEXT("MCTS: Expanding action with prior %.3f (index %d/%d)"),
			ActionPriors[SelectedIndex], SelectedIndex, ActionPriors.Num());
	}
	else
	{
		// Fallback to random selection if no priors available
		SelectedIndex = FMath::RandRange(0, UntriedActions.Num() - 1);
		UE_LOG(LogTemp, VeryVerbose, TEXT("MCTS: Expanding action randomly (no priors, index %d/%d)"),
			SelectedIndex, UntriedActions.Num());
	}

	TMap<AActor*, UObjective*> NewObjectives = UntriedActions[SelectedIndex];
	UntriedActions.RemoveAt(SelectedIndex);

	// Also remove the corresponding prior
	if (ActionPriors.Num() > SelectedIndex)
	{
		ActionPriors.RemoveAt(SelectedIndex);
	}

	TSharedPtr<FTeamMCTSNode> Child = MakeShared<FTeamMCTSNode>();
	Child->Initialize(AsShared(), NewObjectives);
	Children.Add(Child);

	return Child;
}

void FTeamMCTSNode::Backpropagate(float Reward)
{
	VisitCount++;
	TotalReward += Reward;

	TSharedPtr<FTeamMCTSNode> ParentPinned = Parent.Pin();
	if (ParentPinned.IsValid())
	{
		ParentPinned->Backpropagate(Reward);
	}
}

void FTeamMCTSNode::BackpropagateThreadSafe(float Reward)
{
	// Thread-safe update of node stats
	UpdateStatsThreadSafe(Reward);

	// Recursively backpropagate to parent
	TSharedPtr<FTeamMCTSNode> ParentPinned = Parent.Pin();
	if (ParentPinned.IsValid())
	{
		ParentPinned->BackpropagateThreadSafe(Reward);
	}
}

void FTeamMCTSNode::UpdateStatsThreadSafe(float Reward)
{
	FScopeLock Lock(&NodeMutex);
	VisitCount++;
	TotalReward += Reward;
}
