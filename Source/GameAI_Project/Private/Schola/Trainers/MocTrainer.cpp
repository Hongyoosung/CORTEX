// File: Schola/Trainers/MocTrainer.cpp

#include "Schola/Trainers/MocTrainer.h"
#include "Schola/Components/ScholaMocAgent.h"
#include "Schola/Logging/ScholaTransitionLogger.h"
#include "Schola/Logging/MocTransitionLogger.h"
#include "Characters/MocCharacter.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "NavigationSystem.h"
#include "DrawDebugHelpers.h"
#include "Types/RewardTypes.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Training/StateStructs/TrainerState.h"

AMocTrainer::AMocTrainer()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.0167f;  // 60Hz

    // Episode state
    CurrentEpisodeSteps = 0;
    EpisodeReward = 0.0f;

    // Training statistics
    TotalEpisodes = 0;
    CumulativeReward = 0.0f;
    AverageEpisodeLength = 0.0f;

    // Initialize pointers
    MocAgent = nullptr;
    ControlledCharacter = nullptr;
    TransitionLogger = nullptr;

    // Default strategy
    CachedCommandedStrategy = EStrategyType::Assault;
    LastEQSTargetLocation = FVector::ZeroVector;
}

void AMocTrainer::InitializeMocTrainer(UScholaMocAgent* InAgent)
{
    MocAgent = InAgent;
    if (!MocAgent)
    {
        UE_LOG(LogTemp, Error, TEXT("[MocTrainer] Invalid agent reference! Initialization failed."));
        return;
    }

    // Character 참조 획득
    ControlledCharacter = Cast<AMocCharacter>(MocAgent->GetOwner());
    if (!ControlledCharacter)
    {
        UE_LOG(LogTemp, Error, TEXT("[MocTrainer] Agent owner is not AMocCharacter! Initialization failed."));
        return;
    }

    // EQS Query Template 검증
    if (!EQSQueryTemplate)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] EQS Query Template not set! EQS execution will be disabled."));
    }

    // Transition Logger 초기화
    if (bLogTransitions)
    {
        TransitionLogger = NewObject<UScholaTransitionLogger>(this);
        if (TransitionLogger)
        {
            TransitionLogger->Initialize(TransitionLogPath);
            UE_LOG(LogTemp, Log, TEXT("[MocTrainer] Transition logging enabled: %s"), *TransitionLogPath);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[MocTrainer] Failed to create TransitionLogger!"));
        }
    }

    // 초기 commanded strategy 캐시
    CachedCommandedStrategy = ControlledCharacter->GetCommandedStrategy();

    UE_LOG(LogTemp, Log, TEXT("[MocTrainer] v10.2 Executor initialized for agent: %s (Strategy: %s)"),
        *ControlledCharacter->GetName(),
        *UEnum::GetValueAsString(CachedCommandedStrategy));
}

void AMocTrainer::BeginPlay()
{
    Super::BeginPlay();

    // v10.2: Strategy는 Squad Commander가 할당
    // Executor는 commanded strategy를 수행하는 데만 집중

    // Initialize observation state
    PreviousObservation = FObservation();
    CurrentObservation = GatherStateObservation();
}

void AMocTrainer::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!ControlledCharacter || !ControlledCharacter->IsAlive())
    {
        return; // Skip tick if character is invalid or dead
    }

    CurrentEpisodeSteps++;

    // v10.2: Update commanded strategy cache (may change from Squad Commander)
    EStrategyType NewStrategy = ControlledCharacter->GetCommandedStrategy();
    if (NewStrategy != CachedCommandedStrategy)
    {
        UE_LOG(LogTemp, Log, TEXT("[MocTrainer] Strategy changed: %s -> %s"),
            *UEnum::GetValueAsString(CachedCommandedStrategy),
            *UEnum::GetValueAsString(NewStrategy));
        CachedCommandedStrategy = NewStrategy;
    }

    // Debug visualization
    if (bEnableDebugVisualization)
    {
        DrawTrainingDebug();
    }
}

// ==================== Schola Interface Implementation ====================

