#include "AI/MCTS/MCTS.h"
#include "Kismet/KismetMathLibrary.h"
#include "Team/TeamTypes.h"
#include "Team/MissionManager.h"
#include "Team/Mission.h"
#include "Team/FollowerAgentComponent.h"
#include "RL/RLPolicyNetwork.h"
#include "RL/RLTypes.h"  // v5.0: FAssignmentScoreConfig for individual scoring
#include "Combat/HealthComponent.h"
#include "Combat/WeaponComponent.h"
#include "Core/ProfilingMacros.h"  // v6.0: Performance profiling

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

    UE_LOG(LogTemp, Log, TEXT("✅ [MCTS v7.0] Initialized for mission assignment (Simulations: %d, Exploration: %.2f)"),
        MaxSimulations, ExplorationParameter);
    UE_LOG(LogTemp, Log, TEXT("✅ [MCTS v7.0] Using RL value estimates + coordination heuristics"));
}

//==============================================================================
// v7.0: MISSION ASSIGNMENT API
//==============================================================================

FMissionAssignment UMCTS::RunMissionAssignment(
    const TArray<AActor*>& Agents,
    const TArray<UMission*>& Missions,
    int32 Simulations,
    const TMap<AActor*, FObservationElement>& InCachedObservations)
{
    SCOPE_CYCLE_COUNTER(STAT_MCTSAssignment);  // v7.0: Profile total assignment time (target: <50ms)

    AvailableAgents = Agents;
    AvailableMissions = Missions;
    CachedObservations = InCachedObservations; // v6.0 fix: Store observations for thread-safe async execution

    // Create root node (current assignment)
    TSharedPtr<FTeamMCTSNode> Root = MakeShared<FTeamMCTSNode>();

    // Build initial assignment (empty or current assignment)
    TMap<AActor*, UMission*> InitialMapping;
    for (AActor* Agent : Agents)
    {
        // Assign to first mission as default (will be explored by MCTS)
        if (Missions.Num() > 0)
        {
            InitialMapping.Add(Agent, Missions[0]);
        }
    }

    Root->Initialize(nullptr, InitialMapping);

    // Generate possible assignments and extract maps for UntriedActions
    FMissionAssignment InitialAssignment;

    InitialAssignment.AgentToMission.Empty(InitialMapping.Num());
    for( const auto& [Agent, Mission] : InitialMapping)
    {
        InitialAssignment.AgentToMission.Add(Agent, Mission);
	}

    TArray<FMissionAssignment> PossibleAssignments = GeneratePossibleAssignments(InitialAssignment);

    // Convert FMissionAssignment array to TMap array for UntriedActions
    Root->UntriedActions.Empty();
    for (const FMissionAssignment& Assignment : PossibleAssignments)
    {
        TMap<AActor*, UMission*> MapForNode;
        for (const auto& [Agent, Mission] : Assignment.AgentToMission)
        {
            MapForNode.Add(Agent.Get(), Mission.Get());
        }
        Root->UntriedActions.Add(MapForNode);
    }

    // Run MCTS simulations
    for (int32 i = 0; i < Simulations; ++i)
    {
        // Selection: Traverse tree to most promising node
        TSharedPtr<FTeamMCTSNode> Node = Selection(Root);

        // Safety check: Selection can return invalid node if SelectBestChild returns nullptr
        if (!Node.IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("[MCTS v6.0] Selection returned invalid node at iteration %d, using root"), i);
            Node = Root;
        }

        // Expansion: Add new child node
        if (!Node->IsFullyExpanded() && Node->UntriedActions.Num() > 0)
        {
            Node = Expansion(Node);
        }

        // Simulation: Evaluate assignment
        float Value = Simulation(Node);

        // Backpropagation: Update ancestors
        Backpropagation(Node, Value);
    }

    // Select best child (highest visit count = most robust)
    TSharedPtr<FTeamMCTSNode> BestChild = nullptr;
    int32 MaxVisits = 0;
    for (const auto& Child : Root->Children)
    {
        if (Child->VisitCount > MaxVisits)
        {
            MaxVisits = Child->VisitCount;
            BestChild = Child;
        }
    }

    if (BestChild.IsValid())
    {
        FMissionAssignment Result;
        Result.AgentToMission = BestChild->GetMissions();
        Result.ExpectedValue = BestChild->TotalReward / FMath::Max(BestChild->VisitCount, 1);
        Result.VisitCount = BestChild->VisitCount;
        Result.Timestamp = FPlatformTime::Seconds();

        UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v6.0] Best assignment: Value=%.2f, Visits=%d"),
            Result.ExpectedValue, Result.VisitCount);

        return Result;
    }

    // Fallback: Return root assignment
    FMissionAssignment FallbackResult;
    FallbackResult.AgentToMission = Root->GetMissions();
    FallbackResult.ExpectedValue = 0.0f;
    FallbackResult.VisitCount = 0;
    FallbackResult.Timestamp = FPlatformTime::Seconds();
    return FallbackResult;
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

    FMissionAssignment Assignment;
    Assignment.AgentToMission = Node->GetMissions();

    return EvaluateAssignment(Assignment);
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
// v6.0: ASSIGNMENT EVALUATION (RL-GUIDED)
//==============================================================================

