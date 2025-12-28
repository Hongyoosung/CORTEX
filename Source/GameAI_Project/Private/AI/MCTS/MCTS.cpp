#include "AI/MCTS/MCTS.h"
#include "Kismet/KismetMathLibrary.h"
#include "Team/TeamTypes.h"
#include "Team/ObjectiveManager.h"
#include "Team/Objective.h"
#include "Team/FollowerAgentComponent.h"
#include "RL/RLPolicyNetwork.h"
#include "Combat/HealthComponent.h"
#include "Combat/WeaponComponent.h"

UMCTS::UMCTS()
    : MaxSimulations(500)
    , DiscountFactor(0.95f)
    , ExplorationParameter(1.41f)
    , MaxCombinationsPerExpansion(10)
    , bEnableParallelSimulations(true)
    , ParallelBatchSize(50)
    , TeamRootNode(nullptr)
{
}

//==============================================================================
// TEAM-LEVEL MCTS IMPLEMENTATION
//==============================================================================

void UMCTS::InitializeTeamMCTS(int32 InMaxSimulations, float InExplorationParam)
{
    MaxSimulations = InMaxSimulations;
    ExplorationParameter = InExplorationParam;

    // Initialize RL Policy Network for action priors (GetObjectivePriors - heuristic only)
    RLPolicyNetwork = NewObject<URLPolicyNetwork>(this);

    UE_LOG(LogTemp, Log, TEXT("MCTS: Initialized for team-level decisions (Simulations: %d, Exploration: %.2f)"),
        MaxSimulations, ExplorationParameter);
    UE_LOG(LogTemp, Log, TEXT("MCTS: Using strategic heuristic evaluation for leaf nodes"));
    UE_LOG(LogTemp, Log, TEXT("MCTS: Using heuristic action priors for tree guidance"));
}




float UMCTS::CalculateTeamReward(const FTeamObservation& TeamObs) const
{
    // Team health component (0-200 points)
    float HealthReward = TeamObs.AverageTeamHealth * 2.0f;

    // Formation coherence (0-50 points)
    float FormationReward = TeamObs.FormationCoherence * 50.0f;

    // ============================================================================
    // PROXIMITY DIAGNOSIS: Log FormationCoherence value
    // ============================================================================
    UE_LOG(LogTemp, Verbose, TEXT("[MCTS] FormationCoherence=%.3f (reward component: %.1f)"),
        TeamObs.FormationCoherence, FormationReward);

    // Objective progress (0-100 points)
    // Closer to objective = higher reward
    float ObjectiveReward = 0.0f;
    if (TeamObs.DistanceToObjective > 0.0f)
    {
        // Max reward at 0 distance, min at 10000cm (100m)
        ObjectiveReward = FMath::Max(0.0f, 100.0f - (TeamObs.DistanceToObjective / 100.0f));
    }

    // Combat effectiveness (variable, can be negative)
    float CombatReward = TeamObs.KillDeathRatio * 50.0f;

    // Threat penalty (-100 to 0 points)
    float ThreatPenalty = -TeamObs.ThreatLevel * 20.0f;

    // Outnumbered penalty (-100 points)
    float OutnumberedPenalty = TeamObs.bOutnumbered ? -100.0f : 0.0f;

    // Flanked penalty (-50 points)
    float FlankedPenalty = TeamObs.bFlanked ? -50.0f : 0.0f;

    // Cover advantage bonus (+50 points)
    float CoverBonus = TeamObs.bHasCoverAdvantage ? 50.0f : 0.0f;

    // High ground bonus (+30 points)
    float HighGroundBonus = TeamObs.bHasHighGround ? 30.0f : 0.0f;

    float TotalReward = HealthReward + FormationReward + ObjectiveReward + CombatReward
                      + ThreatPenalty + OutnumberedPenalty + FlankedPenalty
                      + CoverBonus + HighGroundBonus;

    UE_LOG(LogTemp, Verbose, TEXT("MCTS Team Reward: %.1f (Health=%.1f, Formation=%.1f, Objective=%.1f, Combat=%.1f, Threat=%.1f)"),
        TotalReward, HealthReward, FormationReward, ObjectiveReward, CombatReward, ThreatPenalty);

    return TotalReward;
}



TSharedPtr<FTeamMCTSNode> UMCTS::SelectNode(TSharedPtr<FTeamMCTSNode> Node)
{
    // Traverse tree using UCT until reaching a leaf node
    if (!Node.IsValid()) return nullptr;

    while (!Node->IsTerminal())
    {
        if (!Node->IsFullyExpanded())
        {
            return Node;
        }
        else
        {
            Node = Node->SelectBestChild(ExplorationParameter);
            if (!Node.IsValid())
            {
                break;
            }
        }
    }

    return Node;
}


TSharedPtr<FTeamMCTSNode> UMCTS::ExpandNode(TSharedPtr<FTeamMCTSNode> Node, const TArray<AActor*>& Followers)
{
    if (!Node.IsValid()) return nullptr;

    // v3.0: UntriedActions should be pre-populated by objective-based MCTS
    // If empty, there's nothing to expand
    if (Node->UntriedActions.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("MCTS::ExpandNode: No untried actions available for expansion"));
        return nullptr;
    }

    return Node->Expand(Followers);
}


