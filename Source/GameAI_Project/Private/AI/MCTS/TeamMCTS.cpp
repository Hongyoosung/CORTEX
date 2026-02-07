#include "AI/MCTS/TeamMCTS.h"
#include "AI/MCTS/TeamTreeNode.h"
#include "AI/Models/TeamWorldModel.h"

UTeamMCTS::UTeamMCTS()
	: TeamWorldModel(nullptr)
	, LastIterationCount(0)
	, LastPlanningTimeMs(0.0f)
{
}

void UTeamMCTS::Setup(UTeamWorldModel* InTeamWorldModel, const FTeamMCTSConfig& InConfig)
{
	TeamWorldModel = InTeamWorldModel;
	Config = InConfig;

	UE_LOG(LogTemp, Log, TEXT("TeamMCTS initialized: TimeBudget=%.1fms, BatchSize=%d"),
		Config.TimeBudgetSeconds * 1000.0f, Config.BatchSize);
}

ETacticalPlay UTeamMCTS::FindBestTacticalPlay(const FTeamState& CurrentState)
{
	if (!TeamWorldModel)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamMCTS: No TeamWorldModel set, returning default"));
		return ETacticalPlay::StandardComp;
	}

	// Start timing
	double StartTime = FPlatformTime::Seconds();

	// Create root node
	TSharedPtr<FTeamTreeNode> Root = MakeShared<FTeamTreeNode>();
	Root->Initialize(nullptr, ETacticalPlay::StandardComp, CurrentState, 1.0f);

	// MCTS main loop (time-budgeted)
	int32 Iterations = 0;

	while ((FPlatformTime::Seconds() - StartTime) < Config.TimeBudgetSeconds
		&& Iterations < Config.MaxIterations)
	{
		// ===== 1. Selection Phase: Collect batch of leaf nodes =====
		PendingBatch.Empty();

		for (int32 i = 0; i < Config.BatchSize; ++i)
		{
			TSharedPtr<FTeamTreeNode> Leaf = SelectNode(Root);

			if (Leaf.IsValid() && Leaf->IsLeaf())
			{
				// Generate all tactical plays for this leaf
				TArray<ETacticalPlay> Plays = GenerateTacticalPlays(Leaf->State);

				for (ETacticalPlay Play : Plays)
				{
					// Apply virtual loss to prevent duplicate selection
					Leaf->ApplyVirtualLoss();
					PendingBatch.Add(TPair<TSharedPtr<FTeamTreeNode>, ETacticalPlay>(Leaf, Play));
				}

				// Note: We only process one leaf per batch to keep batch size manageable
				// Each leaf generates ~10 tactical plays, so 1 leaf = ~10 predictions
				break;
			}
		}

		// ===== 2. Expansion & Backpropagation Phase =====
		if (PendingBatch.Num() > 0)
		{
			ProcessBatch(PendingBatch);
		}

		Iterations++;
	}

	// Calculate planning time
	LastPlanningTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0f;
	LastIterationCount = Iterations;

	// ===== 3. Final Selection: Choose most visited child =====
	TSharedPtr<FTeamTreeNode> BestChild = nullptr;
	int32 MaxVisits = -1;

	for (const TSharedPtr<FTeamTreeNode>& Child : Root->Children)
	{
		if (Child.IsValid() && Child->VisitCount > MaxVisits)
		{
			MaxVisits = Child->VisitCount;
			BestChild = Child;
		}
	}

	// Return best tactical play
	if (BestChild.IsValid())
	{
		UE_LOG(LogTemp, Display, TEXT("TeamMCTS: Selected %s (Visits=%d, Value=%.2f, Time=%.2fms, Iterations=%d)"),
			*UEnum::GetValueAsString(BestChild->ActionFromParent),
			BestChild->VisitCount,
			BestChild->VisitCount > 0 ? BestChild->TotalValue / BestChild->VisitCount : 0.0f,
			LastPlanningTimeMs,
			Iterations);

		return BestChild->ActionFromParent;
	}

	// Fallback
	UE_LOG(LogTemp, Warning, TEXT("TeamMCTS: No valid child found, returning default"));
	return ETacticalPlay::StandardComp;
}

TSharedPtr<FTeamTreeNode> UTeamMCTS::SelectNode(TSharedPtr<FTeamTreeNode> Node)
{
	// Selection Phase: Traverse tree using UCB until leaf is reached
	while (!Node->IsLeaf())
	{
		TSharedPtr<FTeamTreeNode> BestChild = Node->SelectBestChild();

		if (!BestChild.IsValid())
		{
			// No valid child, return current node
			return Node;
		}

		Node = BestChild;
	}

	return Node;
}