TArray<float> AMocTrainer::GetObservation()
{
    // v10.2: 52-dim agent state only
    // Commanded strategy is already set in Character by Squad Commander
    CurrentObservation = GatherStateObservation();

    // Observation 유효성 검사
    if (!ValidateObservation(CurrentObservation))
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] Observation validation failed, returning zeroed observation"));
        return TArray<float>();
    }

    TArray<float> Observation = CurrentObservation.ToArray();

    // 차원 검증
    if (Observation.Num() != 52)
    {
        UE_LOG(LogTemp, Error, TEXT("[MocTrainer] Observation dimension mismatch: %d (expected 52)"),
            Observation.Num());
    }

    return Observation;
}

void AMocTrainer::ApplyAction(const TArray<float>& ActionValues)
{
    if (!MocAgent || !ControlledCharacter)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] Cannot apply action - invalid agent or character"));
        return;
    }

    // ActionValues 검증
    if (ActionValues.Num() != 8)
    {
        UE_LOG(LogTemp, Error, TEXT("[MocTrainer] Invalid action size: %d (expected 8)"), ActionValues.Num());
        return;
    }

    // FEQSWeightParameters 구조체로 변환 및 클램핑
    FEQSWeightParameters Weights;
    Weights.EnemyObjectiveProximity = FMath::Clamp(ActionValues[0], -1.0f, 1.0f);
    Weights.AllyObjectiveProximity  = FMath::Clamp(ActionValues[1], -1.0f, 1.0f);
    Weights.CoverDensity            = FMath::Clamp(ActionValues[2], -1.0f, 1.0f);
    Weights.EnemyVisibility         = FMath::Clamp(ActionValues[3], -1.0f, 1.0f);
    Weights.AllyProximity           = FMath::Clamp(ActionValues[4], -1.0f, 1.0f);
    Weights.CombatRange             = FMath::Clamp(ActionValues[5], -1.0f, 1.0f);
    Weights.PickupProximity         = FMath::Clamp(ActionValues[6], -1.0f, 1.0f);
    Weights.HeightAdvantage         = FMath::Clamp(ActionValues[7], -1.0f, 1.0f);

    // 가중치 유효성 검사
    if (!ValidateEQSWeights(Weights))
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] EQS weights validation failed, using defaults"));
        return;
    }

    LastAction = Weights;

    // EQS 가중치를 Character의 이동 시스템에 적용
    ApplyEQSWeightsToCharacter(Weights);
}

