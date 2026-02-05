#include "AI/MCTS/MCTS.h"
#include "Kismet/KismetMathLibrary.h"
#include "Team/TeamTypes.h"
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
    UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0] START: Agents=%d, Objectives=%d, Simulations=%d"),
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
        UE_LOG(LogTemp, Error, TEXT("[MCTS v9.0] Failed to generate batches"));
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
            TEXT("[MCTS v9.0] Output batch VALID: 4 agents assigned"));
    }

    return SelectedBatch;
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
        UE_LOG(LogTemp, Error, TEXT("[MCTS v9.0] SelectBatchByUCB1 received empty batch list"));
        return TMap<AActor*, FStrategyAssignment>();
    }

    // ✅ v9.0 FIX: Epsilon-greedy exploration (20% random selection)
    const float EpsilonExploration = 0.20f;
    if (FMath::FRand() < EpsilonExploration)
    {
        int32 RandomIdx = FMath::RandRange(0, AllBatches.Num() - 1);
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0] 🎲 EPSILON-GREEDY: Randomly selected batch %d (exploration)"), RandomIdx);
        return AllBatches[RandomIdx];
    }

    float BestUCB = -FLT_MAX;
    TArray<int32> BestBatchIndices;  // v9.0: Track all batches with best UCB (for tie-breaking)

    UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0] UCB1 Batch Selection (TotalTrials=%d, CacheSize=%d):"),
        TotalBatchTrials, BatchCache.Num());

    for (int32 i = 0; i < AllBatches.Num(); ++i)
    {
        const auto& Batch = AllBatches[i];
        FString BatchKey = GetBatchKey(Batch);

        // ✅ BatchCache에서 가져오거나 기본값으로 FBatchPerformance 생성
        FBatchPerformance Performance;
        bool bCacheHit = BatchCache.Contains(BatchKey);
        if (bCacheHit)
        {
            Performance = BatchCache[BatchKey];
            UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0]  [CACHE HIT] Batch %d: Key='%s', Trials=%d"),
                i, *BatchKey, Performance.Trials);
        }
        else
        {
            // 캐시에 없으면 기본값 (Trials=0, WinRate=0.5)
            Performance.BatchKey = BatchKey;
            Performance.Trials = 0;
            Performance.Wins = 0;
            Performance.AverageValue = 0.5f;
            UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0]  [CACHE MISS] Batch %d: Key='%s' (new entry)"),
                i, *BatchKey);
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
            TEXT("  Batch %d (%s): WR=%.2f (%d/%d), Exploration=%.2f, UCB=%s %s"),
            i, *BatchKey,
            WinRate * 100.0f,
            Trials == 0 ? 0 : FMath::RoundToInt(WinRate * Trials), Trials,
            Exploration, *UCBStr,
            (UCB > BestUCB) ? TEXT("← NEW BEST") : (FMath::IsNearlyEqual(UCB, BestUCB, 0.0001f) ? TEXT("← TIE") : TEXT("")));

        // v9.0: Track ties for randomized tie-breaking
        if (UCB > BestUCB)
        {
            BestUCB = UCB;
            BestBatchIndices.Empty();
            BestBatchIndices.Add(i);
        }
        else if (FMath::IsNearlyEqual(UCB, BestUCB, 0.0001f))
        {
            BestBatchIndices.Add(i);
        }
    }

    // ✅ v9.0 FIX: Randomized tie-breaking
    int32 BestBatchIdx = 0;
    if (BestBatchIndices.Num() > 1)
    {
        BestBatchIdx = BestBatchIndices[FMath::RandRange(0, BestBatchIndices.Num() - 1)];
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0] 🎲 TIE-BREAKING: %d batches tied, randomly selected batch %d"),
            BestBatchIndices.Num(), BestBatchIdx);
    }
    else
    {
        BestBatchIdx = BestBatchIndices[0];
    }

    // ✅ 로그 출력도 FLT_MAX 처리
    FString BestUCBStr = (BestUCB >= FLT_MAX / 2) ? TEXT("∞") : FString::Printf(TEXT("%.4f"), BestUCB);
    UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0] ✅ Selected batch %d with UCB=%s"),
        BestBatchIdx, *BestUCBStr);

    return AllBatches[BestBatchIdx];
}

