#include "AI/MCTS/MCTS.h"
#include "Kismet/KismetMathLibrary.h"
#include "Team/TeamTypes.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Team/ObjectiveActor.h"
#include "RL/RLPolicyNetwork.h"
#include "RL/RLTypes.h"  
#include "Combat/Components/HealthComponent.h"
#include "Combat/Components/WeaponComponent.h"
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

    // v8.20: Initialize batch prototypes
    BatchPrototypes.Empty(8);

    // Prototype 1: Tight Assault (3 Assault, 1 Support)
    FBatchPrototype TightAssault;
    TightAssault.Name = TEXT("TightAssault");
    TightAssault.Strategies = {
        EStrategyType::Assault,
        EStrategyType::Assault,
        EStrategyType::Assault,
        EStrategyType::Support
    };
    TightAssault.Description = TEXT("3 Assault + 1 Support, aggressive push");
    TightAssault.EstimatedValue = 0.55f;
    BatchPrototypes.Add(TightAssault);

    // Prototype 2: Wide Defense (2 Defend, 2 Support)
    FBatchPrototype WideDefense;
    WideDefense.Name = TEXT("WideDefense");
    WideDefense.Strategies = {
        EStrategyType::Defend,
        EStrategyType::Defend,
        EStrategyType::Support,
        EStrategyType::Support
    };
    WideDefense.Description = TEXT("2 Defend + 2 Support, defensive formation");
    WideDefense.EstimatedValue = 0.52f;
    BatchPrototypes.Add(WideDefense);

    // Prototype 3: Balanced (A, D, S, R - one each)
    FBatchPrototype Balanced;
    Balanced.Name = TEXT("Balanced");
    Balanced.Strategies = {
        EStrategyType::Assault,
        EStrategyType::Defend,
        EStrategyType::Support,
        EStrategyType::Retreat
    };
    Balanced.Description = TEXT("One of each strategy, adaptable");
    Balanced.EstimatedValue = 0.50f;
    BatchPrototypes.Add(Balanced);

    // Prototype 4: Support Focus (2 Assault, 2 Support)
    FBatchPrototype SupportFocus;
    SupportFocus.Name = TEXT("SupportFocus");
    SupportFocus.Strategies = {
        EStrategyType::Assault,
        EStrategyType::Assault,
        EStrategyType::Support,
        EStrategyType::Support
    };
    SupportFocus.Description = TEXT("2 Assault + 2 Support, coordinated offense");
    SupportFocus.EstimatedValue = 0.54f;
    BatchPrototypes.Add(SupportFocus);

    // Prototype 5: Defense Focus (3 Defend, 1 Support)
    FBatchPrototype DefenseFocus;
    DefenseFocus.Name = TEXT("DefenseFocus");
    DefenseFocus.Strategies = {
        EStrategyType::Defend,
        EStrategyType::Defend,
        EStrategyType::Defend,
        EStrategyType::Support
    };
    DefenseFocus.Description = TEXT("3 Defend + 1 Support, fortified position");
    DefenseFocus.EstimatedValue = 0.48f;
    BatchPrototypes.Add(DefenseFocus);

    // Prototype 6: Offensive Swarm (all Assault)
    FBatchPrototype OffensiveSwarm;
    OffensiveSwarm.Name = TEXT("OffensiveSwarm");
    OffensiveSwarm.Strategies = {
        EStrategyType::Assault,
        EStrategyType::Assault,
        EStrategyType::Assault,
        EStrategyType::Assault
    };
    OffensiveSwarm.Description = TEXT("All Assault, maximum aggression");
    OffensiveSwarm.EstimatedValue = 0.50f;
    BatchPrototypes.Add(OffensiveSwarm);

    // Prototype 7: Defensive Wall (all Defend)
    FBatchPrototype DefensiveWall;
    DefensiveWall.Name = TEXT("DefensiveWall");
    DefensiveWall.Strategies = {
        EStrategyType::Defend,
        EStrategyType::Defend,
        EStrategyType::Defend,
        EStrategyType::Defend
    };
    DefensiveWall.Description = TEXT("All Defend, turtle strategy");
    DefensiveWall.EstimatedValue = 0.45f;
    BatchPrototypes.Add(DefensiveWall);

    // Prototype 8: Mixed Objectives (2→Friendly, 2→Hostile)
    FBatchPrototype MixedObjectives;
    MixedObjectives.Name = TEXT("MixedObjectives");
    MixedObjectives.Strategies = {
        EStrategyType::Assault,
        EStrategyType::Assault,
        EStrategyType::Defend,
        EStrategyType::Defend
    };
    MixedObjectives.Description = TEXT("2 Assault + 2 Defend, split focus");
    MixedObjectives.EstimatedValue = 0.50f;
    BatchPrototypes.Add(MixedObjectives);

    UE_LOG(LogTemp, Log, TEXT("[MCTS v8.20] Initialized %d batch prototypes"), BatchPrototypes.Num());

    // v8.20: Initialize RLPolicyNetwork for value estimates
    RLPolicyNetwork = NewObject<URLPolicyNetwork>(this);

    // Try to load batch cache from previous runs (warm start)
    FString CachePath = FPaths::ProjectSavedDir() + TEXT("MCTS/BatchCache.json");
    if (FPaths::FileExists(*CachePath))
    {
        LoadBatchCache(CachePath);
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.20] Loaded batch cache with %d entries"), BatchCache.Num());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.20] No existing batch cache, starting with fresh priors"));
    }
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
    UE_LOG(LogTemp, Warning, TEXT("[MCTS DEBUG] Input Agents: %d"), Agents.Num());
    UE_LOG(LogTemp, Warning, TEXT("[MCTS DEBUG] Input Objectives: %d"), Objectives.Num());
    UE_LOG(LogTemp, Warning, TEXT("[MCTS DEBUG] Cached Observations: %d"), InCachedObservations.Num());

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

    // v8.10 FIX: Traverse down to find a complete assignment (all agents assigned)
    // The tree builds incrementally: root (0 agents) → level 1 (1 agent) → ... → level N (N agents)
    // We need to follow the best path down to get a complete assignment

    TSharedPtr<FTeamMCTSNode> CurrentNode = TeamRootNode;
    TSharedPtr<FTeamMCTSNode> BestLeafNode = nullptr;
    int32 TraversalDepth = 0;

    // Follow best child path until we reach a node with all agents assigned or a terminal node
    while (CurrentNode.IsValid())
    {
        TMap<TObjectPtr<AActor>, FStrategyAssignment> CurrentAssignments = CurrentNode->GetStrategyAssignments();

        // Check if all agents are assigned
        if (CurrentAssignments.Num() >= Agents.Num())
        {
            BestLeafNode = CurrentNode;
            UE_LOG(LogTemp, Display, TEXT("[MCTS v8.10 FIX] Found complete assignment at depth %d: %d agents assigned"),
                TraversalDepth, CurrentAssignments.Num());
            break;
        }

        // Select best child (highest visit count for robust decision)
        TSharedPtr<FTeamMCTSNode> BestChild = CurrentNode->SelectBestChild(0.0f);  // ExplorationParam=0 for pure exploitation

        if (!BestChild.IsValid())
        {
            // No children - use current node if it has some assignments
            if (CurrentAssignments.Num() > 0)
            {
                BestLeafNode = CurrentNode;
                UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.10 FIX] Reached leaf at depth %d with partial assignment: %d/%d agents"),
                    TraversalDepth, CurrentAssignments.Num(), Agents.Num());
            }
            break;
        }

        CurrentNode = BestChild;
        TraversalDepth++;

        // Safety: Prevent infinite loop
        if (TraversalDepth > 10)
        {
            UE_LOG(LogTemp, Error, TEXT("[MCTS v8.10 FIX] Traversal depth exceeded 10, breaking"));
            break;
        }
    }

    if (BestLeafNode.IsValid())
    {
        // Convert TObjectPtr keys to AActor* keys
        TMap<TObjectPtr<AActor>, FStrategyAssignment> BestAssignmentsTObject = BestLeafNode->GetStrategyAssignments();

        UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.10 FIX] Best assignment found: Value=%.2f, Visits=%d, Agents=%d"),
            BestLeafNode->TotalReward / FMath::Max(1, BestLeafNode->VisitCount),
            BestLeafNode->VisitCount,
            BestAssignmentsTObject.Num());

        // Convert to return format
        TMap<AActor*, FStrategyAssignment> ResultAssignments;
        ResultAssignments.Reserve(BestAssignmentsTObject.Num());

        for (auto& [AgentPtr, Assignment] : BestAssignmentsTObject)
        {
            // Add metrics
            Assignment.ExpectedValue = BestLeafNode->TotalReward / FMath::Max(1, BestLeafNode->VisitCount);
            Assignment.VisitCount = BestLeafNode->VisitCount;
            Assignment.Timestamp = FPlatformTime::Seconds();

            ResultAssignments.Add(AgentPtr, Assignment);
        }

        UE_LOG(LogTemp, Warning, TEXT("[MCTS DEBUG] Output Assignments: %d"), ResultAssignments.Num());

        return ResultAssignments;
    }

    // Fallback: Return empty assignments
    UE_LOG(LogTemp, Error, TEXT("[MCTS v8.10 FIX] No valid assignment found after %d simulations"), Simulations);
    return TMap<AActor*, FStrategyAssignment>();
}