float AMocTrainer::ComputeReward()
{
    if (!MocAgent || !ControlledCharacter)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] Cannot compute reward - invalid agent or character"));
        return 0.0f;
    }

    // v10.2: Get commanded strategy from Character (set by Squad Commander)
    EStrategyType CommandedStrategy = ControlledCharacter->GetCommandedStrategy();

    // 현재 상태와 이전 상태 비교하여 보상 계산
    float StepReward = ComputeCommandedStrategyReward(
        CommandedStrategy,
        PreviousObservation,
        CurrentObservation,
        LastAction
    );

    EpisodeReward += StepReward;

    // Transition 로깅 (World Model 학습용)
    if (bLogTransitions && TransitionLogger)
    {
        LogTransition(
            PreviousObservation,
            CommandedStrategy,
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
    
    if (!ControlledCharacter || ControlledCharacter->GetHealthPercentage() <= 0.0f)
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
    UE_LOG(LogTemp, Log, TEXT("[MocTrainer] Resetting episode. Total reward: %.2f, Steps: %d"),
        EpisodeReward, CurrentEpisodeSteps);

    // 훈련 통계 업데이트
    UpdateTrainingStatistics();

    // 통계 초기화
    CurrentEpisodeSteps = 0;
    EpisodeReward = 0.0f;

    // v10.2: Squad Commander가 새로운 전략을 할당
    if (ControlledCharacter)
    {
        CachedCommandedStrategy = ControlledCharacter->GetCommandedStrategy();
    }

    // 상태 초기화
    PreviousObservation = FObservation();
    CurrentObservation = GatherStateObservation();

    // EQS 타겟 초기화
    LastEQSTargetLocation = FVector::ZeroVector;
}

// ==================== Helper Functions ====================

FObservation AMocTrainer::GatherStateObservation()
{
    FObservation Obs;

    if (!ControlledCharacter) return Obs;

    // Self state
    Obs.Position = ControlledCharacter->GetActorLocation();
    Obs.Health = ControlledCharacter->GetHealthPercentage(); // [0.0-1.0]
    Obs.Velocity = ControlledCharacter->GetVelocity();
    Obs.WeaponCooldown = ControlledCharacter->GetWeaponCooldown_Implementation();
    Obs.CurrentStrategy = ControlledCharacter->GetCommandedStrategy();
    Obs.bIsAlive = ControlledCharacter->IsAlive();

    // 팀원 정보 (4 allies)
    TArray<AActor*> AllCharacters;
    UGameplayStatics::GetAllActorsOfClass(
        GetWorld(),
        AMocCharacter::StaticClass(),
        AllCharacters
    );

    int32 AllyIndex = 0;
    int32 EnemyIndex = 0;

    for (AActor* Actor : AllCharacters)
    {
        AMocCharacter* FoundCharacter = Cast<AMocCharacter>(Actor);
        if (!FoundCharacter || FoundCharacter == ControlledCharacter) continue;

        if (FoundCharacter->GetTeamID() == ControlledCharacter->GetTeamID())
        {
            // Ally
            if (AllyIndex < 4)
            {
                Obs.AllyPositions[AllyIndex] = FoundCharacter->GetActorLocation();
                Obs.AllyHealths[AllyIndex] = FoundCharacter->GetHealthPercentage();
                Obs.AllyStrategies[AllyIndex] = FoundCharacter->GetCommandedStrategy();
                AllyIndex++;
            }
        }
        else
        {
            // Enemy
            if (EnemyIndex < 5)
            {
                Obs.EnemyPositions[EnemyIndex] = FoundCharacter->GetActorLocation();

                // Simple visibility check: line of sight within vision range
                FVector ToEnemy = FoundCharacter->GetActorLocation() - ControlledCharacter->GetActorLocation();
                float Distance = ToEnemy.Size();
                bool bVisible = false;

                if (Distance < 8000.0f) // Vision range from FAgentStats
                {
                    // Simple line trace for visibility
                    FHitResult HitResult;
                    FCollisionQueryParams QueryParams;
                    QueryParams.AddIgnoredActor(ControlledCharacter);

                    bVisible = !GetWorld()->LineTraceSingleByChannel(
                        HitResult,
                        ControlledCharacter->GetActorLocation() + FVector(0, 0, 90), // Eye height
                        FoundCharacter->GetActorLocation() + FVector(0, 0, 90),
                        ECC_Visibility,
                        QueryParams
                    );
                }

                Obs.EnemyVisible[EnemyIndex] = bVisible;
                EnemyIndex++;
            }
        }
    }

    // Map state (capture points)
    // TODO: Implement capture point balance calculation
    Obs.CapturePointBalance = 0;
    Obs.TimeRemaining = 1.0f; // TODO: Get from game mode

    return Obs;
}

float AMocTrainer::ComputeCommandedStrategyReward(
    EStrategyType CommandedStrategy,
    const FObservation& Prev,
    const FObservation& Current,
    const FEQSWeightParameters& Action
)
{
    float Reward = 0.0f;

    // v10.2: Team-aligned rewards based on commanded strategy execution quality
    // Using configurable parameters for reward shaping

    switch (CommandedStrategy)
    {
    case EStrategyType::Assault:
        {
            // Reward for aggressive positioning toward enemies
            float PositionChange = FVector::Dist(Prev.Position, Current.Position);
            Reward += AssaultMovementReward * PositionChange;

            // Health management (aggressive but not reckless)
            float HealthLoss = Prev.Health - Current.Health;
            if (HealthLoss > 0.3f)
            {
                Reward -= AssaultHealthPenalty * HealthLoss;
            }

            // Reward for maintaining weapon readiness
            if (Current.WeaponCooldown < 0.5f)
            {
                Reward += 0.5f;
            }
        }
        break;

    case EStrategyType::Defend:
        {
            // Reward for staying in defensive position (low movement)
            float PositionChange = FVector::Dist(Prev.Position, Current.Position);
            if (PositionChange < 200.0f) // Staying relatively still
            {
                Reward += DefendPositionReward;
            }

            // Health preservation is critical for defenders
            if (Current.Health > 0.7f)
            {
                Reward += DefendHealthBonus;
            }

            // Reward for weapon readiness
            if (Current.WeaponCooldown < 0.3f)
            {
                Reward += 1.0f;
            }
        }
        break;

    case EStrategyType::Support:
        {
            // Reward for moderate positioning (not too aggressive, not too passive)
            float PositionChange = FVector::Dist(Prev.Position, Current.Position);
            if (PositionChange > 100.0f && PositionChange < 500.0f)
            {
                Reward += SupportPositionReward;
            }

            // Health preservation important for support
            if (Current.Health > 0.8f)
            {
                Reward += SupportHealthBonus;
            }
        }
        break;
    }

    // Common penalties
    if (!Current.bIsAlive)
    {
        Reward -= DeathPenalty;
    }

    Reward -= TimePenalty; // Encourage efficiency

    return Reward;
}

void AMocTrainer::ApplyEQSWeightsToCharacter(const FEQSWeightParameters& Weights)
{
    if (!ControlledCharacter)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] Cannot apply EQS weights - invalid character"));
        return;
    }

    // Training Mode: EQS를 직접 실행하여 이동 위치 결정
    UEnvQueryManager* EQSManager = UEnvQueryManager::GetCurrent(GetWorld());
    if (!EQSManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] EQS Manager not available"));
        return;
    }

    if (!EQSQueryTemplate)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] EQS Query Template not set"));
        return;
    }

    // 가중치 적용하여 Query 생성
    FEnvQueryRequest QueryRequest(EQSQueryTemplate, ControlledCharacter);

    // Named Parameters로 가중치 전달 (스케일링하여 적용)
    QueryRequest.SetFloatParam(TEXT("EnemyObjectiveWeight"), Weights.EnemyObjectiveProximity * 2.0f);
    QueryRequest.SetFloatParam(TEXT("AllyObjectiveWeight"), Weights.AllyObjectiveProximity * 2.0f);
    QueryRequest.SetFloatParam(TEXT("CoverDensityWeight"), Weights.CoverDensity * 2.0f);
    QueryRequest.SetFloatParam(TEXT("EnemyVisibilityWeight"), Weights.EnemyVisibility * 2.0f);
    QueryRequest.SetFloatParam(TEXT("AllyProximityWeight"), Weights.AllyProximity * 2.0f);
    QueryRequest.SetFloatParam(TEXT("CombatRangeWeight"), Weights.CombatRange * 2.0f);
    QueryRequest.SetFloatParam(TEXT("PickupWeight"), Weights.PickupProximity * 2.0f);
    QueryRequest.SetFloatParam(TEXT("HeightWeight"), Weights.HeightAdvantage * 2.0f);

    // 검색 반경 설정
    QueryRequest.SetFloatParam(TEXT("SearchRadius"), EQSSearchRadius);

    // 비동기 실행
    FQueryFinishedSignature Delegate;
    Delegate.BindLambda([this](TSharedPtr<FEnvQueryResult> Result)
    {
        if (!Result.IsValid() || !Result->IsSuccessful())
        {
            UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] EQS query failed"));
            return;
        }

        if (!ControlledCharacter)
        {
            UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] Character destroyed during EQS execution"));
            return;
        }

        FVector TargetLocation = Result->GetItemAsLocation(0);
        LastEQSTargetLocation = TargetLocation;

        AAIController* AIC = Cast<AAIController>(ControlledCharacter->GetController());
        if (AIC)
        {

            EPathFollowingRequestResult::Type MoveResult = AIC->MoveToLocation(TargetLocation, EQSAcceptanceRadius);
            if (MoveResult != EPathFollowingRequestResult::RequestSuccessful)
            {
                UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] Movement request failed"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] Character has no AI controller"));
        }
    });

    QueryRequest.Execute(EEnvQueryRunMode::SingleResult, Delegate);
}