float UMCTS::SimulateNode(TSharedPtr<FTeamMCTSNode> Node, const FTeamObservation& TeamObs)
{
    if (!Node.IsValid())
    {
        return 0.0f;
    }

    // Strategic heuristic evaluation: Analyze game state for team-level value
    // Evaluates objective progress, team strength, and positional advantage
    // Range: [-1.0, +1.0] = (±0.6 objective + ±0.3 strength + ±0.1 positioning)

    TMap<AActor*, UObjective*> NodeObjectives = Node->GetObjectives();
    TArray<AActor*> Followers;
    NodeObjectives.GetKeys(Followers);

    float Value = 0.0f;

    // 1. Objective Progress (±0.6) - PRIMARY strategic metric
    Value += EvaluateObjectiveProgress(Node, TeamObs);

    // 2. Team Strength (±0.3) - Force composition
    Value += EvaluateTeamStrength(Followers, TeamObs);

    // 3. Positional Advantage (±0.1) - Tactical positioning
    Value += EvaluatePositionalAdvantage(Followers, TeamObs);

    // Value already clamped by individual functions, double-check
    return FMath::Clamp(Value, -1.0f, 1.0f);
}

void UMCTS::RunSingleSimulation(
    TSharedPtr<FTeamMCTSNode> Root,
    const TArray<AActor*>& Followers,
    const FTeamObservation& TeamObs)
{
    if (!Root.IsValid())
    {
        return;
    }

    // Selection: Traverse tree to leaf node
    TSharedPtr<FTeamMCTSNode> LeafNode = SelectNode(Root);
    if (!LeafNode.IsValid())
    {
        return;
    }

    // Expansion: Expand leaf node if not terminal and visited
    TSharedPtr<FTeamMCTSNode> NodeToSimulate = LeafNode;
    if (!LeafNode->IsTerminal() && LeafNode->VisitCount > 0)
    {
        NodeToSimulate = ExpandNode(LeafNode, Followers);
        if (!NodeToSimulate.IsValid())
        {
            NodeToSimulate = LeafNode;
        }
    }

    // Simulation: Estimate value using PPO critic
    float Reward = SimulateNode(NodeToSimulate, TeamObs);

    // Backpropagation: Update node stats
    // Use thread-safe version for parallel MCTS
    if (bEnableParallelSimulations)
    {
        NodeToSimulate->BackpropagateThreadSafe(Reward);
    }
    else
    {
        NodeToSimulate->Backpropagate(Reward);
    }
}






//==============================================================================
// v3.0 COMBAT REFACTORING: OBJECTIVE-BASED MCTS
//==============================================================================