TArray<TMap<AActor*, FStrategyAssignment>> UMCTS::GenerateCompleteBatches(
    const TArray<AActor*>& Agents,
    const TArray<AObjectiveActor*>& Objectives)
{
    // [v8.21] 생존 에이전트가 없으면 빈 배열 반환
    if (Agents.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.21] No agents available for strategy assignment."));
        return TArray<TMap<AActor*, FStrategyAssignment>>();
    }

    // [v8.21] 목표가 부족한 경우에 대한 예외 처리 (최소 1개는 있어야 함)
    if (Objectives.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[MCTS v8.21] No objectives available!"));
        return TArray<TMap<AActor*, FStrategyAssignment>>();
    }

    TArray<TMap<AActor*, FStrategyAssignment>> AllBatches;
    AllBatches.Reserve(BatchPrototypes.Num());

    // [v8.21] 전략 일관성을 위해 에이전트를 이름(혹은 ID) 순으로 정렬
    // 이렇게 해야 "첫 번째 전략"이 항상 "특정 에이전트(리더급)"에게 할당됨
    TArray<AActor*> SortedAgents = Agents;
    SortedAgents.Sort([](const AActor& A, const AActor& B) {
        return A.GetName() < B.GetName();
        });

    for (const auto& Prototype : BatchPrototypes)
    {
        TMap<AActor*, FStrategyAssignment> Batch;

        // [v8.21 핵심] 루프 횟수를 '생존 에이전트 수'와 '프로토타입 전략 수' 중 작은 쪽으로 제한
        // 예: 생존자 2명이면, 프로토타입의 앞쪽 전략 2개만 가져옴
        int32 AssignCount = FMath::Min(SortedAgents.Num(), Prototype.Strategies.Num());

        for (int32 i = 0; i < AssignCount; ++i)
        {
            AActor* Agent = SortedAgents[i];
            EStrategyType Strategy = Prototype.Strategies[i];

            // v9.0: Strategy-only assignment (no explicit objective)
            // Objective selection is now implicit in reward functions
            FStrategyAssignment Assignment;
            Assignment.Agent = Agent;
            Assignment.Strategy = Strategy;
            Assignment.Priority = 5;
            Assignment.Timestamp = FPlatformTime::Seconds();

            Batch.Add(Agent, Assignment);
        }

        // [v8.21] 유효성 검사: 생성된 배치의 크기가 생존 에이전트 수와 같은지 확인
        if (Batch.Num() == SortedAgents.Num())
        {
            AllBatches.Add(Batch);
        }
        else
        {
            // 이론상 발생하면 안 되지만, 디버그용 로그
            UE_LOG(LogTemp, Error, TEXT("[MCTS v8.21] Batch generation mismatch! Agents: %d, Batch: %d"),
                SortedAgents.Num(), Batch.Num());
        }
    }

    return AllBatches;
}

