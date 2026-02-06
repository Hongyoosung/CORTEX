// File: Schola/Trainers/MocTrainer.cpp

#include "MocTrainer.h"
#include "ScholaMocAgent.h"
#include "Characters/MocCharacter.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "NavigationSystem.h"
#include "DrawDebugHelpers.h"

AMocTrainer::AMocTrainer()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.0167f;  // 60Hz
    
    CurrentEpisodeSteps = 0;
    EpisodeReward = 0.0f;
}

void AMocTrainer::Initialize(UScholaAgentComponent* InAgent)
{
    Super::Initialize(InAgent);
    
    MocAgent = Cast<UScholaMocAgent>(InAgent);
    if (!MocAgent)
    {
        UE_LOG(LogTemp, Error, TEXT("MocTrainer: Agent is not UScholaMocAgent!"));
        return;
    }
    
    // Character 참조 획득
    ControlledCharacter = Cast<AMocCharacter>(MocAgent->GetOwner());
    
    // Transition Logger 초기화
    if (bLogTransitions)
    {
        TransitionLogger = MakeShared<FTransitionLogger>(TransitionLogPath);
        UE_LOG(LogTemp, Log, TEXT("Transition logging enabled: %s"), *TransitionLogPath);
    }
    
    UE_LOG(LogTemp, Log, TEXT("MocTrainer initialized for agent: %s"), 
        *MocAgent->GetName());
}

void AMocTrainer::BeginPlay()
{
    Super::BeginPlay();
    
    // 초기 Option 샘플링
    SampleNewOption();
}

void AMocTrainer::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    CurrentEpisodeSteps++;
    
    // Option 전환 체크 (주기적 또는 Volatility 기반)
    if (CurrentEpisodeSteps % OptionSwitchInterval == 0)
    {
        FObservation NewObs = GatherStateObservation();
        float Volatility = CalculateVolatility(CurrentObservation, NewObs);
        
        if (Volatility > VolatilityThreshold)
        {
            UE_LOG(LogTemp, Warning, TEXT("High volatility detected: %.3f, switching option"), 
                Volatility);
            SampleNewOption();
        }
    }
}

// ==================== Schola Interface Implementation ====================

TArray<float> AMocTrainer::GetObservation()
{
    TArray<float> Observation;
    
    // 1. State (52-dim)
    CurrentObservation = GatherStateObservation();
    Observation.Append(CurrentObservation.ToArray());
    
    // 2. Option (1-dim - index)
    Observation.Add(static_cast<float>(CurrentOption));
    
    // 3. Target (3-dim)
    Observation.Add(CurrentTarget.X);
    Observation.Add(CurrentTarget.Y);
    Observation.Add(CurrentTarget.Z);
    
    // 4. Duration (1-dim)
    Observation.Add(CurrentOptionDuration);
    
    check(Observation.Num() == 57);  // 안전 체크
    
    return Observation;
}

void AMocTrainer::ApplyAction(const TArray<float>& ActionValues)
{
    if (!MocAgent || !ControlledCharacter) return;
    
    // ActionValues는 [8-dim] EQS Weights (이미 [-1, 1] 범위)
    check(ActionValues.Num() == 8);
    
    // FEQSWeightParameters 구조체로 변환
    FEQSWeightParameters Weights;
    Weights.EnemyObjectiveProximity = ActionValues[0];
    Weights.AllyObjectiveProximity  = ActionValues[1];
    Weights.CoverDensity            = ActionValues[2];
    Weights.EnemyVisibility         = ActionValues[3];
    Weights.AllyProximity           = ActionValues[4];
    Weights.CombatRange             = ActionValues[5];
    Weights.PickupProximity         = ActionValues[6];
    Weights.HeightAdvantage         = ActionValues[7];
    
    LastAction = Weights;
    
    // EQS 가중치를 Character의 이동 시스템에 적용
    ApplyEQSWeightsToCharacter(Weights);
}