float UMCTS::EvaluateAssignment(const FMissionAssignment& Assignment)
{
    SCOPE_CYCLE_COUNTER(STAT_MCTSEvaluate);  // v6.0: Profile assignment evaluation

    if (!RLPolicyNetwork)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v6.0] RLPolicyNetwork is null, using fallback heuristic"));
        return 0.0f;
    }

    float TotalValue = 0.0f;
    int32 AgentCount = 0;

    // Query RL value for each agent
    for (const auto& [Agent, Mission] : Assignment.AgentToMission)
    {
        if (!Agent || !Mission) continue;

        // v6.0 fix: Use cached observation for thread safety
        const FObservationElement* CachedObs = CachedObservations.Find(Agent);
        if (!CachedObs)
        {
            UE_LOG(LogTemp, Warning, TEXT("[MCTS v6.0] No cached observation for agent %s, skipping"), *Agent->GetName());
            continue;
        }

        // Build Mission context (thread-safe - no GetWorld() calls)
        UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>();
        if (!FollowerComp) continue;

        FMissionContext ObjCtx = FollowerComp->BuildMissionContext(Mission);

        // Get RL value estimate using cached observation
        float AgentValue = RLPolicyNetwork->GetStateValue(*CachedObs, ObjCtx);
        TotalValue += AgentValue;
        AgentCount++;
    }

    // Normalize by agent count
    float AverageValue = AgentCount > 0 ? TotalValue / AgentCount : 0.0f;

    // Add coordination heuristics
    float Cohesion = TeamCohesionScore(Assignment);
    float Coverage = MissionCoverageScore(Assignment);
    float Capability = CapabilityMatchScore(Assignment);

    // Weighted combination (RL = 60%, Coordination = 40%)
    float FinalValue = AverageValue * 0.6f + Cohesion * 0.15f + Coverage * 0.15f + Capability * 0.1f;

    return FMath::Clamp(FinalValue, -1.0f, 1.0f);
}

TArray<FMissionAssignment> UMCTS::GeneratePossibleAssignments(const FMissionAssignment& CurrentAssignment)
{
    TArray<FMissionAssignment> Assignments;

    // Generate simple permutations: For each agent, try assigning to each Mission
    for (AActor* Agent : AvailableAgents)
    {
        for (UMission* Mission : AvailableMissions)
        {
            FMissionAssignment NewAssignment = CurrentAssignment;

            // Update this agent's assignment
            NewAssignment.AgentToMission.Add(Agent, Mission);

            Assignments.Add(NewAssignment);
        }
    }

    // Limit to MaxCombinationsPerExpansion
    if (Assignments.Num() > MaxCombinationsPerExpansion)
    {
        Assignments.SetNum(MaxCombinationsPerExpansion);
    }

    return Assignments;
}

//==============================================================================
// v6.0: COORDINATION HEURISTICS
//==============================================================================

float UMCTS::TeamCohesionScore(const FMissionAssignment& Assignment) const
{
    // Higher score if agents on same Mission are near each other
    float CohesionScore = 0.0f;
    int32 PairCount = 0;

    TMap<UMission*, TArray<AActor*>> MissionToAgents;
    for (const auto& [Agent, Mission] : Assignment.AgentToMission)
    {
        MissionToAgents.FindOrAdd(Mission).Add(Agent);
    }

    for (const auto& [Mission, Agents] : MissionToAgents)
    {
        if (Agents.Num() < 2) continue;

        // Check pairwise distances
        for (int32 i = 0; i < Agents.Num() - 1; ++i)
        {
            for (int32 j = i + 1; j < Agents.Num(); ++j)
            {
                float Distance = FVector::Dist(Agents[i]->GetActorLocation(), Agents[j]->GetActorLocation());
                float NormalizedDist = FMath::Clamp(Distance / 2000.0f, 0.0f, 1.0f); // 2000cm = 20m
                CohesionScore += (1.0f - NormalizedDist); // Closer = higher score
                PairCount++;
            }
        }
    }

    return PairCount > 0 ? CohesionScore / PairCount : 0.5f;
}

float UMCTS::MissionCoverageScore(const FMissionAssignment& Assignment) const
{
    // Higher score if all high-priority Missions have agents
    int32 CoveredHighPriority = 0;
    int32 TotalHighPriority = 0;

    for (UMission* Obj : AvailableMissions)
    {
        if (Obj->Priority >= 7) // High priority threshold
        {
            TotalHighPriority++;

            // Check if any agent is assigned to this Mission
            for (const auto& [Agent, AssignedObj] : Assignment.AgentToMission)
            {
                if (AssignedObj == Obj)
                {
                    CoveredHighPriority++;
                    break;
                }
            }
        }
    }

    return TotalHighPriority > 0 ? static_cast<float>(CoveredHighPriority) / TotalHighPriority : 0.5f;
}

float UMCTS::CapabilityMatchScore(const FMissionAssignment& Assignment) const
{
    // Higher score if healthy agents assigned to offensive Missions, etc.
    float MatchScore = 0.0f;
    int32 AssignmentCount = 0;

    for (const auto& [Agent, Mission] : Assignment.AgentToMission)
    {
        if (!Agent || !Mission) continue;

        UHealthComponent* HealthComp = Agent->FindComponentByClass<UHealthComponent>();
        if (!HealthComp) continue;

        float Health = HealthComp->GetCurrentHealth() / HealthComp->GetMaxHealth();

        // Heuristic: Healthy agents for offensive, damaged agents for defensive (v7.0: Capture → Assault)
        if (Mission->Type == EMissionType::Assault && Health > 0.7f)
        {
            MatchScore += 1.0f; // Good match
        }
        else if (Mission->Type == EMissionType::Defend && Health < 0.5f)
        {
            MatchScore += 0.8f; // Reasonable (defend while healing)
        }
        else if (Mission->Type == EMissionType::Retreat && Health < 0.3f)
        {
            MatchScore += 1.0f; // Good match
        }
        else
        {
            MatchScore += 0.5f; // Neutral
        }

        AssignmentCount++;
    }

    return AssignmentCount > 0 ? MatchScore / AssignmentCount : 0.5f;
}