TMap<AActor*, FStrategyAssignment> UMCTS::SelectBatchByUCB1(
    const TArray<TMap<AActor*, FStrategyAssignment>>& AllBatches)
{
    if (AllBatches.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[MCTS v8.20] SelectBatchByUCB1 received empty batch list"));
        return TMap<AActor*, FStrategyAssignment>();
    }

    float BestUCB = -FLT_MAX;
    int32 BestBatchIdx = 0;

    UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.20] UCB1 Batch Selection:"));

    for (int32 i = 0; i < AllBatches.Num(); ++i)
    {
        const auto& Batch = AllBatches[i];
        FString BatchKey = GetBatchKey(Batch);

        // ✅ BatchCache에서 가져오거나 기본값으로 FBatchPerformance 생성
        FBatchPerformance Performance;
        if (BatchCache.Contains(BatchKey))
        {
            Performance = BatchCache[BatchKey];
        }
        else
        {
            // 캐시에 없으면 기본값 (Trials=0, WinRate=0.5)
            Performance.BatchKey = BatchKey;
            Performance.Trials = 0;
            Performance.Wins = 0;
            Performance.AverageValue = 0.5f;
        }

        // ✅ GetUCBValue() 함수 호출 (FLT_MAX 처리 포함)
        float UCB = Performance.GetUCBValue(ExplorationParameter, TotalBatchTrials);
        float WinRate = Performance.GetWinRate();
        int32 Trials = Performance.Trials;

        // 로그용 Exploration 계산 (디버깅용)
        float Exploration = (Trials == 0) ? 0.0f :
            ExplorationParameter * FMath::Sqrt(FMath::Loge((float)TotalBatchTrials + 1) / (Trials + 1));

        // ✅ UCB가 FLT_MAX인 경우 특별 처리
        FString UCBStr;
        if (UCB >= FLT_MAX / 2)  // FLT_MAX 근사값 체크
        {
            UCBStr = TEXT("∞");
        }
        else
        {
            UCBStr = FString::Printf(TEXT("%.4f"), UCB);
        }

        UE_LOG(LogTemp, Warning,
            TEXT("  Batch %d: WR=%.2f (%d/%d), Exploration=%.2f, UCB=%s %s"),
            i, WinRate * 100.0f,
            Trials == 0 ? 0 : FMath::RoundToInt(WinRate * Trials), Trials,
            Exploration, *UCBStr,
            (UCB > BestUCB) ? TEXT("← BEST") : TEXT(""));

        if (UCB > BestUCB)
        {
            BestUCB = UCB;
            BestBatchIdx = i;
        }
    }

    // ✅ 로그 출력도 FLT_MAX 처리
    FString BestUCBStr = (BestUCB >= FLT_MAX / 2) ? TEXT("∞") : FString::Printf(TEXT("%.4f"), BestUCB);
    UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.20] Selected batch %d with UCB=%s"),
        BestBatchIdx, *BestUCBStr);

    return AllBatches[BestBatchIdx];
}