float AMocTrainer::ComputeReward()
{
    if (!MocAgent) return 0.0f;
    
    // 현재 상태와 이전 상태 비교하여 보상 계산
    float StepReward = ComputeOptionReward(
        CurrentOption,
        PreviousObservation,
        CurrentObservation,
        LastAction
    );
    
    EpisodeReward += StepReward;
    
    // Transition 로깅 (World Model 학습용)
    if (bLogTransitions && TransitionLogger.IsValid())
    {
        LogTransition(
            PreviousObservation,
            CurrentOption,
            LastAction,
            StepReward,
            CurrentObservation,
            IsEpisodeDone()
        );
    }
    
    // 이전 상태 업데이트
    PreviousObservation = CurrentObservation;
    
    return StepReward;
}

bool AMocTrainer::IsEpisodeDone()
{
    // 종료 조건
    if (CurrentEpisodeSteps >= MaxEpisodeSteps)
    {
        UE_LOG(LogTemp, Log, TEXT("Episode ended: Max steps reached"));
        return true;
    }
    
    if (!ControlledCharacter || ControlledCharacter->GetHealth() <= 0.0f)
    {
        UE_LOG(LogTemp, Log, TEXT("Episode ended: Agent died"));
        return true;
    }
    
    // 게임 종료 조건 (승리/패배)
    AGameModeBase* GameMode = UGameplayStatics::GetGameMode(GetWorld());
    if (GameMode)
    {
        // 게임 모드별 종료 조건 체크
        // 예: 모든 거점 점령, 시간 초과 등
    }
    
    return false;
}

void AMocTrainer::ResetEpisode()
{
    UE_LOG(LogTemp, Log, TEXT("Resetting episode. Total reward: %.2f, Steps: %d"),
        EpisodeReward, CurrentEpisodeSteps);
    
    // 통계 초기화
    CurrentEpisodeSteps = 0;
    EpisodeReward = 0.0f;
    
    // Character 리셋
    if (ControlledCharacter)
    {
        ControlledCharacter->ResetToSpawnLocation();
        ControlledCharacter->SetHealth(100.0f);
    }
    
    // 새 Option 샘플링
    SampleNewOption();
    
    // 상태 초기화
    PreviousObservation = FObservation();
    CurrentObservation = GatherStateObservation();
}

// ==================== Helper Functions ====================

FObservation AMocTrainer::GatherStateObservation()
{
    FObservation Obs;
    
    if (!ControlledCharacter) return Obs;
    
    // 자신의 정보
    Obs.SelfPosition = ControlledCharacter->GetActorLocation();
    Obs.SelfRotation = ControlledCharacter->GetActorRotation();
    Obs.SelfHealth = ControlledCharacter->GetHealth();
    Obs.SelfAmmo = ControlledCharacter->GetCurrentAmmo();
    
    // 적 정보 (Perception 또는 간단한 거리 기반)
    TArray<AActor*> Enemies;
    UGameplayStatics::GetAllActorsOfClass(
        GetWorld(), 
        AMocCharacter::StaticClass(), 
        Enemies
    );
    
    int32 EnemyCount = 0;
    for (AActor* Actor : Enemies)
    {
        AMocCharacter* Enemy = Cast<AMocCharacter>(Actor);
        if (Enemy && Enemy->GetTeamID() != ControlledCharacter->GetTeamID())
        {
            if (EnemyCount < 3)  // 최대 3명
            {
                Obs.EnemyPositions[EnemyCount] = Enemy->GetActorLocation();
                Obs.EnemyHealths[EnemyCount] = Enemy->GetHealth();
                EnemyCount++;
            }
        }
    }
    Obs.NumVisibleEnemies = EnemyCount;
    
    // 거점 정보 (게임 모드에서 가져오기)
    // ... 프로젝트별 구현
    
    // 팀원 정보
    // ... 프로젝트별 구현
    
    return Obs;
}