void AMocTrainer::LogTransition(
    const FObservation& InState,
    EStrategyType CommandedStrategy,
    const FEQSWeightParameters& Action,
    float Reward,
    const FObservation& NextState,
    bool bDone
)
{
    if (!TransitionLogger) return;

    // Convert to UScholaTransitionLogger format
    TArray<float> StateBefore = InState.ToArray();
    TArray<float> StateAfter = NextState.ToArray();

    // Create FTacticalOption from commanded strategy
    // v10.2: Individual agents don't choose options, they execute commanded strategies
    // We log the commanded strategy as a tactical option for consistency
    FTacticalOption Option;
    Option.Strategy = CommandedStrategy;
    Option.Confidence = 1.0f; // Full confidence in commanded strategy

    // Convert scalar reward to FCompositeReward
    // v10.2: For simplicity, put the entire reward in ObjectiveScore
    FCompositeReward CompositeReward;
    CompositeReward.WinProb = 0.0f;
    CompositeReward.HealthDelta = (NextState.Health - InState.Health);
    CompositeReward.ObjectiveScore = Reward;


    // Record transition
    TransitionLogger->RecordTransition(StateBefore, Option, CompositeReward, StateAfter, bDone);
}

void AMocTrainer::DrawTrainingDebug()
{
    if (!ControlledCharacter || !GetWorld()) return;

    FVector CharLocation = ControlledCharacter->GetActorLocation();

    // === Agent Info Text ===
    // Check if training override is active
    FString StrategyInfo = UEnum::GetValueAsString(CachedCommandedStrategy);
    if (MocAgent && MocAgent->bUseTrainingStrategyOverride)
    {
        StrategyInfo += TEXT(" [TRAINING OVERRIDE]");
    }

    FString DebugText = FString::Printf(
        TEXT("Strategy: %s\n")
        TEXT("Health: %.1f%%\n")
        TEXT("Steps: %d / %d\n")
        TEXT("Episode Reward: %.2f\n")
        TEXT("Total Episodes: %d\n")
        TEXT("---EQS Weights---\n")
        TEXT("EnemyObj: %.2f | AllyObj: %.2f\n")
        TEXT("Cover: %.2f | Visibility: %.2f\n")
        TEXT("AllyProx: %.2f | Range: %.2f\n")
        TEXT("Pickup: %.2f | Height: %.2f"),
        *StrategyInfo,
        CurrentObservation.Health * 100.0f,
        CurrentEpisodeSteps,
        MaxEpisodeSteps,
        EpisodeReward,
        TotalEpisodes,
        LastAction.EnemyObjectiveProximity,
        LastAction.AllyObjectiveProximity,
        LastAction.CoverDensity,
        LastAction.EnemyVisibility,
        LastAction.AllyProximity,
        LastAction.CombatRange,
        LastAction.PickupProximity,
        LastAction.HeightAdvantage
    );

    DrawDebugString(
        GetWorld(),
        CharLocation + FVector(0, 0, 250),
        DebugText,
        nullptr,
        FColor::Cyan,
        0.0f,
        true,
        1.2f
    );

    // === EQS Target Location ===
    if (!LastEQSTargetLocation.IsZero())
    {
        DrawDebugSphere(
            GetWorld(),
            LastEQSTargetLocation,
            100.0f,
            16,
            FColor::Yellow,
            false,
            0.0f,
            0,
            5.0f
        );

        DrawDebugLine(
            GetWorld(),
            CharLocation,
            LastEQSTargetLocation,
            FColor::Yellow,
            false,
            0.0f,
            0,
            2.0f
        );

        float DistToTarget = FVector::Dist(CharLocation, LastEQSTargetLocation);
        DrawDebugString(
            GetWorld(),
            LastEQSTargetLocation + FVector(0, 0, 150),
            FString::Printf(TEXT("Target\nDist: %.0f"), DistToTarget),
            nullptr,
            FColor::Yellow,
            0.0f,
            true
        );
    }

    // === Allies (Green) ===
    for (int32 i = 0; i < CurrentObservation.AllyPositions.Num(); ++i)
    {
        if (!CurrentObservation.AllyPositions[i].IsZero())
        {
            DrawDebugSphere(
                GetWorld(),
                CurrentObservation.AllyPositions[i],
                50.0f,
                12,
                FColor::Green,
                false,
                0.0f,
                0,
                2.0f
            );
        }
    }

    // === Enemies (Red - visible only) ===
    for (int32 i = 0; i < CurrentObservation.EnemyPositions.Num(); ++i)
    {
        if (CurrentObservation.EnemyVisible[i] && !CurrentObservation.EnemyPositions[i].IsZero())
        {
            DrawDebugSphere(
                GetWorld(),
                CurrentObservation.EnemyPositions[i],
                50.0f,
                12,
                FColor::Red,
                false,
                0.0f,
                0,
                2.0f
            );

            DrawDebugLine(
                GetWorld(),
                CharLocation + FVector(0, 0, 90),
                CurrentObservation.EnemyPositions[i] + FVector(0, 0, 90),
                FColor::Red,
                false,
                0.0f,
                0,
                1.0f
            );
        }
    }

    // === Strategy-specific indicators ===
    FColor StrategyColor;
    switch (CachedCommandedStrategy)
    {
    case EStrategyType::Assault:
        StrategyColor = FColor::Orange;
        break;
    case EStrategyType::Defend:
        StrategyColor = FColor::Blue;
        break;
    case EStrategyType::Support:
        StrategyColor = FColor::Purple;
        break;
    default:
        StrategyColor = FColor::White;
        break;
    }

    DrawDebugSphere(
        GetWorld(),
        CharLocation,
        150.0f,
        8,
        StrategyColor,
        false,
        0.0f,
        0,
        3.0f
    );
}