TArray<ETacticalPlay> UTeamMCTS::GenerateTacticalPlays(const FTeamState& State)
{
	TArray<ETacticalPlay> Plays;

	// All 10 tactical plays (pruned action space)
	Plays.Add(ETacticalPlay::AllOutRush);
	Plays.Add(ETacticalPlay::AggressivePush);
	Plays.Add(ETacticalPlay::Phalanx);
	Plays.Add(ETacticalPlay::StandardComp);
	Plays.Add(ETacticalPlay::FortressDefense);
	Plays.Add(ETacticalPlay::TurtleFormation);
	Plays.Add(ETacticalPlay::BaitStrategy);
	Plays.Add(ETacticalPlay::PincerManeuver);
	Plays.Add(ETacticalPlay::HealerComp);
	Plays.Add(ETacticalPlay::ResourceDeny);

	// Optional: Filter based on state constraints
	// Example: Don't allow AllOutRush if average health < 30%
	float AvgHealth = State.GetAverageHealth();
	if (AvgHealth < 0.3f)
	{
		// Remove aggressive plays when low health
		Plays.Remove(ETacticalPlay::AllOutRush);
		Plays.Remove(ETacticalPlay::AggressivePush);
	}

	// Ensure at least one play is available
	if (Plays.Num() == 0)
	{
		Plays.Add(ETacticalPlay::StandardComp);
	}

	return Plays;
}

void UTeamMCTS::ProcessBatch(TArray<TPair<TSharedPtr<FTeamTreeNode>, ETacticalPlay>>& NodesToExpand)
{
	if (!TeamWorldModel || NodesToExpand.Num() == 0)
	{
		return;
	}

	// ===== 1. Build batch input =====
	FTeamBatchInput BatchInput;

	for (const auto& Pair : NodesToExpand)
	{
		BatchInput.CurrentStates.Add(Pair.Key->State);
		BatchInput.SelectedPlays.Add(Pair.Value);
	}

	// ===== 2. Batch prediction via TeamWorldModel (3-5ms) =====
	FTeamBatchOutput BatchOutput = TeamWorldModel->PredictBatch(BatchInput);

	if (!BatchOutput.IsValid() || BatchOutput.GetBatchSize() != NodesToExpand.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamMCTS: Invalid batch output, skipping expansion"));

		// Remove virtual loss even on failure
		for (const auto& Pair : NodesToExpand)
		{
			Pair.Key->RemoveVirtualLoss();
		}
		return;
	}

	// ===== 3. Expand nodes and backpropagate =====
	for (int32 i = 0; i < NodesToExpand.Num(); ++i)
	{
		TSharedPtr<FTeamTreeNode> ParentNode = NodesToExpand[i].Key;
		ETacticalPlay Play = NodesToExpand[i].Value;

		FTeamState NextState = BatchOutput.PredictedStates[i];
		FTeamReward Reward = BatchOutput.Rewards[i];
		float Confidence = BatchOutput.Confidences[i];

		// Skip low-confidence predictions
		if (Confidence < 0.3f)
		{
			UE_LOG(LogTemp, Verbose, TEXT("TeamMCTS: Skipping low-confidence prediction (%.2f)"), Confidence);
			ParentNode->RemoveVirtualLoss();
			continue;
		}

		// Create child node
		TSharedPtr<FTeamTreeNode> Child = MakeShared<FTeamTreeNode>();
		Child->Initialize(ParentNode, Play, NextState, Confidence);

		// Add to parent's children
		{
			FScopeLock Lock(&ParentNode->NodeMutex);
			ParentNode->Children.Add(Child);
		}

		// Backpropagate value
		float Value = ScalarizeReward(Reward);
		Child->Backpropagate(Value);

		// Remove virtual loss
		ParentNode->RemoveVirtualLoss();
	}
}

float UTeamMCTS::ScalarizeReward(const FTeamReward& Reward) const
{
	// Use pre-computed total from TeamWorldModel
	// This already includes: WinProb*2.0 + TeamHealthDelta*0.01 + ObjectiveScore*1.5 + Diversity*0.5
	return Reward.TotalReward;

	// Alternative: Custom MCTS weights (if exploration tuning needed)
	// return Reward.WinProb * 2.5f
	//      + Reward.TeamHealthDelta * 0.015f
	//      + Reward.ObjectiveScore * 2.0f
	//      + Reward.DiversityBonus * 0.3f;
}