TArray<TMap<AActor*, UObjective*>> UMCTS::GenerateObjectiveAssignments(
    const TArray<AActor*>& Followers,
    const FTeamObservation& TeamObs,
    UObjectiveManager* ObjectiveManager,
    int32 MaxCombinations) const
{
    TArray<TMap<AActor*, UObjective*>> Assignments;

    if (Followers.Num() == 0 || !ObjectiveManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("GenerateObjectiveAssignments: Invalid input (Followers=%d, ObjMgr=%s)"),
            Followers.Num(), ObjectiveManager ? TEXT("Valid") : TEXT("Null"));
        return Assignments;
    }

    // Available objective types (4 strategic objectives - v4.0 simplified)
    TArray<EObjectiveType> PossibleObjectives = {
        EObjectiveType::Assault,    // Offensive: Push toward enemy/objective
        EObjectiveType::Defend,     // Defensive: Hold position/objective
        EObjectiveType::Support,    // Auxiliary: Provide cover/assistance
        EObjectiveType::Retreat     // Fallback: Disengage and reposition
    };

    UE_LOG(LogTemp, Display, TEXT("🎯 [UCB] Generating %d objective assignments for %d followers"),
        MaxCombinations, Followers.Num());

    // Sprint 5: UCB-based progressive widening
    // 1. Compute objective scores for each follower-objective pair
    // 2. Greedily select top-K objectives per follower
    // 3. Combine with synergy bonuses
    // 4. Add exploration with epsilon-greedy

    const int32 TopKPerFollower = 3; // Top-3 objectives per follower
    const float EpsilonExploration = 0.2f; // 20% random exploration

    // Build scored objectives for each follower
    TMap<AActor*, TArray<TPair<EObjectiveType, float>>> FollowerObjectiveScores;

    for (AActor* Follower : Followers)
    {
        if (!Follower) continue;

        TArray<TPair<EObjectiveType, float>> Scores;

        for (EObjectiveType ObjType : PossibleObjectives)
        {
            float Score = CalculateObjectiveScore(Follower, ObjType, TeamObs);
            Scores.Add(TPair<EObjectiveType, float>(ObjType, Score));
        }

        // Sort by score (descending)
        Scores.Sort([](const TPair<EObjectiveType, float>& A, const TPair<EObjectiveType, float>& B) {
            return A.Value > B.Value;
        });

        FollowerObjectiveScores.Add(Follower, Scores);
    }

    // Generate combinations using greedy + synergy approach
    for (int32 i = 0; i < MaxCombinations; ++i)
    {
        TMap<AActor*, UObjective*> Assignment;
        TMap<AActor*, EObjectiveType> SelectedTypes; // Track for synergy calculation

        // Epsilon-greedy: Sometimes explore randomly
        bool bExplore = (FMath::FRand() < EpsilonExploration);

        if (bExplore)
        {
            // Random assignment (exploration)
            for (AActor* Follower : Followers)
            {
                if (!Follower) continue;

                int32 RandomIndex = FMath::RandRange(0, PossibleObjectives.Num() - 1);
                EObjectiveType ObjType = PossibleObjectives[RandomIndex];
                SelectedTypes.Add(Follower, ObjType);
            }
        }
        else
        {
            // Greedy selection with synergy (exploitation)
            for (AActor* Follower : Followers)
            {
                if (!Follower) continue;

                const TArray<TPair<EObjectiveType, float>>* Scores = FollowerObjectiveScores.Find(Follower);
                if (!Scores || Scores->Num() == 0) continue;

                // Pick from top-K with probability weighted by score
                int32 K = FMath::Min(TopKPerFollower, Scores->Num());
                TArray<TPair<EObjectiveType, float>> TopK = *Scores;
                TopK.SetNum(K);

                // Apply synergy bonus based on already selected objectives
                for (auto& ScorePair : TopK)
                {
                    float SynergyBonus = CalculateObjectiveSynergy(ScorePair.Key, SelectedTypes, TeamObs);
                    ScorePair.Value += SynergyBonus;
                }

                // Softmax selection from top-K
                float TotalScore = 0.0f;
                for (const auto& ScorePair : TopK)
                {
                    TotalScore += FMath::Exp(ScorePair.Value);
                }

                float RandomValue = FMath::FRand() * TotalScore;
                float CumulativeScore = 0.0f;
                EObjectiveType SelectedType = TopK[0].Key;

                for (const auto& ScorePair : TopK)
                {
                    CumulativeScore += FMath::Exp(ScorePair.Value);
                    if (RandomValue <= CumulativeScore)
                    {
                        SelectedType = ScorePair.Key;
                        break;
                    }
                }

                SelectedTypes.Add(Follower, SelectedType);
            }
        }

        // Create actual objectives from selected types
        for (const auto& Pair : SelectedTypes)
        {
            AActor* Follower = Pair.Key;
            EObjectiveType ObjType = Pair.Value;

            AActor* TargetActor = nullptr;
            FVector TargetLocation = FVector::ZeroVector;
            int32 Priority = 5;

            // Select target based on objective type (v4.0 simplified)
            switch (ObjType)
            {
                case EObjectiveType::Assault:
                    // Offensive: Target enemies or objective location
                    if (TeamObs.TrackedEnemies.Num() > 0)
                    {
                        TArray<AActor*> EnemyArray = TeamObs.TrackedEnemies.Array();
                        TargetActor = EnemyArray[FMath::RandRange(0, EnemyArray.Num() - 1)];
                    }
                    TargetLocation = TeamObs.TeamCentroid; // Fallback: push forward
                    Priority = 8;
                    break;

                case EObjectiveType::Defend:
                    // Defensive: Hold current position/objective
                    TargetLocation = TeamObs.TeamCentroid;
                    Priority = 7;
                    break;

                case EObjectiveType::Support:
                    // Auxiliary: Support allies
                    if (Followers.Num() > 1)
                    {
                        TArray<AActor*> OtherFollowers = Followers;
                        OtherFollowers.Remove(Follower);
                        if (OtherFollowers.Num() > 0)
                        {
                            TargetActor = OtherFollowers[FMath::RandRange(0, OtherFollowers.Num() - 1)];
                        }
                    }
                    TargetLocation = TeamObs.TeamCentroid;
                    Priority = 6;
                    break;

                case EObjectiveType::Retreat:
                    // Fallback: Disengage from enemies
                    if (TeamObs.TrackedEnemies.Num() > 0)
                    {
                        TArray<AActor*> EnemyArray = TeamObs.TrackedEnemies.Array();
                        TargetActor = EnemyArray[FMath::RandRange(0, EnemyArray.Num() - 1)]; // Track enemies to retreat FROM
                    }
                    TargetLocation = TeamObs.TeamCentroid;
                    Priority = 6;
                    break;

                case EObjectiveType::None:
                    // No objective
                    break;
            }

            // Create objective
            UObjective* Objective = ObjectiveManager->CreateObjective(ObjType, TargetActor, TargetLocation, Priority);
            if (Objective)
            {
                Assignment.Add(Follower, Objective);
            }
        }

        if (Assignment.Num() > 0)
        {
            Assignments.Add(Assignment);
        }
    }

    UE_LOG(LogTemp, Display, TEXT("🎯 [UCB] Generated %d objective combinations (%.1f%% exploration)"),
        Assignments.Num(), EpsilonExploration * 100.0f);
    return Assignments;
}

