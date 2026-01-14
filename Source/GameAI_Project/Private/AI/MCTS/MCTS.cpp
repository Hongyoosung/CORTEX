#include "AI/MCTS/MCTS.h"
#include "Kismet/KismetMathLibrary.h"
#include "Team/TeamTypes.h"
#include "Team/FollowerAgentComponent.h"
#include "Team/ObjectiveActor.h"
#include "RL/RLPolicyNetwork.h"
#include "RL/RLTypes.h"  
#include "Combat/HealthComponent.h"
#include "Combat/WeaponComponent.h"
#include "Core/ProfilingMacros.h" 

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

    // v6.0: Initialize RL Policy Network for value estimates (actual neural network)
    RLPolicyNetwork = NewObject<URLPolicyNetwork>(this);

    UE_LOG(LogTemp, Log, TEXT("✅ [MCTS v8.0] Initialized for strategy assignment (Simulations: %d, Exploration: %.2f)"),
        MaxSimulations, ExplorationParameter);
    UE_LOG(LogTemp, Log, TEXT("✅ [MCTS v8.0] Using RL value estimates + coordination heuristics"));
}

//==============================================================================
// v8.0 API: STRATEGY ASSIGNMENT
//==============================================================================

TMap<AActor*, FStrategyAssignment> UMCTS::RunStrategyAssignment(
    const TArray<AActor*>& Agents,
    const TArray<AObjectiveActor*>& Objectives,
    int32 Simulations,
    const TMap<AActor*, FObservationElement>& InCachedObservations
)
{
    // Cache inputs for thread-safe async execution
    AvailableAgents = Agents;
    AvailableObjectives = Objectives;
    CachedObservations = InCachedObservations;

    // Initialize root node with empty assignments
    TMap<AActor*, FStrategyAssignment> EmptyAssignments;
    TeamRootNode = MakeShared<FTeamMCTSNode>();
    TeamRootNode->Initialize(nullptr, EmptyAssignments);

    // Generate all possible first-level strategy assignments
    TeamRootNode->UntriedActions = GeneratePossibleStrategyAssignments(EmptyAssignments);

    UE_LOG(LogTemp, Verbose, TEXT("[MCTS v8.0] Starting strategy assignment search: %d agents, %d objectives, %d simulations"),
        Agents.Num(), Objectives.Num(), Simulations);

    // Run MCTS simulations
    for (int32 i = 0; i < Simulations; ++i)
    {
        // 1. Selection: Traverse tree to leaf node
        TSharedPtr<FTeamMCTSNode> SelectedNode = Selection(TeamRootNode);

        // 2. Expansion: Add new child node
        TSharedPtr<FTeamMCTSNode> ExpandedNode = Expansion(SelectedNode);

        // 3. Simulation: Evaluate the assignment
        float Value = Simulation(ExpandedNode);

        // 4. Backpropagation: Update ancestors
        Backpropagation(ExpandedNode, Value);
    }

    // Select best child (highest visit count = most explored)
    TSharedPtr<FTeamMCTSNode> BestChild = TeamRootNode->SelectBestChild(0.0f);  // ExplorationParam=0 for pure exploitation

    if (BestChild.IsValid())
    {
        // 원본: TObjectPtr 키 사용
        TMap<TObjectPtr<AActor>, FStrategyAssignment> BestAssignmentsTObject = BestChild->GetStrategyAssignments();

        UE_LOG(LogTemp, Log, TEXT("[MCTS v8.0] Best assignment found: Value=%.2f, Visits=%d"),
            BestChild->TotalReward / FMath::Max(1, BestChild->VisitCount), BestChild->VisitCount);

        // 반환용: AActor* 키 사용
        TMap<AActor*, FStrategyAssignment> ResultAssignments;
        ResultAssignments.Reserve(BestAssignmentsTObject.Num()); // 성능 최적화

        for (auto& [AgentPtr, Assignment] : BestAssignmentsTObject)
        {
            // 값을 수정 (메트릭 추가)
            Assignment.ExpectedValue = BestChild->TotalReward / FMath::Max(1, BestChild->VisitCount);
            Assignment.VisitCount = BestChild->VisitCount;
            Assignment.Timestamp = FPlatformTime::Seconds();

            // TObjectPtr -> AActor* 자동 변환되어 저장됨
            ResultAssignments.Add(AgentPtr, Assignment);
        }

        return ResultAssignments;
    }

    // Fallback: Return root assignments (should never happen if simulations > 0)
    UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.0] No best child found, returning empty assignments"));
    return TMap<AActor*, FStrategyAssignment>();
}