void AMocTrainer::SampleNewOption()
{
    // Random Option Sampling (Phase 1 Training)
    int32 RandomIndex = FMath::RandRange(0, AvailableStrategies.Num() - 1);
    CurrentOption = AvailableStrategies[RandomIndex];
    
    // 전략에 맞는 Target 선택
    CurrentTarget = SelectTargetForOption(CurrentOption);
    
    // Duration 랜덤 설정 (5-20초)
    CurrentOptionDuration = FMath::FRandRange(5.0f, 20.0f);
    
    UE_LOG(LogTemp, Log, TEXT("Sampled Option: %s, Target: %s, Duration: %.1fs"),
        *UEnum::GetValueAsString(CurrentOption),
        *CurrentTarget.ToString(),
        CurrentOptionDuration);
}

FVector AMocTrainer::SelectTargetForOption(EStrategyType Strategy)
{
    // 전략에 따라 타겟 거점 선택
    switch (Strategy)
    {
    case EStrategyType::Attack:
        // 적 거점으로
        return GetNearestEnemyObjective();
        
    case EStrategyType::Defend:
        // 아군 거점으로
        return GetNearestAllyObjective();
        
    default:
        return ControlledCharacter->GetActorLocation();
    }
}

float AMocTrainer::CalculateVolatility(const FObservation& Prev, const FObservation& Current)
{
    // State 변화량 계산 (주요 feature만)
    float HealthDelta = FMath::Abs(Current.SelfHealth - Prev.SelfHealth);
    float PositionDelta = FVector::Dist(Current.SelfPosition, Prev.SelfPosition);
    float EnemyCountDelta = FMath::Abs(
        static_cast<float>(Current.NumVisibleEnemies - Prev.NumVisibleEnemies)
    );
    
    // 정규화 및 가중 합산
    float Volatility = 
        (HealthDelta / 100.0f) * 0.5f +        // 체력 변화 50%
        (PositionDelta / 1000.0f) * 0.2f +     // 위치 변화 20%
        (EnemyCountDelta / 3.0f) * 0.3f;       // 적 감지 변화 30%
    
    return FMath::Clamp(Volatility, 0.0f, 1.0f);
}

float AMocTrainer::ComputeOptionReward(
    EStrategyType Strategy,
    const FObservation& Prev,
    const FObservation& Current,
    const FEQSWeightParameters& Action
)
{
    float Reward = 0.0f;
    
    // Strategy-Specific Rewards (v10.1 설계)
    switch (Strategy)
    {
    case EStrategyType::Attack:
        {
            // 적 거점 접근
            float PrevDist = FVector::Dist(Prev.SelfPosition, CurrentTarget);
            float CurrDist = FVector::Dist(Current.SelfPosition, CurrentTarget);
            float DistanceReduction = (PrevDist - CurrDist) / 100.0f;
            Reward += 0.5f * DistanceReduction;
            
            // 킬/데미지
            if (Current.TotalKills > Prev.TotalKills)
            {
                Reward += 10.0f;  // +10 per kill
            }
            Reward += (Current.TotalDamageDealt - Prev.TotalDamageDealt) * 0.01f;
            
            // 자기 피해 페널티
            float SelfDamage = Prev.SelfHealth - Current.SelfHealth;
            if (SelfDamage > 0.0f)
            {
                Reward -= 2.0f * (SelfDamage / 100.0f);
            }
        }
        break;
        
    case EStrategyType::Defend:
        {
            // 아군 거점 근처 유지
            float DistToAllyObj = FVector::Dist(Current.SelfPosition, CurrentTarget);
            if (DistToAllyObj < 500.0f)  // 5m 이내
            {
                Reward += 5.0f * DeltaTime;  // 초당 +5
            }
            
            // 엄폐 상태 보상
            if (Current.bInCover)
            {
                Reward += 0.5f * DeltaTime;
            }
            
            // 체력 보존
            float HealthPreserved = (Current.SelfHealth / 100.0f);
            Reward += 3.0f * HealthPreserved * DeltaTime;
        }
        break;
    }
    
    // 공통 페널티: Time cost
    Reward -= 0.001f;  // 시간당 작은 페널티 (효율성 유도)
    
    return Reward;
}