// ==================== AAbstractTrainer Pure Virtual Implementations ====================

EAgentTrainingStatus AMocTrainer::ComputeStatus()
{
    // Check termination conditions
    if (CurrentEpisodeSteps >= MaxEpisodeSteps)
    {
        return EAgentTrainingStatus::Truncated; // Episode truncated due to max steps
    }

    if (!ControlledCharacter || !ControlledCharacter->IsAlive())
    {
        return EAgentTrainingStatus::Completed; // Episode complete (agent died)
    }

    // Check game mode for victory/defeat conditions
    // TODO: Implement game-specific termination logic

    return EAgentTrainingStatus::Running;
}

void AMocTrainer::GetInfo(TMap<FString, FString>& Info)
{
    // Provide comprehensive debug/training information
    Info.Add(TEXT("TotalEpisodes"), FString::FromInt(TotalEpisodes));
    Info.Add(TEXT("CurrentSteps"), FString::FromInt(CurrentEpisodeSteps));
    Info.Add(TEXT("MaxSteps"), FString::FromInt(MaxEpisodeSteps));
    Info.Add(TEXT("EpisodeReward"), FString::Printf(TEXT("%.3f"), EpisodeReward));
    Info.Add(TEXT("AverageReward"), FString::Printf(TEXT("%.3f"),
        TotalEpisodes > 0 ? CumulativeReward / TotalEpisodes : 0.0f));
    Info.Add(TEXT("AverageLength"), FString::Printf(TEXT("%.1f"), AverageEpisodeLength));

    if (ControlledCharacter)
    {
        Info.Add(TEXT("Strategy"), UEnum::GetValueAsString(CachedCommandedStrategy));
        Info.Add(TEXT("Health"), FString::Printf(TEXT("%.1f%%"),
            ControlledCharacter->GetHealthPercentage() * 100.0f));
        Info.Add(TEXT("IsAlive"), ControlledCharacter->IsAlive() ? TEXT("true") : TEXT("false"));

        if (!LastEQSTargetLocation.IsZero())
        {
            float DistToTarget = FVector::Dist(
                ControlledCharacter->GetActorLocation(),
                LastEQSTargetLocation
            );
            Info.Add(TEXT("DistanceToTarget"), FString::Printf(TEXT("%.0f"), DistToTarget));
        }
    }

    // Reward breakdown
    if (ControlledCharacter)
    {
        FRewardBreakdown Breakdown = CalculateRewardBreakdown(
            CachedCommandedStrategy,
            PreviousObservation,
            CurrentObservation
        );
        Info.Add(TEXT("RewardPosition"), FString::Printf(TEXT("%.3f"), Breakdown.PositionComponent));
        Info.Add(TEXT("RewardHealth"), FString::Printf(TEXT("%.3f"), Breakdown.HealthComponent));
        Info.Add(TEXT("RewardDeath"), FString::Printf(TEXT("%.3f"), Breakdown.DeathPenaltyComponent));
    }
}