//==============================================================================
// v6.0: MCTS CORE PHASES
//==============================================================================

TSharedPtr<FTeamMCTSNode> UMCTS::Selection(TSharedPtr<FTeamMCTSNode> Root)
{
    SCOPE_CYCLE_COUNTER(STAT_MCTSSelection);  // v6.0: Profile selection phase

    TSharedPtr<FTeamMCTSNode> Node = Root;
    TSharedPtr<FTeamMCTSNode> LastValidNode = Root;

    while (Node.IsValid() && !Node->IsTerminal())
    {
        if (!Node->IsFullyExpanded())
        {
            return Node;
        }
        else
        {
            LastValidNode = Node;  // Save last valid node before calling SelectBestChild
            Node = Node->SelectBestChild(ExplorationParameter);

            // If SelectBestChild returns nullptr (no children), return the last valid node
            if (!Node.IsValid())
            {
                UE_LOG(LogTemp, Verbose, TEXT("[MCTS v6.0] SelectBestChild returned nullptr, using last valid node"));
                return LastValidNode;
            }
        }
    }

    return Node;
}

TSharedPtr<FTeamMCTSNode> UMCTS::Expansion(TSharedPtr<FTeamMCTSNode> Node)
{
    SCOPE_CYCLE_COUNTER(STAT_MCTSExpansion);  // v6.0: Profile expansion phase

    if (!Node.IsValid() || Node->UntriedActions.Num() == 0)
    {
        return Node;
    }

    return Node->Expand(AvailableAgents);
}

float UMCTS::Simulation(TSharedPtr<FTeamMCTSNode> Node)
{
    SCOPE_CYCLE_COUNTER(STAT_MCTSSimulation);  // v6.0: Profile simulation phase

    if (!Node.IsValid())
    {
        return 0.0f;
    }

    // v8.0: Evaluate strategy assignments instead of mission assignments
	TMap<TObjectPtr<AActor>, FStrategyAssignment> Assignments = Node->GetStrategyAssignments();
	TMap<AActor*, FStrategyAssignment> AssignmentsAActor;
	AssignmentsAActor.Reserve(Assignments.Num());

    for (auto& [AgentPtr, Assignment] : Assignments)
    {
		AssignmentsAActor.Add(AgentPtr.Get(), Assignment);
    }


    return EvaluateStrategyAssignment(AssignmentsAActor);
}

void UMCTS::Backpropagation(TSharedPtr<FTeamMCTSNode> Node, float Value)
{
    SCOPE_CYCLE_COUNTER(STAT_MCTSBackpropagation);  // v6.0: Profile backpropagation phase

    while (Node.IsValid())
    {
        Node->Backpropagate(Value);
        Node = Node->Parent.Pin();
    }
}

//==============================================================================
// v8.0: ASSIGNMENT EVALUATION (RL-GUIDED)
//==============================================================================