void AMocTrainer::ApplyEQSWeightsToCharacter(const FEQSWeightParameters& Weights)
{
    if (!ControlledCharacter) return;
    
    // Training Mode: EQS를 직접 실행하여 이동 위치 결정
    UEnvQueryManager* EQSManager = UEnvQueryManager::GetCurrent(GetWorld());
    if (!EQSManager) return;
    
    // EQS Query 로드 (Blueprint 에셋)
    UEnvQuery* QueryTemplate = LoadObject<UEnvQuery>(
        nullptr, 
        TEXT("/Game/AI/EQS/EQ_TacticalMovement.EQ_TacticalMovement")
    );
    
    if (!QueryTemplate) return;
    
    // 가중치 적용하여 Query 생성
    FEnvQueryRequest QueryRequest(QueryTemplate, ControlledCharacter);
    
    // Named Parameters로 가중치 전달
    QueryRequest.SetFloatParam(TEXT("EnemyObjectiveWeight"), Weights.EnemyObjectiveProximity * 2.0f);
    QueryRequest.SetFloatParam(TEXT("AllyObjectiveWeight"), Weights.AllyObjectiveProximity * 2.0f);
    QueryRequest.SetFloatParam(TEXT("CoverDensityWeight"), Weights.CoverDensity * 2.0f);
    QueryRequest.SetFloatParam(TEXT("EnemyVisibilityWeight"), Weights.EnemyVisibility * 2.0f);
    QueryRequest.SetFloatParam(TEXT("AllyProximityWeight"), Weights.AllyProximity * 2.0f);
    QueryRequest.SetFloatParam(TEXT("CombatRangeWeight"), Weights.CombatRange * 2.0f);
    QueryRequest.SetFloatParam(TEXT("PickupWeight"), Weights.PickupProximity * 2.0f);
    QueryRequest.SetFloatParam(TEXT("HeightWeight"), Weights.HeightAdvantage * 2.0f);
    
    // 비동기 실행
    FQueryFinishedSignature Delegate;
    Delegate.BindLambda([this](TSharedPtr<FEnvQueryResult> Result)
    {
        if (Result->IsSuccessful() && ControlledCharacter)
        {
            FVector TargetLocation = Result->GetItemAsLocation(0);
            
            // Simple Move (Navigation System 사용)
            UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
            if (NavSys)
            {
                NavSys->SimpleMoveToLocation(
                    ControlledCharacter->GetController(), 
                    TargetLocation
                );
            }
        }
    });
    
    QueryRequest.Execute(EEnvQueryRunMode::SingleResult, Delegate);
}

void AMocTrainer::LogTransition(
    const FObservation& State,
    EStrategyType Option,
    const FEQSWeightParameters& Action,
    float Reward,
    const FObservation& NextState,
    bool bDone
)
{
    if (!TransitionLogger.IsValid()) return;
    
    // JSON 형태로 Transition 기록
    FTransitionData Transition;
    Transition.State = State.ToArray();
    Transition.Option = static_cast<int32>(Option);
    Transition.Action = Action.ToArray();
    Transition.Reward = Reward;
    Transition.NextState = NextState.ToArray();
    Transition.Done = bDone;
    Transition.Timestamp = FDateTime::Now().ToUnixTimestamp();
    
    TransitionLogger->AppendTransition(Transition);
}

void AMocTrainer::DrawTrainingDebug()
{
    if (!ControlledCharacter) return;
    
    // Current Option 표시
    FString DebugText = FString::Printf(
        TEXT("Option: %s\nSteps: %d\nReward: %.2f"),
        *UEnum::GetValueAsString(CurrentOption),
        CurrentEpisodeSteps,
        EpisodeReward
    );
    
    DrawDebugString(
        GetWorld(),
        ControlledCharacter->GetActorLocation() + FVector(0, 0, 200),
        DebugText,
        nullptr,
        FColor::Cyan,
        0.0f,
        true
    );
    
    // Target 시각화
    DrawDebugSphere(
        GetWorld(),
        CurrentTarget,
        100.0f,
        12,
        FColor::Yellow,
        false,
        0.1f
    );
}