float UMCTS::CalculateObjectiveScore(AActor* Follower, EObjectiveType ObjType, const FTeamObservation& TeamObs) const
{
    if (!Follower) return 0.0f;

    float Score = 0.0f;

    // v4.0: Simplified strategic scoring for 4 objective types
    switch (ObjType)
    {
        case EObjectiveType::Assault:
            // Offensive: Prefer when enemies present, team healthy, and coordinated
            Score = TeamObs.TrackedEnemies.Num() * 10.0f;
            Score += TeamObs.AverageTeamHealth * 0.5f;
            Score += TeamObs.FormationCoherence * 15.0f;
            Score -= TeamObs.ThreatLevel * 5.0f; // Avoid if under heavy fire
            Score += 50.0f - (TeamObs.DistanceToObjective / 100.0f); // Prefer when near objective
            break;

        case EObjectiveType::Defend:
            // Defensive: Prefer when on objective, threat is moderate, formation good
            Score = 30.0f - (TeamObs.DistanceToObjective / 50.0f);
            Score += TeamObs.ThreatLevel * 5.0f; // Higher threat = more defense needed
            Score += TeamObs.FormationCoherence * 15.0f;
            Score += TeamObs.AverageTeamHealth * 0.3f;
            break;

        case EObjectiveType::Support:
            // Auxiliary: Prefer when team health low, formation broken, or high threat
            Score = (100.0f - TeamObs.AverageTeamHealth) * 0.4f;
            Score += (1.0f - TeamObs.FormationCoherence) * 25.0f;
            Score += TeamObs.ThreatLevel * 4.0f;
            break;

        case EObjectiveType::Retreat:
            // Fallback: Prefer when health low, threat high, or K/D poor
            Score = (100.0f - TeamObs.AverageTeamHealth) * 0.6f;
            Score += TeamObs.ThreatLevel * 10.0f;
            Score += (TeamObs.KillDeathRatio < 0.5f) ? 25.0f : 0.0f;
            break;

        case EObjectiveType::None:
            Score = 0.0f;
            break;
    }

    return FMath::Max(0.0f, Score);
}

float UMCTS::CalculateObjectiveSynergy(EObjectiveType ObjType, const TMap<AActor*, EObjectiveType>& ExistingObjectives, const FTeamObservation& TeamObs) const
{
    if (ExistingObjectives.Num() == 0) return 0.0f;

    float Synergy = 0.0f;

    // Count objective types
    TMap<EObjectiveType, int32> ObjectiveCounts;
    for (const auto& Pair : ExistingObjectives)
    {
        ObjectiveCounts.FindOrAdd(Pair.Value, 0)++;
    }

    // Synergy bonuses for tactical diversity (v4.0 simplified)
    int32 UniqueTypes = ObjectiveCounts.Num();
    if (UniqueTypes >= 2)
    {
        Synergy += 5.0f; // Bonus for diverse tactics
    }

    // v4.0: Simplified synergy rules for 4 objective types
    switch (ObjType)
    {
        case EObjectiveType::Assault:
            // Good synergy with Support (covering fire)
            if (ObjectiveCounts.Contains(EObjectiveType::Support))
            {
                Synergy += 8.0f;
            }
            // Good synergy with Defend (hold ground while assaulting)
            if (ObjectiveCounts.Contains(EObjectiveType::Defend))
            {
                Synergy += 6.0f;
            }
            // Bad synergy with Retreat (conflicting)
            if (ObjectiveCounts.Contains(EObjectiveType::Retreat))
            {
                Synergy -= 12.0f;
            }
            break;

        case EObjectiveType::Defend:
            // Good synergy with Assault (defensive cover for offense)
            if (ObjectiveCounts.Contains(EObjectiveType::Assault))
            {
                Synergy += 6.0f;
            }
            // Good synergy with Support (defensive support)
            if (ObjectiveCounts.Contains(EObjectiveType::Support))
            {
                Synergy += 5.0f;
            }
            // Neutral/Slight negative with Retreat (both defensive but different intent)
            if (ObjectiveCounts.Contains(EObjectiveType::Retreat))
            {
                Synergy -= 3.0f;
            }
            break;

        case EObjectiveType::Support:
            // Good synergy with all offensive/defensive objectives
            if (ObjectiveCounts.Contains(EObjectiveType::Assault))
            {
                Synergy += 8.0f;
            }
            if (ObjectiveCounts.Contains(EObjectiveType::Defend))
            {
                Synergy += 5.0f;
            }
            // Good synergy with Retreat (cover withdrawal)
            if (ObjectiveCounts.Contains(EObjectiveType::Retreat))
            {
                Synergy += 4.0f;
            }
            break;

        case EObjectiveType::Retreat:
            // Good synergy with Support (cover withdrawal)
            if (ObjectiveCounts.Contains(EObjectiveType::Support))
            {
                Synergy += 6.0f;
            }
            // Bad synergy with Assault (conflicting)
            if (ObjectiveCounts.Contains(EObjectiveType::Assault))
            {
                Synergy -= 12.0f;
            }
            // Slight negative with Defend (different defensive intent)
            if (ObjectiveCounts.Contains(EObjectiveType::Defend))
            {
                Synergy -= 3.0f;
            }
            break;

        case EObjectiveType::None:
            // No synergy
            break;
    }

    // Penalty for too many of the same objective (diminishing returns)
    int32* ExistingCount = ObjectiveCounts.Find(ObjType);
    if (ExistingCount && *ExistingCount >= 2)
    {
        Synergy -= 5.0f * (*ExistingCount); // Increasing penalty
    }

    return Synergy;
}