float UMCTS::EvaluateStrategyAssignment(const TMap<AActor*, FStrategyAssignment>& Assignments)
{
    SCOPE_CYCLE_COUNTER(STAT_MCTSEvaluate);  // v6.0: Profile assignment evaluation

    if (!RLPolicyNetwork)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.0] RLPolicyNetwork is null, using fallback heuristic"));
        return 0.0f;
    }

    float TotalValue = 0.0f;
    int32 AgentCount = 0;

    // Query RL value for each agent-strategy-objective combination
    for (const auto& [Agent, Assignment] : Assignments)
    {
        if (!Agent || !Assignment.TargetObjective) continue;

        // v8.0: Use cached observation for thread safety
        const FObservationElement* CachedObs = CachedObservations.Find(Agent);
        if (!CachedObs)
        {
            UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.0] No cached observation for agent %s, skipping"), *Agent->GetName());
            continue;
        }

        // Build objective context from strategy assignment
        UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>();
        if (!FollowerComp) continue;

        // v8.0: Build context from objective actor
        FObjectiveContext ObjCtx;
        ObjCtx.TargetObjective = Assignment.TargetObjective;
        ObjCtx.Distance = FVector::Dist(Agent->GetActorLocation(), Assignment.TargetObjective->GetActorLocation());
        ObjCtx.Distance = FMath::Clamp(ObjCtx.Distance / 5000.0f, 0.0f, 1.0f);  // Normalize

        FVector Direction = (Assignment.TargetObjective->GetActorLocation() - Agent->GetActorLocation()).GetSafeNormal2D();
        ObjCtx.Direction = FVector2D(Direction.X, Direction.Y);
    }

    // Normalize by agent count
    float AverageValue = AgentCount > 0 ? TotalValue / AgentCount : 0.0f;

    // Add v8.0 coordination heuristics
    float Composition = TeamCompositionScore(Assignments);
    float Coverage = ObjectiveCoverageScore(Assignments);
    float Synergy = StrategySynergyScore(Assignments);

    // Weighted combination (RL = 60%, Coordination = 40%)
    float FinalValue = AverageValue * 0.6f + Composition * 0.15f + Coverage * 0.15f + Synergy * 0.1f;

    return FMath::Clamp(FinalValue, -1.0f, 1.0f);
}

TArray<TMap<AActor*, FStrategyAssignment>> UMCTS::GeneratePossibleStrategyAssignments(
    const TMap<AActor*, FStrategyAssignment>& CurrentAssignments
)
{
    TArray<TMap<AActor*, FStrategyAssignment>> PossibleAssignments;

    // v8.0: Generate strategy assignments for each agent × strategy × objective combination
    // Action space: 4 agents × 4 strategies × 2 objectives = simpler than v7.0

    for (AActor* Agent : AvailableAgents)
    {
        // Skip agents already assigned
        if (CurrentAssignments.Contains(Agent))
        {
            continue;
        }

        // Try each strategy
        for (int32 StratIdx = 0; StratIdx < static_cast<int32>(EStrategyType::COUNT); ++StratIdx)
        {
            EStrategyType Strategy = static_cast<EStrategyType>(StratIdx);

            // Try each objective
            for (AObjectiveActor* Objective : AvailableObjectives)
            {
                if (!Objective) continue;

                // Create new assignment map
                TMap<AActor*, FStrategyAssignment> NewAssignments = CurrentAssignments;

                // Build strategy assignment
                FStrategyAssignment Assignment;
                Assignment.Agent = Agent;
                Assignment.Strategy = Strategy;
                Assignment.TargetObjective = Objective;
                Assignment.Priority = 5;  // Default priority
                Assignment.Timestamp = FPlatformTime::Seconds();

                // Add assignment for this agent
                NewAssignments.Add(Agent, Assignment);

                PossibleAssignments.Add(NewAssignments);
            }
        }
    }

    // Limit to MaxCombinationsPerExpansion
    if (PossibleAssignments.Num() > MaxCombinationsPerExpansion)
    {
        // Shuffle and take top N (randomized sampling for diversity)
        for (int32 i = PossibleAssignments.Num() - 1; i > 0; --i)
        {
            int32 j = FMath::RandRange(0, i);
            PossibleAssignments.Swap(i, j);
        }
        PossibleAssignments.SetNum(MaxCombinationsPerExpansion);
    }

    return PossibleAssignments;
}

//==============================================================================
// v8.0: COORDINATION HEURISTICS
//==============================================================================

float UMCTS::TeamCompositionScore(const TMap<AActor*, FStrategyAssignment>& Assignments) const
{
    // v8.0: Higher score for balanced mix of strategies (not all assault, not all defend)
    // Penalize extreme compositions (e.g., 4 assault, 0 defend)

    if (Assignments.Num() == 0)
    {
        return 0.5f;
    }

    // Count strategy distribution
    TMap<EStrategyType, int32> StrategyCounts;
    for (const auto& [Agent, Assignment] : Assignments)
    {
        StrategyCounts.FindOrAdd(Assignment.Strategy, 0)++;
    }

    // Calculate diversity (Shannon entropy normalized)
    float Entropy = 0.0f;
    for (const auto& [Strategy, Count] : StrategyCounts)
    {
        if (Count > 0)
        {
            float Probability = static_cast<float>(Count) / Assignments.Num();
            Entropy -= Probability * FMath::Loge(Probability);
        }
    }

    // Normalize entropy: MaxEntropy = log(4) for 4 strategies
    float MaxEntropy = FMath::Loge(4.0f);
    float NormalizedEntropy = MaxEntropy > 0.0f ? Entropy / MaxEntropy : 0.0f;

    // Higher diversity = higher score (but not required to be perfectly balanced)
    return NormalizedEntropy;
}