void UMCTS::UpdateBatchCache(
    const TMap<AActor*, FStrategyAssignment>& BatchAssignments,
    ETeamEpisodeResult Result) 
{
    FString BatchKey = GetBatchKey(BatchAssignments);

    if (!BatchCache.Contains(BatchKey))
    {
        BatchCache.Add(BatchKey, FBatchPerformance());
        BatchCache[BatchKey].BatchKey = BatchKey;
    }

    auto& CachedBatch = BatchCache[BatchKey];
    CachedBatch.Trials++;
    CachedBatch.LastUsedTime = FPlatformTime::Seconds();

    // 승/패/무승부 처리
    switch (Result)
    {
    case ETeamEpisodeResult::Win:
        CachedBatch.Wins++;
        TotalBatchWins++; // 전역 승리 카운트 증가
        break;

    case ETeamEpisodeResult::Loss:
        CachedBatch.Losses++; // 구조체에 Losses 필드가 있다고 가정
        break;

    case ETeamEpisodeResult::Draw:
        CachedBatch.Draws++; // 구조체에 Draws 필드가 있다고 가정
        // 무승부는 승률 계산 시 0.5승으로 칠지, 제외할지 결정 필요
        // 현재 로직상 Wins/Trials 이므로 무승부는 승률을 낮추는 요인이 됨 (보수적 접근)
        break;
    }

    // 평균 가치 재계산 (Win Rate Update)
    CachedBatch.AverageValue = CachedBatch.GetWinRate();
    TotalBatchTrials++;
}

