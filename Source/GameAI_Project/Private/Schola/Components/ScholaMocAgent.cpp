#include "ScholaMocAgent.h"

void UScholaMocAgent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Role 2: Inference Mode (MCTS Driven)
    // Training Mode일 때는 Schola(Trainer)가 Step()을 호출하므로 여기서 아무것도 안 함
    if (CurrentMode == EAgentMode::Inference)
    {
        // 1. 현재 상태 관측
        TArray<float> CurrentState = GetCurrentState();

        // 2. MCTS 계획 수립 (15ms 예산)
        FTacticalOption BestOption = MCTSAlgorithm->FindBestOption(CurrentState);

        // 3. 실행
        ExecuteOption(BestOption);

        // 4. (선택적) 추론 중에도 성능 평가를 위해 로깅 가능
    }
}

void UScholaMocAgent::ExecuteOption(const FTacticalOption& Option)
{
    // 로깅: 이전 프레임의 행동에 대한 결과(State_t+1, Reward)를 기록
    if (bHasLastAction && DataLogger)
    {
        TArray<float> CurrentState = GetCurrentState();
        FCompositeReward Reward = AgentRewardManager->GetDetailedReward(); 
        
        DataLogger->RecordTransition(LastState, LastOption, Reward, CurrentState, false);
    }

    // 실제 액터에게 명령 전달
    // 예: Blackboard 업데이트, Controller 함수 호출 등
    AFollowerCharacter* Pawn = Cast<AFollowerCharacter>(GetOwner());
    if (Pawn) {
        // Option의 Strategy와 Target을 Pawn의 시스템이 이해하는 명령으로 변환
        Pawn->ExecuteHighLevelCommand(Option.Strategy, Option.TargetObjective);
    }

    // 상태 갱신
    LastState = GetCurrentState();
    LastOption = Option;
    bHasLastAction = true;
}

void UScholaMocAgent::ExecuteGymAction(const TArray<int32>& GymActionVector)
{
    // Role 1: Training Mode (Data Collection)
    // Gym에서 온 [StrategyIdx, ObjectiveIdx] 정수 배열을 FTacticalOption으로 변환
    
    FTacticalOption NewOption;
    NewOption.Strategy = static_cast<EStrategyType>(GymActionVector[0]);
    
    // Objective ID 매핑 (별도 룩업 테이블 필요)
    NewOption.TargetObjective = ResolveObjectiveByID(GymActionVector[1]); 
    NewOption.PlannedDuration = 3.0f; // 학습 중에는 고정하거나 별도 Action으로 받음

    ExecuteOption(NewOption);
}

void UScholaMocAgent::ReloadModels(FString WorldModelPath, FString ValueNetPath)
{
    // Role 3: Model Hot-Load
    WorldModel->LoadFromONNX(WorldModelPath);
    ValueNetwork->LoadFromONNX(ValueNetPath);
    UE_LOG(LogTemp, Log, TEXT("[MOC] Models Hot-Loaded via Schola Interface"));
}