float UMCTS::ObjectiveCoverageScore(const TMap<AActor*, FStrategyAssignment>& Assignments) const
{
    // v8.0: Higher score if both objectives have adequate coverage
    // Both friendly (defend) and hostile (assault) objectives should be covered

    if (Assignments.Num() == 0 || AvailableObjectives.Num() == 0)
    {
        return 0.5f;
    }

    // Count agents assigned to each objective
    TMap<AObjectiveActor*, int32> ObjectiveCounts;
    for (const auto& [Agent, Assignment] : Assignments)
    {
        if (Assignment.TargetObjective)
        {
            ObjectiveCounts.FindOrAdd(Assignment.TargetObjective, 0)++;
        }
    }

    // Calculate coverage ratio (objectives with at least 1 agent / total objectives)
    int32 CoveredObjectives = 0;
    for (const auto& [Objective, Count] : ObjectiveCounts)
    {
        if (Count > 0)
        {
            CoveredObjectives++;
        }
    }

    float CoverageRatio = static_cast<float>(CoveredObjectives) / AvailableObjectives.Num();

    // Bonus for balanced distribution (no objective with >75% of agents)
    float BalanceScore = 1.0f;
    for (const auto& [Objective, Count] : ObjectiveCounts)
    {
        float Ratio = static_cast<float>(Count) / Assignments.Num();
        if (Ratio > 0.75f)
        {
            BalanceScore -= 0.3f;  // Penalize over-concentration
        }
    }

    return FMath::Clamp(CoverageRatio * BalanceScore, 0.0f, 1.0f);
}

float UMCTS::StrategySynergyScore(const TMap<AActor*, FStrategyAssignment>& Assignments) const
{
    // v8.0: Higher score if compatible strategies work together
    // Examples:
    // - Assault + Support together = good (synergy)
    // - All Retreat = bad (no cohesion)
    // - Assault agents targeting same objective = good (focus fire)

    if (Assignments.Num() == 0)
    {
        return 0.5f;
    }

    float SynergyScore = 0.0f;
    int32 SynergyCount = 0;

    // Check pairwise strategy synergies
    TArray<AActor*> Agents;
    Assignments.GetKeys(Agents);

    for (int32 i = 0; i < Agents.Num() - 1; ++i)
    {
        for (int32 j = i + 1; j < Agents.Num(); ++j)
        {
            const FStrategyAssignment& A1 = Assignments[Agents[i]];
            const FStrategyAssignment& A2 = Assignments[Agents[j]];

            // Same objective + compatible strategies = synergy
            if (A1.TargetObjective == A2.TargetObjective)
            {
                // Assault + Assault = good (focus fire)
                if (A1.Strategy == EStrategyType::Assault && A2.Strategy == EStrategyType::Assault)
                {
                    SynergyScore += 1.0f;
                }
                // Assault + Support = excellent (cover and push)
                else if ((A1.Strategy == EStrategyType::Assault && A2.Strategy == EStrategyType::Support) ||
                         (A1.Strategy == EStrategyType::Support && A2.Strategy == EStrategyType::Assault))
                {
                    SynergyScore += 1.2f;
                }
                // Defend + Defend = good (hold position together)
                else if (A1.Strategy == EStrategyType::Defend && A2.Strategy == EStrategyType::Defend)
                {
                    SynergyScore += 1.0f;
                }
                // Retreat + Retreat = neutral (survival)
                else if (A1.Strategy == EStrategyType::Retreat && A2.Strategy == EStrategyType::Retreat)
                {
                    SynergyScore += 0.5f;
                }
                // Mixed = neutral
                else
                {
                    SynergyScore += 0.6f;
                }

                SynergyCount++;
            }
        }
    }

    return SynergyCount > 0 ? SynergyScore / SynergyCount : 0.5f;
}