void UMCTS::UpdateBatchCache(
    const TMap<AActor*, FStrategyAssignment>& BatchAssignments,
    ETeamEpisodeResult Result)
{
    FString BatchKey = GetBatchKey(BatchAssignments);

    UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0] [MCTS CACHE UPDATE] Key='%s', Result=%d, CacheSize=%d (before)"),
        *BatchKey, static_cast<int32>(Result), BatchCache.Num());

    if (!BatchCache.Contains(BatchKey))
    {
        BatchCache.Add(BatchKey, FBatchPerformance());
        BatchCache[BatchKey].BatchKey = BatchKey;
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0]  [NEW ENTRY] Added to cache"));
    }

    auto& CachedBatch = BatchCache[BatchKey];
    int32 OldTrials = CachedBatch.Trials;
    CachedBatch.Trials++;
    CachedBatch.LastUsedTime = FPlatformTime::Seconds();

    UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0]  [UPDATED] Trials: %d → %d, TotalTrials: %d → %d"),
        OldTrials, CachedBatch.Trials, TotalBatchTrials, TotalBatchTrials + 1);

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
    // v9.0 FIX: Strategy-pattern-only key (agent-agnostic)
    // This ensures the same strategy composition always maps to the same cache entry,
    // regardless of which specific agents are assigned or if agents are recreated between episodes.

    TArray<EStrategyType> Strategies;
    Strategies.Reserve(BatchAssignments.Num());

    // Extract strategies
    for (const auto& [Agent, Assignment] : BatchAssignments)
    {
        Strategies.Add(Assignment.Strategy);
    }

    // Sort strategies for consistent key (independent of agent order)
    Strategies.Sort([](const EStrategyType& A, const EStrategyType& B) {
        return static_cast<int32>(A) < static_cast<int32>(B);
    });

    // Build key: "Assault,Assault,Assault,Support"
    FString Key;
    for (int32 i = 0; i < Strategies.Num(); ++i)
    {
        if (i > 0)
        {
            Key += TEXT(",");
        }

        FString StrategyStr = UEnum::GetValueAsString(Strategies[i]);
        // Remove "EStrategyType::" prefix if present
        StrategyStr.RemoveFromStart(TEXT("EStrategyType::"));
        Key += StrategyStr;
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
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0] Saved batch cache to %s (%d batches)"),
            *SavePath, BatchCache.Num());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[MCTS v9.0] Failed to save batch cache to %s"), *SavePath);
    }

    return bSuccess;
}

bool UMCTS::LoadBatchCache(const FString& LoadPath)
{
    // Read file
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *LoadPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0] Batch cache file not found: %s"), *LoadPath);
        return false;
    }

    // Parse JSON
    TSharedPtr<FJsonObject> RootJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, RootJson) || !RootJson.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[MCTS v9.0] Failed to parse batch cache JSON"));
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

    UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0] Loaded batch cache: %d batches, %d total trials"),
        BatchCache.Num(), TotalBatchTrials);

    return true;
}

void UMCTS::LogBatchPerformance()
{
    if (BatchCache.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0] No batch performance data"));
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
        float StateValue = RLPolicyNetwork->GetStateValue(*CachedObs, Assignment.Strategy);

        TotalValue += StateValue;
        AgentCount++;

        UE_LOG(LogTemp, VeryVerbose, TEXT("[MCTS v8.10 FIX] Agent '%s' Strategy '%s' → Value: %.3f"),
            *Agent->GetName(),
            *UEnum::GetValueAsString(Assignment.Strategy),
            StateValue);
    }

    // Normalize by agent count
    float AverageValue = AgentCount > 0 ? TotalValue / AgentCount : 0.0f;


    return FMath::Clamp(AverageValue, -1.0f, 1.0f);
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