void AMocTrainer::ResetTrainer()
{
    // Reset per-episode state
    CurrentEpisodeSteps = 0;
    EpisodeReward = 0.0f;

    // Reset observations
    PreviousObservation = FObservation();

    if (ControlledCharacter)
    {
        CurrentObservation = GatherStateObservation();
        CachedCommandedStrategy = ControlledCharacter->GetCommandedStrategy();
    }
    else
    {
        CurrentObservation = FObservation();
    }

    // Reset EQS target
    LastEQSTargetLocation = FVector::ZeroVector;

    UE_LOG(LogTemp, Log, TEXT("[MocTrainer] v10.2 Trainer reset for episode %d"), TotalEpisodes + 1);
}

void AMocTrainer::OnCompletion()
{
    // Episode completion callback
    UE_LOG(LogTemp, Log, TEXT("[MocTrainer] Episode completed. Final reward: %.2f, Steps: %d"),
        EpisodeReward, CurrentEpisodeSteps);

    // 훈련 통계 업데이트
    UpdateTrainingStatistics();

    // Flush transition logs if enabled
    if (bLogTransitions && TransitionLogger)
    {
        TransitionLogger->FlushToDisk();
        UE_LOG(LogTemp, Log, TEXT("[MocTrainer] Transition logs flushed to disk"));
    }
}

