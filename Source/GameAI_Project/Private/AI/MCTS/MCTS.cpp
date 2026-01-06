#include "AI/MCTS/MCTS.h"
#include "Kismet/KismetMathLibrary.h"
#include "Team/TeamTypes.h"
#include "Team/ObjectiveManager.h"
#include "Team/Objective.h"
#include "Team/FollowerAgentComponent.h"
#include "RL/RLPolicyNetwork.h"
#include "RL/RLTypes.h"  // v5.0: FAssignmentScoreConfig for individual scoring
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

    // v6.0: Initialize RL Policy Network for value estimates (actual neural network)
    RLPolicyNetwork = NewObject<URLPolicyNetwork>(this);

    UE_LOG(LogTemp, Log, TEXT("✅ [MCTS v6.0] Initialized for objective assignment (Simulations: %d, Exploration: %.2f)"),
        MaxSimulations, ExplorationParameter);
    UE_LOG(LogTemp, Log, TEXT("✅ [MCTS v6.0] Using RL value estimates + coordination heuristics"));
}

//==============================================================================
// v6.0: OBJECTIVE ASSIGNMENT API
//==============================================================================

FObjectiveAssignment UMCTS::RunObjectiveAssignment(
    const TArray<AActor*>& Agents,
    const TArray<UObjective*>& Objectives,
    int32 Simulations)
{
    AvailableAgents = Agents;
    AvailableObjectives = Objectives;

    // Create root node (current assignment)
    TSharedPtr<FTeamMCTSNode> Root = MakeShared<FTeamMCTSNode>();

    // Build initial assignment (empty or current assignment)
    TMap<AActor*, UObjective*> InitialMapping;
    for (AActor* Agent : Agents)
    {
        // Assign to first objective as default (will be explored by MCTS)
        if (Objectives.Num() > 0)
        {
            InitialMapping.Add(Agent, Objectives[0]);
        }
    }

    Root->Initialize(nullptr, InitialMapping);

    // Generate possible assignments and extract maps for UntriedActions
    FObjectiveAssignment InitialAssignment;

    InitialAssignment.AgentToObjective.Empty(InitialMapping.Num());
    for( const auto& [Agent, Objective] : InitialMapping)
    {
        InitialAssignment.AgentToObjective.Add(Agent, Objective);
	}

    TArray<FObjectiveAssignment> PossibleAssignments = GeneratePossibleAssignments(InitialAssignment);

    // Convert FObjectiveAssignment array to TMap array for UntriedActions
    Root->UntriedActions.Empty();
    for (const FObjectiveAssignment& Assignment : PossibleAssignments)
    {
        TMap<AActor*, UObjective*> MapForNode;
        for (const auto& [Agent, Objective] : Assignment.AgentToObjective)
        {
            MapForNode.Add(Agent.Get(), Objective.Get());
        }
        Root->UntriedActions.Add(MapForNode);
    }

    // Run MCTS simulations
    for (int32 i = 0; i < Simulations; ++i)
    {
        // Selection: Traverse tree to most promising node
        TSharedPtr<FTeamMCTSNode> Node = Selection(Root);

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
        FObjectiveAssignment Result;
        Result.AgentToObjective = BestChild->GetObjectives();
        Result.ExpectedValue = BestChild->TotalReward / FMath::Max(BestChild->VisitCount, 1);
        Result.VisitCount = BestChild->VisitCount;
        Result.Timestamp = FPlatformTime::Seconds();

        UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v6.0] Best assignment: Value=%.2f, Visits=%d"),
            Result.ExpectedValue, Result.VisitCount);

        return Result;
    }

    // Fallback: Return root assignment
    FObjectiveAssignment FallbackResult;
    FallbackResult.AgentToObjective = Root->GetObjectives();
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
    TSharedPtr<FTeamMCTSNode> Node = Root;

    while (Node.IsValid() && !Node->IsTerminal())
    {
        if (!Node->IsFullyExpanded())
        {
            return Node;
        }
        else
        {
            Node = Node->SelectBestChild(ExplorationParameter);
        }
    }

    return Node;
}

TSharedPtr<FTeamMCTSNode> UMCTS::Expansion(TSharedPtr<FTeamMCTSNode> Node)
{
    if (!Node.IsValid() || Node->UntriedActions.Num() == 0)
    {
        return Node;
    }

    return Node->Expand(AvailableAgents);
}

float UMCTS::Simulation(TSharedPtr<FTeamMCTSNode> Node)
{
    if (!Node.IsValid())
    {
        return 0.0f;
    }

    FObjectiveAssignment Assignment;
    Assignment.AgentToObjective = Node->GetObjectives();

    return EvaluateAssignment(Assignment);
}