TMap<AActor*, UObjective*> UMCTS::RunTeamMCTSWithObjectives(
    const FTeamObservation& TeamObservation,
    const TArray<AActor*>& Followers,
    UObjectiveManager* ObjectiveManager)
{
    UE_LOG(LogTemp, Log, TEXT("🎯 MCTS: Running objective-based tree search for %d followers (%d simulations)"),
        Followers.Num(), MaxSimulations);

    if (!ObjectiveManager)
    {
        UE_LOG(LogTemp, Error, TEXT("🎯 MCTS: ObjectiveManager is null! Cannot run objective-based MCTS"));
        return TMap<AActor*, UObjective*>();
    }

    return RunTeamMCTSTreeSearchWithObjectives(TeamObservation, Followers, ObjectiveManager);
}


TMap<AActor*, UObjective*> UMCTS::RunTeamMCTSTreeSearchWithObjectives(
    const FTeamObservation& TeamObs,
    const TArray<AActor*>& Followers,
    UObjectiveManager* ObjectiveManager)
{
    UE_LOG(LogTemp, Verbose, TEXT("🎯 MCTS OBJECTIVE SEARCH: Starting for %d followers (%d simulations)"),
        Followers.Num(), MaxSimulations);

    // Cache for simulation
    CachedTeamObservation = TeamObs;
    CachedObjectiveManager = ObjectiveManager;

    // Create root node with empty assignment
    TeamRootNode = MakeShared<FTeamMCTSNode>();

    // Generate initial objective assignments
    TArray<TMap<AActor*, UObjective*>> ObjectiveAssignments =
        GenerateObjectiveAssignments(Followers, TeamObs, ObjectiveManager, MaxCombinationsPerExpansion);

    if (ObjectiveAssignments.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("🎯 MCTS: No objective assignments generated! Aborting"));
        return TMap<AActor*, UObjective*>();
    }

    UE_LOG(LogTemp, Display, TEXT("🎯 MCTS: Root initialized with %d objective combinations"),
        ObjectiveAssignments.Num());

    // Compute priors for objective assignments using heuristic evaluation
    TArray<float> ObjectivePriors;
    if (RLPolicyNetwork)
    {
        // Get base priors from heuristic analysis for each objective type
        TArray<float> BasePriors = RLPolicyNetwork->GetObjectivePriors(TeamObs);

        // Compute prior for each objective assignment combination
        for (const auto& ObjAssignment : ObjectiveAssignments)
        {
            // Count objective types in this assignment
            TMap<EObjectiveType, int32> ObjectiveCounts;
            for (const auto& Pair : ObjAssignment)
            {
                if (Pair.Value)
                {
                    ObjectiveCounts.FindOrAdd(Pair.Value->Type, 0)++;
                }
            }

            // Compute combined prior for this assignment
            // Use geometric mean of individual objective priors
            float CombinedPrior = 1.0f;
            int32 TotalObjectives = 0;

            for (const auto& CountPair : ObjectiveCounts)
            {
                int32 ObjTypeIndex = static_cast<int32>(CountPair.Key);
                if (ObjTypeIndex >= 0 && ObjTypeIndex < BasePriors.Num())
                {
                    // Weight by count (multiple followers with same objective)
                    for (int32 i = 0; i < CountPair.Value; ++i)
                    {
                        CombinedPrior *= BasePriors[ObjTypeIndex];
                        TotalObjectives++;
                    }
                }
            }

            // Take Nth root to get geometric mean
            if (TotalObjectives > 0)
            {
                CombinedPrior = FMath::Pow(CombinedPrior, 1.0f / TotalObjectives);
            }

            // Bonus for tactical diversity (multiple objective types)
            if (ObjectiveCounts.Num() >= 2)
            {
                CombinedPrior *= 1.2f;  // 20% bonus for diverse tactics
            }

            ObjectivePriors.Add(CombinedPrior);
        }

        // Normalize priors to sum to 1.0
        float Sum = 0.0f;
        for (float Prior : ObjectivePriors)
        {
            Sum += Prior;
        }
        if (Sum > 0.0f)
        {
            for (int32 i = 0; i < ObjectivePriors.Num(); ++i)
            {
                ObjectivePriors[i] /= Sum;
            }
        }
    }
    else
    {
        // Fallback to uniform priors if RLPolicyNetwork initialization failed
        ObjectivePriors.Init(1.0f / FMath::Max(1, ObjectiveAssignments.Num()), ObjectiveAssignments.Num());
        UE_LOG(LogTemp, Warning, TEXT("🎯 MCTS: Using uniform priors (RLPolicyNetwork not initialized)"));
    }

    // Store objective assignments as untried actions (convert to command format for compatibility)
    // NOTE: This is a temporary bridge - TeamMCTSNode will be updated to support objectives natively in future

    // Assign computed priors to root node (Sprint 4)
    TeamRootNode->ActionPriors = ObjectivePriors;
    TeamRootNode->UntriedActions = ObjectiveAssignments;

    // Run MCTS simulations (parallel or sequential based on config)
    if (bEnableParallelSimulations && MaxSimulations >= ParallelBatchSize * 2)
    {
        // Parallel root parallelization: Run simulations in batches
        const int32 NumBatches = FMath::CeilToInt(static_cast<float>(MaxSimulations) / ParallelBatchSize);

        UE_LOG(LogTemp, Warning, TEXT("🔄 MCTS: Parallel mode enabled (%d simulations, %d batches of %d)"),
            MaxSimulations, NumBatches, ParallelBatchSize);

        for (int32 Batch = 0; Batch < NumBatches; ++Batch)
        {
            const int32 BatchStart = Batch * ParallelBatchSize;
            const int32 BatchEnd = FMath::Min(BatchStart + ParallelBatchSize, MaxSimulations);
            const int32 BatchSize = BatchEnd - BatchStart;

            // Run batch in parallel
            ParallelFor(BatchSize, [&](int32 SimIdx)
            {
                RunSingleSimulation(TeamRootNode, Followers, TeamObs);
            });
        }
    }
    else
    {
        // Sequential fallback (original implementation)
        UE_LOG(LogTemp, Warning, TEXT("🔄 MCTS: Sequential mode (%d simulations)"), MaxSimulations);

        for (int32 i = 0; i < MaxSimulations; ++i)
        {
            RunSingleSimulation(TeamRootNode, Followers, TeamObs);
        }
    }

    // Find best child
    TSharedPtr<FTeamMCTSNode> BestChild = nullptr;
    int32 MaxVisits = 0;

    for (const TSharedPtr<FTeamMCTSNode>& Child : TeamRootNode->Children)
    {
        if (Child.IsValid() && Child->VisitCount > MaxVisits)
        {
            MaxVisits = Child->VisitCount;
            BestChild = Child;
        }
    }

    // Convert best commands back to objective assignments
    TMap<AActor*, UObjective*> BestObjectives;

    if (BestChild.IsValid())
    {
        float AvgReward = BestChild->TotalReward / FMath::Max(1, BestChild->VisitCount);
        UE_LOG(LogTemp, Verbose, TEXT("🎯 MCTS OBJECTIVE SEARCH: Best child found (Visits: %d, Avg Reward: %.1f)"),
            MaxVisits, AvgReward);

        // Find corresponding objective assignment
        int32 BestIndex = TeamRootNode->Children.Find(BestChild);
        if (BestIndex >= 0 && BestIndex < ObjectiveAssignments.Num())
        {
            BestObjectives = ObjectiveAssignments[BestIndex];
        }
        else
        {
            UE_LOG(LogTemp, Verbose, TEXT("🎯 MCTS: Could not map best child to objective assignment, using first"));
            if (ObjectiveAssignments.Num() > 0)
            {
                BestObjectives = ObjectiveAssignments[0];
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("🎯 MCTS OBJECTIVE SEARCH FAILED: No valid solution found"));
        if (ObjectiveAssignments.Num() > 0)
        {
            BestObjectives = ObjectiveAssignments[0];
        }
    }

    return BestObjectives;
}