FString UMCTS::GetBatchKey(const TMap<AActor*, FStrategyAssignment>& BatchAssignments) const
{
    TArray<AActor*> SortedAgents;
    BatchAssignments.GetKeys(SortedAgents);

    // Sort for consistent key generation
    SortedAgents.Sort([](const AActor& A, const AActor& B) {
        return A.GetName() < B.GetName();
        });

    FString Key;
    for (const auto& Agent : SortedAgents)
    {
        if (const auto* Assignment = BatchAssignments.Find(Agent))
        {
            if (!Key.IsEmpty())
            {
                Key += TEXT(",");
            }

            FString StrategyStr = UEnum::GetValueAsString(Assignment->Strategy);

            // v9.0: Simplified key format (strategy-only, no objective)
            Key += FString::Printf(TEXT("%s→%s"),
                *Agent->GetName(),
                *StrategyStr);
        }
    }

    return Key;
}

bool UMCTS::SaveBatchCache(const FString& SavePath)
{
    // Create JSON structure
    TSharedPtr<FJsonObject> RootJson = MakeShared<FJsonObject>();
    RootJson->SetNumberField(TEXT("Version"), 1);
    RootJson->SetNumberField(TEXT("Timestamp"), FPlatformTime::Seconds());
    RootJson->SetNumberField(TEXT("TotalTrials"), TotalBatchTrials);
    RootJson->SetNumberField(TEXT("TotalWins"), TotalBatchWins);

    TSharedPtr<FJsonObject> BatchesJson = MakeShared<FJsonObject>();

    // Add each batch entry
    for (const auto& [Key, Value] : BatchCache)
    {
        TSharedPtr<FJsonObject> BatchJson = MakeShared<FJsonObject>();
        BatchJson->SetNumberField(TEXT("Wins"), Value.Wins);
        BatchJson->SetNumberField(TEXT("Trials"), Value.Trials);
        BatchJson->SetNumberField(TEXT("AverageValue"), Value.AverageValue);
        BatchJson->SetNumberField(TEXT("LastUsedTime"), Value.LastUsedTime);

        BatchesJson->SetObjectField(Key, BatchJson);
    }

    RootJson->SetObjectField(TEXT("Batches"), BatchesJson);

    // Write to file
    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(RootJson.ToSharedRef(), Writer);

    // Ensure directory exists
    FString Directory = FPaths::GetPath(SavePath);
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*Directory))
    {
        PlatformFile.CreateDirectoryTree(*Directory);
    }

    // Save file
    bool bSuccess = FFileHelper::SaveStringToFile(JsonString, *SavePath);

    if (bSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.20] Saved batch cache to %s (%d batches)"),
            *SavePath, BatchCache.Num());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[MCTS v8.20] Failed to save batch cache to %s"), *SavePath);
    }

    return bSuccess;
}

bool UMCTS::LoadBatchCache(const FString& LoadPath)
{
    // Read file
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *LoadPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.20] Batch cache file not found: %s"), *LoadPath);
        return false;
    }

    // Parse JSON
    TSharedPtr<FJsonObject> RootJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, RootJson) || !RootJson.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[MCTS v8.20] Failed to parse batch cache JSON"));
        return false;
    }

    // Load metadata
    TotalBatchTrials = RootJson->GetIntegerField(TEXT("TotalTrials"));
    TotalBatchWins = RootJson->GetIntegerField(TEXT("TotalWins"));

    // Load batches
    TSharedPtr<FJsonObject> BatchesJson = RootJson->GetObjectField(TEXT("Batches"));

    for (const auto& [Key, Value] : BatchesJson->Values)
    {
        TSharedPtr<FJsonObject> BatchJson = Value->AsObject();

        FBatchPerformance Performance;
        Performance.BatchKey = Key;
        Performance.Wins = BatchJson->GetIntegerField(TEXT("Wins"));
        Performance.Trials = BatchJson->GetIntegerField(TEXT("Trials"));
        Performance.AverageValue = BatchJson->GetNumberField(TEXT("AverageValue"));
        Performance.LastUsedTime = BatchJson->GetNumberField(TEXT("LastUsedTime"));

        BatchCache.Add(Key, Performance);
    }

    UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.20] Loaded batch cache: %d batches, %d total trials"),
        BatchCache.Num(), TotalBatchTrials);

    return true;
}