void UMCTS::Backpropagation(TSharedPtr<FTeamMCTSNode> Node, float Value)
{
    while (Node.IsValid())
    {
        Node->Backpropagate(Value);
        Node = Node->Parent.Pin();
    }
}

//==============================================================================
// v6.0: ASSIGNMENT EVALUATION (RL-GUIDED)
//==============================================================================

float UMCTS::EvaluateAssignment(const FObjectiveAssignment& Assignment)
{
    if (!RLPolicyNetwork)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v6.0] RLPolicyNetwork is null, using fallback heuristic"));
        return 0.0f;
    }

    float TotalValue = 0.0f;
    int32 AgentCount = 0;

    // Query RL value for each agent
    for (const auto& [Agent, Objective] : Assignment.AgentToObjective)
    {
        if (!Agent || !Objective) continue;

        // Build observation for agent
        UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>();
        if (!FollowerComp) continue;

        FObservationElement Obs = FollowerComp->BuildLocalObservation();
        FObjectiveContext ObjCtx = FollowerComp->BuildObjectiveContext(Objective);

        // Get RL value estimate
        float AgentValue = RLPolicyNetwork->GetStateValue(Obs, ObjCtx);
        TotalValue += AgentValue;
        AgentCount++;
    }

    // Normalize by agent count
    float AverageValue = AgentCount > 0 ? TotalValue / AgentCount : 0.0f;

    // Add coordination heuristics
    float Cohesion = TeamCohesionScore(Assignment);
    float Coverage = ObjectiveCoverageScore(Assignment);
    float Capability = CapabilityMatchScore(Assignment);

    // Weighted combination (RL = 60%, Coordination = 40%)
    float FinalValue = AverageValue * 0.6f + Cohesion * 0.15f + Coverage * 0.15f + Capability * 0.1f;

    return FMath::Clamp(FinalValue, -1.0f, 1.0f);
}

TArray<FObjectiveAssignment> UMCTS::GeneratePossibleAssignments(const FObjectiveAssignment& CurrentAssignment)
{
    TArray<FObjectiveAssignment> Assignments;

    // Generate simple permutations: For each agent, try assigning to each objective
    for (AActor* Agent : AvailableAgents)
    {
        for (UObjective* Objective : AvailableObjectives)
        {
            FObjectiveAssignment NewAssignment = CurrentAssignment;

            // Update this agent's assignment
            NewAssignment.AgentToObjective.Add(Agent, Objective);

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

float UMCTS::TeamCohesionScore(const FObjectiveAssignment& Assignment) const
{
    // Higher score if agents on same objective are near each other
    float CohesionScore = 0.0f;
    int32 PairCount = 0;

    TMap<UObjective*, TArray<AActor*>> ObjectiveToAgents;
    for (const auto& [Agent, Objective] : Assignment.AgentToObjective)
    {
        ObjectiveToAgents.FindOrAdd(Objective).Add(Agent);
    }

    for (const auto& [Objective, Agents] : ObjectiveToAgents)
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

float UMCTS::ObjectiveCoverageScore(const FObjectiveAssignment& Assignment) const
{
    // Higher score if all high-priority objectives have agents
    int32 CoveredHighPriority = 0;
    int32 TotalHighPriority = 0;

    for (UObjective* Obj : AvailableObjectives)
    {
        if (Obj->Priority >= 7) // High priority threshold
        {
            TotalHighPriority++;

            // Check if any agent is assigned to this objective
            for (const auto& [Agent, AssignedObj] : Assignment.AgentToObjective)
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

float UMCTS::CapabilityMatchScore(const FObjectiveAssignment& Assignment) const
{
    // Higher score if healthy agents assigned to offensive objectives, etc.
    float MatchScore = 0.0f;
    int32 AssignmentCount = 0;

    for (const auto& [Agent, Objective] : Assignment.AgentToObjective)
    {
        if (!Agent || !Objective) continue;

        UHealthComponent* HealthComp = Agent->FindComponentByClass<UHealthComponent>();
        if (!HealthComp) continue;

        float Health = HealthComp->GetCurrentHealth() / HealthComp->GetMaxHealth();

        // Heuristic: Healthy agents for offensive, damaged agents for defensive
        if (Objective->Type == EObjectiveType::Capture && Health > 0.7f)
        {
            MatchScore += 1.0f; // Good match
        }
        else if (Objective->Type == EObjectiveType::Defend && Health < 0.5f)
        {
            MatchScore += 0.8f; // Reasonable (defend while healing)
        }
        else if (Objective->Type == EObjectiveType::Retreat && Health < 0.3f)
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