float UMCTS::CalculateTeamReward(const FTeamObservation& TeamObs, const TMap<AActor*, UObjective*>& Objectives) const
{
    // Base team reward
    float BaseReward = CalculateTeamReward(TeamObs);

    // Objective-specific bonuses
    float ObjectiveBonus = 0.0f;

    // Count objective types
    TMap<EObjectiveType, int32> ObjectiveCounts;
    for (const auto& Pair : Objectives)
    {
        if (Pair.Value)
        {
            ObjectiveCounts.FindOrAdd(Pair.Value->Type, 0)++;
        }
    }

    int32 TotalFollowers = Objectives.Num();

    // Bonus for tactical diversity
    if (ObjectiveCounts.Num() >= 2)
    {
        ObjectiveBonus += 25.0f; // Higher bonus for diversity
    }

    // Context-aware objective rewards (v4.0 simplified)
    if (TeamObs.TotalVisibleEnemies > 0)
    {
        int32 AssaultCount = ObjectiveCounts.FindRef(EObjectiveType::Assault);
        int32 SupportCount = ObjectiveCounts.FindRef(EObjectiveType::Support);
        int32 DefendCount = ObjectiveCounts.FindRef(EObjectiveType::Defend);

        // Reward combined arms (assault + support/defense)
        if (AssaultCount > 0 && (SupportCount > 0 || DefendCount > 0))
        {
            ObjectiveBonus += 35.0f; // Combined tactics bonus
        }

        // Penalty for poor tactical choices
        if (TeamObs.bOutnumbered && AssaultCount == TotalFollowers)
        {
            ObjectiveBonus -= 60.0f; // Heavy penalty for all-assault when outnumbered
        }

        if (TeamObs.AverageTeamHealth < 40.0f && ObjectiveCounts.FindRef(EObjectiveType::Retreat) == 0)
        {
            ObjectiveBonus -= 40.0f; // Penalty for not retreating when low health
        }
    }

    // Bonus for high-priority objectives
    for (const auto& Pair : Objectives)
    {
        if (Pair.Value && Pair.Value->Priority >= 7)
        {
            ObjectiveBonus += 10.0f;
        }
    }

    float TotalReward = BaseReward + ObjectiveBonus;

    UE_LOG(LogTemp, Verbose, TEXT("🎯 MCTS Objective Reward: %.1f (Base=%.1f, ObjBonus=%.1f)"),
        TotalReward, BaseReward, ObjectiveBonus);

    return TotalReward;
}

