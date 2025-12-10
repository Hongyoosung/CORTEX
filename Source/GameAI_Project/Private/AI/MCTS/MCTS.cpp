#include "AI/MCTS/MCTS.h"
#include "Kismet/KismetMathLibrary.h"
#include "Team/TeamTypes.h"
#include "Team/ObjectiveManager.h"
#include "Team/Objective.h"
#include "Team/FollowerAgentComponent.h"
#include "RL/RLPolicyNetwork.h"

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

    // Initialize RL Policy Network (used for both actions AND value estimation via PPO critic)
    RLPolicyNetwork = NewObject<URLPolicyNetwork>(this);
    if (RLPolicyNetwork)
    {
        FRLPolicyConfig PolicyConfig;
        PolicyConfig.InputSize = 78;  // 71 observation + 7 objective
        PolicyConfig.OutputSize = 8;  // 8 atomic action dimensions
        RLPolicyNetwork->Initialize(PolicyConfig);

        // Try to load trained policy (fallback to heuristic if not found)
        FString PolicyModelPath = TEXT("Models/rl_policy_network.onnx");
        if (!RLPolicyNetwork->LoadPolicy(PolicyModelPath))
        {
            UE_LOG(LogTemp, Warning, TEXT("MCTS: RL Policy model not loaded, using heuristic fallback"));
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("MCTS: RL Policy loaded successfully (actor + critic from PPO training)"));
        }
    }

    // NOTE: Value estimation now uses RLPolicyNetwork->GetStateValue() (PPO critic)
    // This aggregates individual follower values instead of requiring team-level observations
    // Trained via real-time RLlib PPO (no offline self-play needed)

    UE_LOG(LogTemp, Log, TEXT("MCTS: Initialized for team-level decisions (Simulations: %d, Exploration: %.2f)"),
        MaxSimulations, ExplorationParameter);
    UE_LOG(LogTemp, Log, TEXT("MCTS: Using real-time PPO critic for value estimation (no offline training)"));
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
    if (!Node.IsValid() || !RLPolicyNetwork)
    {
        return 0.0f;
    }

    // AlphaZero-style: Use PPO critic to estimate leaf value (no rollout simulation)
    // Aggregate individual follower values for team-level estimate

    TMap<AActor*, UObjective*> NodeObjectives = Node->GetObjectives();
    float TotalValue = 0.0f;
    int32 ValidCount = 0;

    // For each follower with an objective, estimate their value
    for (const auto& Pair : NodeObjectives)
    {
        AActor* Follower = Pair.Key;
        UObjective* Objective = Pair.Value;

        if (!Follower || !Objective)
        {
            continue;
        }

        // Extract individual follower observation from follower component
        UFollowerAgentComponent* FollowerComponent = Follower->FindComponentByClass<UFollowerAgentComponent>();
        if (!FollowerComponent)
        {
            continue;
        }

        FObservationElement FollowerObs = FollowerComponent->GetLocalObservation();

        // Get PPO critic's value estimate for this follower-objective pair
        float FollowerValue = RLPolicyNetwork->GetStateValue(FollowerObs, Objective);
        TotalValue += FollowerValue;
        ValidCount++;
    }

    // Return averaged team value (normalized to [-1, 1])
    return ValidCount > 0 ? (TotalValue / ValidCount) : 0.0f;
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

    // Available objective types (7 strategic objectives)
    TArray<EObjectiveType> PossibleObjectives = {
        EObjectiveType::Eliminate,
        EObjectiveType::CaptureObjective,
        EObjectiveType::DefendObjective,
        EObjectiveType::SupportAlly,
        EObjectiveType::FormationMove,
        EObjectiveType::Retreat,
        EObjectiveType::RescueAlly
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

            // Select target based on objective type (same logic as before)
            switch (ObjType)
            {
                case EObjectiveType::Eliminate:
                    if (TeamObs.TrackedEnemies.Num() > 0)
                    {
                        TArray<AActor*> EnemyArray = TeamObs.TrackedEnemies.Array();
                        TargetActor = EnemyArray[FMath::RandRange(0, EnemyArray.Num() - 1)];
                        Priority = 7;
                    }
                    break;

                case EObjectiveType::SupportAlly:
                    if (Followers.Num() > 1)
                    {
                        TArray<AActor*> OtherFollowers = Followers;
                        OtherFollowers.Remove(Follower);
                        if (OtherFollowers.Num() > 0)
                        {
                            TargetActor = OtherFollowers[FMath::RandRange(0, OtherFollowers.Num() - 1)];
                            Priority = 6;
                        }
                    }
                    break;

                case EObjectiveType::CaptureObjective:
                    TargetLocation = TeamObs.TeamCentroid;
                    Priority = 8;
                    break;

                case EObjectiveType::DefendObjective:
                    TargetLocation = TeamObs.TeamCentroid;
                    Priority = 7;
                    break;

                case EObjectiveType::FormationMove:
                    TargetLocation = TeamObs.TeamCentroid;
                    Priority = 5;
                    break;

                case EObjectiveType::Retreat:
                    if (TeamObs.TrackedEnemies.Num() > 0)
                    {
                        TArray<AActor*> EnemyArray = TeamObs.TrackedEnemies.Array();
                        TargetActor = EnemyArray[FMath::RandRange(0, EnemyArray.Num() - 1)];
                    }
                    TargetLocation = TeamObs.TeamCentroid;
                    Priority = 6;
                    break;

                case EObjectiveType::RescueAlly:
                    if (Followers.Num() > 1)
                    {
                        TArray<AActor*> OtherFollowers = Followers;
                        OtherFollowers.Remove(Follower);
                        if (OtherFollowers.Num() > 0)
                        {
                            TargetActor = OtherFollowers[FMath::RandRange(0, OtherFollowers.Num() - 1)];
                            Priority = 7;
                        }
                    }
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

    // Get follower observation (if available via index)
    // For simplicity, use team-level heuristics

    switch (ObjType)
    {
        case EObjectiveType::Eliminate:
            // Prefer when enemies are present and team health is good
            Score = TeamObs.TrackedEnemies.Num() * 10.0f;
            Score += TeamObs.AverageTeamHealth * 0.5f;
            Score -= TeamObs.ThreatLevel * 5.0f; // Avoid if under heavy fire
            break;

        case EObjectiveType::CaptureObjective:
            // Prefer when near objective and team is coordinated
            Score = 50.0f - (TeamObs.DistanceToObjective / 100.0f);
            Score += TeamObs.FormationCoherence * 20.0f;
            Score -= TeamObs.ThreatLevel * 3.0f;
            break;

        case EObjectiveType::DefendObjective:
            // Prefer when on objective and threat is moderate
            Score = 30.0f - (TeamObs.DistanceToObjective / 50.0f);
            Score += TeamObs.ThreatLevel * 5.0f; // Higher threat = more defense needed
            Score += TeamObs.FormationCoherence * 15.0f;
            break;

        case EObjectiveType::SupportAlly:
            // Prefer when team health is low or formation broken
            Score = (100.0f - TeamObs.AverageTeamHealth) * 0.3f;
            Score += (1.0f - TeamObs.FormationCoherence) * 20.0f;
            break;

        case EObjectiveType::FormationMove:
            // Prefer when formation is broken
            Score = (1.0f - TeamObs.FormationCoherence) * 30.0f;
            Score += 10.0f; // Base utility
            break;

        case EObjectiveType::Retreat:
            // Prefer when health is low or threat is high
            Score = (100.0f - TeamObs.AverageTeamHealth) * 0.5f;
            Score += TeamObs.ThreatLevel * 10.0f;
            Score += (TeamObs.KillDeathRatio < 0.5f) ? 20.0f : 0.0f;
            break;

        case EObjectiveType::RescueAlly:
            // Prefer when allies are low health
            Score = (100.0f - TeamObs.AverageTeamHealth) * 0.4f;
            Score += TeamObs.ThreatLevel * 3.0f;
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

    // Synergy bonuses for tactical diversity
    int32 UniqueTypes = ObjectiveCounts.Num();
    if (UniqueTypes >= 2)
    {
        Synergy += 5.0f; // Bonus for diverse tactics
    }

    // Specific synergies
    switch (ObjType)
    {
        case EObjectiveType::Eliminate:
            // Good synergy with Support (covering fire)
            if (ObjectiveCounts.Contains(EObjectiveType::SupportAlly))
            {
                Synergy += 8.0f;
            }
            // Good synergy with Defend (hold and eliminate)
            if (ObjectiveCounts.Contains(EObjectiveType::DefendObjective))
            {
                Synergy += 6.0f;
            }
            // Bad synergy with Retreat (conflicting objectives)
            if (ObjectiveCounts.Contains(EObjectiveType::Retreat))
            {
                Synergy -= 10.0f;
            }
            break;

        case EObjectiveType::CaptureObjective:
            // Good synergy with FormationMove (coordinated advance)
            if (ObjectiveCounts.Contains(EObjectiveType::FormationMove))
            {
                Synergy += 7.0f;
            }
            // Good synergy with Support (covering fire while advancing)
            if (ObjectiveCounts.Contains(EObjectiveType::SupportAlly))
            {
                Synergy += 5.0f;
            }
            // Bad synergy with Retreat
            if (ObjectiveCounts.Contains(EObjectiveType::Retreat))
            {
                Synergy -= 12.0f;
            }
            break;

        case EObjectiveType::DefendObjective:
            // Good synergy with Eliminate (defensive posture)
            if (ObjectiveCounts.Contains(EObjectiveType::Eliminate))
            {
                Synergy += 6.0f;
            }
            // Bad synergy with FormationMove (conflicting - static vs dynamic)
            if (ObjectiveCounts.Contains(EObjectiveType::FormationMove))
            {
                Synergy -= 5.0f;
            }
            break;

        case EObjectiveType::SupportAlly:
            // Good synergy with most offensive objectives
            if (ObjectiveCounts.Contains(EObjectiveType::Eliminate))
            {
                Synergy += 8.0f;
            }
            if (ObjectiveCounts.Contains(EObjectiveType::CaptureObjective))
            {
                Synergy += 5.0f;
            }
            break;

        case EObjectiveType::FormationMove:
            // Good synergy with CaptureObjective (coordinated advance)
            if (ObjectiveCounts.Contains(EObjectiveType::CaptureObjective))
            {
                Synergy += 7.0f;
            }
            // Neutral with Retreat (both movement-based)
            if (ObjectiveCounts.Contains(EObjectiveType::Retreat))
            {
                Synergy += 2.0f;
            }
            break;

        case EObjectiveType::Retreat:
            // Good synergy with RescueAlly (both defensive)
            if (ObjectiveCounts.Contains(EObjectiveType::RescueAlly))
            {
                Synergy += 5.0f;
            }
            // Bad synergy with offensive objectives
            if (ObjectiveCounts.Contains(EObjectiveType::Eliminate) ||
                ObjectiveCounts.Contains(EObjectiveType::CaptureObjective))
            {
                Synergy -= 10.0f;
            }
            break;

        case EObjectiveType::RescueAlly:
            // Good synergy with Support
            if (ObjectiveCounts.Contains(EObjectiveType::SupportAlly))
            {
                Synergy += 6.0f;
            }
            // Good synergy with Retreat (defensive)
            if (ObjectiveCounts.Contains(EObjectiveType::Retreat))
            {
                Synergy += 5.0f;
            }
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

    // v3.0 Sprint 4: Compute priors for objective assignments using RL policy
    TArray<float> ObjectivePriors;
    if (RLPolicyNetwork && RLPolicyNetwork->IsReady())
    {
        // Get base priors from RL policy for each objective type
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
        // Fallback to uniform priors if no RL policy available
        ObjectivePriors.Init(1.0f / FMath::Max(1, ObjectiveAssignments.Num()), ObjectiveAssignments.Num());
        UE_LOG(LogTemp, Verbose, TEXT("🎯 MCTS: Using uniform priors (no RL policy available)"));
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

    // Context-aware objective rewards
    if (TeamObs.TotalVisibleEnemies > 0)
    {
        int32 EliminateCount = ObjectiveCounts.FindRef(EObjectiveType::Eliminate);
        int32 SupportCount = ObjectiveCounts.FindRef(EObjectiveType::SupportAlly);
        int32 DefendCount = ObjectiveCounts.FindRef(EObjectiveType::DefendObjective);

        // Reward combined arms (attack + support/defense)
        if (EliminateCount > 0 && (SupportCount > 0 || DefendCount > 0))
        {
            ObjectiveBonus += 35.0f; // Combined tactics bonus
        }

        // Penalty for poor tactical choices
        if (TeamObs.bOutnumbered && EliminateCount == TotalFollowers)
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