void UMCTS::LogBatchPerformance()
{
    if (BatchCache.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.20] No batch performance data"));
        return;
    }

    // [Fix] TMap -> TArray 변환 로직 수정
    TArray<TPair<FString, FBatchPerformance>> SortedBatches;
    SortedBatches.Reserve(BatchCache.Num());

    for (const auto& Pair : BatchCache)
    {
        SortedBatches.Add(Pair);
    }

    // Sort by win rate (Desc)
    SortedBatches.Sort([](const TPair<FString, FBatchPerformance>& A, const TPair<FString, FBatchPerformance>& B) {
        return A.Value.GetWinRate() > B.Value.GetWinRate();
        });

    UE_LOG(LogTemp, Warning, TEXT("========== BATCH PERFORMANCE REPORT =========="));
    UE_LOG(LogTemp, Warning, TEXT("Total Trials: %d, Total Wins: %d (%.1f%%)"),
        TotalBatchTrials, TotalBatchWins,
        TotalBatchTrials > 0 ? 100.0f * TotalBatchWins / TotalBatchTrials : 0.0f);
    UE_LOG(LogTemp, Warning, TEXT(""));

    for (int32 i = 0; i < FMath::Min(5, SortedBatches.Num()); ++i)
    {
        const auto& Pair = SortedBatches[i];
        const FString& Key = Pair.Key;
        const FBatchPerformance& Value = Pair.Value;

        UE_LOG(LogTemp, Warning, TEXT("  %d. %s"), i + 1, *Key);
        UE_LOG(LogTemp, Warning, TEXT("     Wins: %d/%d (%.1f%%)"),
            Value.Wins, Value.Trials, 100.0f * Value.GetWinRate());
        UE_LOG(LogTemp, Warning, TEXT("     UCB: %.4f"),
            Value.GetUCBValue(1.41f, TotalBatchTrials));
    }

    UE_LOG(LogTemp, Warning, TEXT("============================================="));
}