//==============================================================================
// MCTS STATISTICS EXPORT (Sprint 3 - Curriculum Learning)
//==============================================================================

void UMCTS::GetMCTSStatistics(float& OutValueVariance, float& OutPolicyEntropy, float& OutAverageValue) const
{
    OutValueVariance = 0.0f;
    OutPolicyEntropy = 0.0f;
    OutAverageValue = 0.0f;

    if (!TeamRootNode.IsValid() || TeamRootNode->Children.Num() == 0)
    {
        return;
    }

    // Calculate average value from root
    OutAverageValue = TeamRootNode->TotalReward / FMath::Max(1, TeamRootNode->VisitCount);

    // Calculate value variance across child nodes
    TArray<float> ChildValues;
    float SumValues = 0.0f;

    for (const TSharedPtr<FTeamMCTSNode>& Child : TeamRootNode->Children)
    {
        if (Child.IsValid() && Child->VisitCount > 0)
        {
            float ChildValue = Child->TotalReward / Child->VisitCount;
            ChildValues.Add(ChildValue);
            SumValues += ChildValue;
        }
    }

    if (ChildValues.Num() > 0)
    {
        float MeanValue = SumValues / ChildValues.Num();
        float VarianceSum = 0.0f;

        for (float Value : ChildValues)
        {
            float Diff = Value - MeanValue;
            VarianceSum += Diff * Diff;
        }

        OutValueVariance = FMath::Sqrt(VarianceSum / ChildValues.Num());
    }

    // Calculate policy entropy from visit count distribution
    TArray<float> VisitProbabilities;
    int32 TotalVisits = 0;

    for (const TSharedPtr<FTeamMCTSNode>& Child : TeamRootNode->Children)
    {
        if (Child.IsValid())
        {
            TotalVisits += Child->VisitCount;
        }
    }

    if (TotalVisits > 0)
    {
        for (const TSharedPtr<FTeamMCTSNode>& Child : TeamRootNode->Children)
        {
            if (Child.IsValid() && Child->VisitCount > 0)
            {
                float Prob = static_cast<float>(Child->VisitCount) / TotalVisits;
                VisitProbabilities.Add(Prob);
            }
        }

        // Calculate entropy: H(p) = -Σ p_i * log(p_i)
        for (float Prob : VisitProbabilities)
        {
            if (Prob > 0.0f)
            {
                OutPolicyEntropy -= Prob * FMath::Loge(Prob);
            }
        }
    }

    UE_LOG(LogTemp, Verbose, TEXT("MCTS Statistics: ValueVariance=%.3f, PolicyEntropy=%.3f, AvgValue=%.1f"),
        OutValueVariance, OutPolicyEntropy, OutAverageValue);
}

int32 UMCTS::GetRootVisitCount() const
{
    if (TeamRootNode.IsValid())
    {
        return TeamRootNode->VisitCount;
    }
    return 0;
}

//==============================================================================
// STRATEGIC HEURISTIC EVALUATION
//==============================================================================