// ==================== Utility Functions ====================

bool AMocTrainer::ValidateEQSWeights(const FEQSWeightParameters& Weights) const
{
    // 모든 가중치가 유효 범위 내에 있는지 확인
    bool bValid = true;

    auto CheckRange = [&bValid](float Value, const FString& Name)
    {
        if (FMath::IsNaN(Value) || FMath::IsFinite(Value) == false)
        {
            UE_LOG(LogTemp, Error, TEXT("[MocTrainer] Invalid EQS weight %s: %f"), *Name, Value);
            bValid = false;
        }
    };

    CheckRange(Weights.EnemyObjectiveProximity, TEXT("EnemyObjectiveProximity"));
    CheckRange(Weights.AllyObjectiveProximity, TEXT("AllyObjectiveProximity"));
    CheckRange(Weights.CoverDensity, TEXT("CoverDensity"));
    CheckRange(Weights.EnemyVisibility, TEXT("EnemyVisibility"));
    CheckRange(Weights.AllyProximity, TEXT("AllyProximity"));
    CheckRange(Weights.CombatRange, TEXT("CombatRange"));
    CheckRange(Weights.PickupProximity, TEXT("PickupProximity"));
    CheckRange(Weights.HeightAdvantage, TEXT("HeightAdvantage"));

    return bValid;
}

bool AMocTrainer::ValidateObservation(const FObservation& Obs) const
{
    // Observation의 주요 필드가 유효한지 확인
    if (FMath::IsNaN(Obs.Health) || Obs.Health < 0.0f || Obs.Health > 1.0f)
    {
        UE_LOG(LogTemp, Error, TEXT("[MocTrainer] Invalid health value: %f"), Obs.Health);
        return false;
    }

    if (!Obs.Position.ContainsNaN() == false || !Obs.Position.IsNormalized())
    {
        // Position can be any valid vector
        if (Obs.Position.ContainsNaN())
        {
            UE_LOG(LogTemp, Error, TEXT("[MocTrainer] Position contains NaN"));
            return false;
        }
    }

    if (Obs.AllyPositions.Num() != 4 || Obs.AllyHealths.Num() != 4 || Obs.AllyStrategies.Num() != 4)
    {
        UE_LOG(LogTemp, Error, TEXT("[MocTrainer] Ally data array size mismatch"));
        return false;
    }

    if (Obs.EnemyPositions.Num() != 5 || Obs.EnemyVisible.Num() != 5)
    {
        UE_LOG(LogTemp, Error, TEXT("[MocTrainer] Enemy data array size mismatch"));
        return false;
    }

    return true;
}