TMap<AActor*, FStrategyAssignment> UMCTS::RunStrategyAssignment_v820(const TArray<AActor*>& Agents, const TArray<AObjectiveActor*>& Objectives, int32 Simulations, const TMap<AActor*, FObservationElement>& InCachedObservations)
{
    UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.20] START: Agents=%d, Objectives=%d, Simulations=%d"),
        Agents.Num(), Objectives.Num(), Simulations);

    float StartTime = FPlatformTime::Seconds();

    // Cache inputs for thread-safe async execution
    AvailableAgents = Agents;
    AvailableObjectives = Objectives;
    CachedObservations = InCachedObservations;

    // [Phase 1] Generate all complete batches
    TArray<TMap<AActor*, FStrategyAssignment>> AllBatches =
        GenerateCompleteBatches(Agents, Objectives);

    if (AllBatches.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[MCTS v8.20] Failed to generate batches"));
        return TMap<AActor*, FStrategyAssignment>();
    }

    // [Phase 2] Select best batch using UCB1
    TMap<AActor*, FStrategyAssignment> SelectedBatch =
        SelectBatchByUCB1(AllBatches);

    // [Phase 3] Optional: Refine batch with MCTS (currently skipped)
    // TMap<AActor*, FStrategyAssignment> RefinedBatch = 
    //     RefineStrategyAssignmentWithin(SelectedBatch, Simulations);

    // [Phase 4] Validate output
    if (SelectedBatch.Num() != Agents.Num())
    {
        UE_LOG(LogTemp, Error,
            TEXT("[MCTS v8.21] Output batch incomplete: Input Agents=%d, Assigned=%d"),
            Agents.Num(), SelectedBatch.Num());
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[MCTS v8.20] Output batch VALID: 4 agents assigned"));
    }

    return SelectedBatch;
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

    // Query RL value for each agent-strategy combination (v9.0: no objective assignment)
    for (const auto& [Agent, Assignment] : Assignments)
    {
        if (!Agent) continue;

        // v8.10 FIX: Use cached observation for thread safety
        const FObservationElement* CachedObs = CachedObservations.Find(Agent);
        if (!CachedObs)
        {
            UE_LOG(LogTemp, Warning, TEXT("[MCTS v8.10] No cached observation for agent %s, skipping"), *Agent->GetName());
            continue;
        }

        // v8.10 FIX: Query RL value estimate directly from cached observation
        // The cached observation already contains all necessary tactical context
        // (enemy positions, cover, allies, etc.) - no need to add objective context
        float StateValue = RLPolicyNetwork->GetStateValueV8(*CachedObs, Assignment.Strategy);

        TotalValue += StateValue;
        AgentCount++;

        UE_LOG(LogTemp, VeryVerbose, TEXT("[MCTS v8.10 FIX] Agent '%s' Strategy '%s' → Value: %.3f"),
            *Agent->GetName(),
            *UEnum::GetValueAsString(Assignment.Strategy),
            StateValue);
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
    UE_LOG(LogTemp, Warning, TEXT("[GEN DEBUG] CurrentAssignments input: %d agents"), CurrentAssignments.Num());

    TArray<TMap<AActor*, FStrategyAssignment>> PossibleAssignments;

    // v8.0: Generate strategy assignments for each agent × strategy × objective combination
    // Action space: 4 agents × 4 strategies × 2 objectives = simpler than v7.0

    for (AActor* Agent : AvailableAgents)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[GEN DEBUG] Checking Agent %s: Contains=%s"),
            *Agent->GetName(),
            CurrentAssignments.Contains(Agent) ? TEXT("YES (SKIP)") : TEXT("NO (PROCESS)"));

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

                // Build strategy assignment (v9.0: no objective assignment)
                FStrategyAssignment Assignment;
                Assignment.Agent = Agent;
                Assignment.Strategy = Strategy;
                // v9.0: TargetObjective removed - objectives implicit in strategy rewards
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

    UE_LOG(LogTemp, Warning, TEXT("[GEN DEBUG] Generated actions: %d"), PossibleAssignments.Num());

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
    // v9.0: Objectives no longer explicitly assigned - coverage is implicit in strategy rewards
    // Return neutral score since this heuristic no longer applies
    return 0.5f;
}

float UMCTS::StrategySynergyScore(const TMap<AActor*, FStrategyAssignment>& Assignments) const
{
    // v9.0: Strategy synergy based on composition (no objective assignment)
    // Examples:
    // - Assault + Support = good (synergy)
    // - All Retreat = bad (no cohesion)
    // - Assault + Assault = good (focus fire)

    if (Assignments.Num() == 0)
    {
        return 0.5f;
    }

    float SynergyScore = 0.0f;
    int32 SynergyCount = 0;

    // Check pairwise strategy synergies (v9.0: no objective checks)
    TArray<AActor*> Agents;
    Assignments.GetKeys(Agents);

    for (int32 i = 0; i < Agents.Num() - 1; ++i)
    {
        for (int32 j = i + 1; j < Agents.Num(); ++j)
        {
            const FStrategyAssignment& A1 = Assignments[Agents[i]];
            const FStrategyAssignment& A2 = Assignments[Agents[j]];

            // v9.0: Compatible strategies = synergy (objectives implicit in rewards)
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
            // Defend + Support = good (defensive formation)
            else if ((A1.Strategy == EStrategyType::Defend && A2.Strategy == EStrategyType::Support) ||
                     (A1.Strategy == EStrategyType::Support && A2.Strategy == EStrategyType::Defend))
            {
                SynergyScore += 0.9f;
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

    return SynergyCount > 0 ? SynergyScore / SynergyCount : 0.5f;
}