float UMCTS::EvaluateObjectiveProgress(TSharedPtr<FTeamMCTSNode> Node, const FTeamObservation& TeamObs) const
{
    if (!Node.IsValid())
    {
        return 0.0f;
    }

    float Score = 0.0f;
    TMap<AActor*, UObjective*> NodeObjectives = Node->GetObjectives();

    for (const auto& Pair : NodeObjectives)
    {
        UObjective* Objective = Pair.Value;
        if (!Objective)
        {
            continue;
        }

        // Evaluate based on objective type and distance
        FVector ObjectiveLocation = Objective->TargetLocation;
        AActor* Follower = Pair.Key;

        if (!Follower || !Follower->IsValidLowLevel())
        {
            continue;
        }

        float Distance = FVector::Dist(Follower->GetActorLocation(), ObjectiveLocation);
        float NormalizedDistance = FMath::Clamp(Distance / 5000.0f, 0.0f, 1.0f); // 5000cm = 50m max

        // Strategic value based on objective type
        switch (Objective->Type)
        {
        case EObjectiveType::Assault:
            // Value proximity to objective (aggressive positioning)
            Score += (1.0f - NormalizedDistance) * 0.2f; // Max +0.2 per agent
            break;

        case EObjectiveType::Defend:
            // Value being AT objective location
            if (Distance < 500.0f) // Within 5m
            {
                Score += 0.15f;
            }
            break;

        case EObjectiveType::Support:
        {
            // Value moderate distance (not too far, not too close)
            float IdealDistance = FMath::Abs(NormalizedDistance - 0.5f);
            Score += (1.0f - IdealDistance * 2.0f) * 0.1f;
            break;
        }
        case EObjectiveType::Retreat:
            // Value distance FROM enemies (inverse)
            if (TeamObs.TotalVisibleEnemies > 0)
            {
                Score += NormalizedDistance * 0.15f; // Reward moving away
            }
            break;

        default:
            break;
        }
    }

    // Clamp to range [-0.6, +0.6]
    return FMath::Clamp(Score, -0.6f, 0.6f);
}

float UMCTS::EvaluateTeamStrength(const TArray<AActor*>& Followers, const FTeamObservation& TeamObs) const
{
    if (Followers.Num() == 0)
    {
        return -0.3f; // No team = worst case
    }

    float Score = 0.0f;
    int32 AliveCount = 0;
    float TotalHealth = 0.0f;
    float TotalAmmo = 0.0f;

    for (AActor* Follower : Followers)
    {
        if (!Follower || !Follower->IsValidLowLevel())
        {
            continue;
        }

        // Health contribution
        UHealthComponent* HealthComp = Follower->FindComponentByClass<UHealthComponent>();
        if (HealthComp && HealthComp->IsAlive())
        {
            AliveCount++;
            TotalHealth += HealthComp->GetHealthPercentage();
        }

        // Ammo contribution
        UWeaponComponent* WeaponComp = Follower->FindComponentByClass<UWeaponComponent>();
        if (WeaponComp)
        {
            TotalAmmo += WeaponComp->GetAmmoPercentage();
        }
    }

    if (AliveCount == 0)
    {
        return -0.3f; // All dead
    }

    // Normalize by team size
    float AvgHealth = TotalHealth / Followers.Num();
    float AvgAmmo = TotalAmmo / Followers.Num();
    float SurvivalRate = static_cast<float>(AliveCount) / Followers.Num();

    // Weighted score (survival > health > ammo)
    Score = (SurvivalRate * 0.15f) + (AvgHealth * 0.1f) + (AvgAmmo * 0.05f);

    // Clamp to range [-0.3, +0.3]
    return FMath::Clamp(Score - 0.15f, -0.3f, 0.3f); // Offset to center around 0
}

float UMCTS::EvaluatePositionalAdvantage(const TArray<AActor*>& Followers, const FTeamObservation& TeamObs) const
{
    if (Followers.Num() == 0)
    {
        return 0.0f;
    }

    float Score = 0.0f;
    int32 InCoverCount = 0;
    float AvgSpread = 0.0f;

    // Calculate team center
    FVector TeamCenter = FVector::ZeroVector;
    for (AActor* Follower : Followers)
    {
        if (Follower && Follower->IsValidLowLevel())
        {
            TeamCenter += Follower->GetActorLocation();
        }
    }
    TeamCenter /= Followers.Num();

    // Evaluate cover usage and formation
    for (AActor* Follower : Followers)
    {
        if (!Follower || !Follower->IsValidLowLevel())
        {
            continue;
        }

        // Cover bonus
        UFollowerAgentComponent* FollowerComp = Follower->FindComponentByClass<UFollowerAgentComponent>();
        if (FollowerComp)
        {
            FObservationElement Obs = FollowerComp->GetLocalObservation();
            if (Obs.bHasCover && Obs.NearestCoverDistance < 200.0f) // Within 2m of cover
            {
                InCoverCount++;
            }
        }

        // Formation spread (penalize clumping)
        float DistanceFromCenter = FVector::Dist(Follower->GetActorLocation(), TeamCenter);
        AvgSpread += DistanceFromCenter;
    }

    AvgSpread /= Followers.Num();

    // Cover score: +0.05 if >50% in cover
    float CoverRatio = static_cast<float>(InCoverCount) / Followers.Num();
    if (CoverRatio > 0.5f)
    {
        Score += 0.05f;
    }

    // Formation score: +0.05 if well-spread (500-2000cm apart)
    if (AvgSpread > 500.0f && AvgSpread < 2000.0f)
    {
        Score += 0.05f;
    }
    else if (AvgSpread < 200.0f)
    {
        Score -= 0.05f; // Penalty for clumping (AoE vulnerability)
    }

    // Clamp to range [-0.1, +0.1]
    return FMath::Clamp(Score, -0.1f, 0.1f);
}