void AMocTrainer::UpdateTrainingStatistics()
{
    TotalEpisodes++;
    CumulativeReward += EpisodeReward;

    // 이동 평균으로 평균 에피소드 길이 업데이트
    float Alpha = 0.1f; // 지수 이동 평균 계수
    AverageEpisodeLength = Alpha * CurrentEpisodeSteps + (1.0f - Alpha) * AverageEpisodeLength;

    UE_LOG(LogTemp, Log, TEXT("[MocTrainer] Episode %d completed - Reward: %.2f, Steps: %d, Avg Length: %.1f"),
        TotalEpisodes, EpisodeReward, CurrentEpisodeSteps, AverageEpisodeLength);
}

AMocTrainer::FRewardBreakdown AMocTrainer::CalculateRewardBreakdown(
    EStrategyType Strategy,
    const FObservation& Prev,
    const FObservation& Current
) const
{
    FRewardBreakdown Breakdown;
    Breakdown.StrategyReward = 0.0f;
    Breakdown.HealthComponent = 0.0f;
    Breakdown.PositionComponent = 0.0f;
    Breakdown.DeathPenaltyComponent = 0.0f;
    Breakdown.TimePenaltyComponent = -TimePenalty;

    float PositionChange = FVector::Dist(Prev.Position, Current.Position);
    float HealthLoss = Prev.Health - Current.Health;

    switch (Strategy)
    {
    case EStrategyType::Assault:
        Breakdown.PositionComponent = AssaultMovementReward * PositionChange;
        if (HealthLoss > 0.3f)
        {
            Breakdown.HealthComponent = -AssaultHealthPenalty * HealthLoss;
        }
        break;

    case EStrategyType::Defend:
        if (PositionChange < 200.0f)
        {
            Breakdown.PositionComponent = DefendPositionReward;
        }
        if (Current.Health > 0.7f)
        {
            Breakdown.HealthComponent = DefendHealthBonus;
        }
        break;

    case EStrategyType::Support:
        if (PositionChange > 100.0f && PositionChange < 500.0f)
        {
            Breakdown.PositionComponent = SupportPositionReward;
        }
        if (Current.Health > 0.8f)
        {
            Breakdown.HealthComponent = SupportHealthBonus;
        }
        break;
    }

    if (!Current.bIsAlive)
    {
        Breakdown.DeathPenaltyComponent = -DeathPenalty;
    }

    Breakdown.StrategyReward = Breakdown.PositionComponent + Breakdown.HealthComponent;
    Breakdown.Total = Breakdown.StrategyReward + Breakdown.DeathPenaltyComponent + Breakdown.TimePenaltyComponent;

    return Breakdown;
}

void AMocTrainer::GetTrainingStats(int32& OutEpisodes, float& OutAvgReward, float& OutAvgLength) const
{
    OutEpisodes = TotalEpisodes;
    OutAvgReward = TotalEpisodes > 0 ? CumulativeReward / TotalEpisodes : 0.0f;
    OutAvgLength = AverageEpisodeLength;
}

void AMocTrainer::LogRewardBreakdown() const
{
    if (!ControlledCharacter) return;

    FRewardBreakdown Breakdown = CalculateRewardBreakdown(
        CachedCommandedStrategy,
        PreviousObservation,
        CurrentObservation
    );

    UE_LOG(LogTemp, Log, TEXT("[MocTrainer] Reward Breakdown - Strategy: %s"),
        *UEnum::GetValueAsString(CachedCommandedStrategy));
    UE_LOG(LogTemp, Log, TEXT("  Position: %.3f, Health: %.3f, Death: %.3f, Time: %.3f"),
        Breakdown.PositionComponent,
        Breakdown.HealthComponent,
        Breakdown.DeathPenaltyComponent,
        Breakdown.TimePenaltyComponent);
    UE_LOG(LogTemp, Log, TEXT("  Total: %.3f"), Breakdown.Total);
}
