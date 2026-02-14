// File: Schola/Trainers/MocTrainer.cpp

#include "Schola/Trainers/MocTrainer.h"
#include "Schola/Components/ScholaMocAgent.h"
#include "Schola/Logging/ScholaTransitionLogger.h"
#include "Schola/Logging/MocTransitionLogger.h"
#include "Characters/MocCharacter.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Types/RewardTypes.h"
#include "AIController.h"
#include "Training/StateStructs/TrainerState.h"
#include "Team/TeamManager.h"
#include "Team/FogOfWarManager.h"
#include "Combat/Components/WeaponComponent.h"
#include "Core/MocGameMode.h"

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

    if (!ControlledCharacter || !ControlledCharacter->IsAlive_Implementation())
    {
        return;
    }

    // Update commanded strategy cache (may change from Squad Commander)
    EStrategyType NewStrategy = ControlledCharacter->GetCommandedStrategy();
    if (NewStrategy != CachedCommandedStrategy)
    {
        UE_LOG(LogTemp, Log, TEXT("[MocTrainer] Strategy changed: %s -> %s"),
            *UEnum::GetValueAsString(CachedCommandedStrategy),
            *UEnum::GetValueAsString(NewStrategy));
        CachedCommandedStrategy = NewStrategy;
    }

    // Execute tactical action ONLY when the actuator has written NEW weights.
    // ConsumeNewWeights() returns true once per Schola Act() step, preventing
    // redundant EQS queries on every tick (60Hz).
    if (ControlledCharacter->ConsumeNewWeights())
    {
        LastAction = ControlledCharacter->GetEQSWeights();
        CurrentEpisodeSteps++;

        // ===== DIAGNOSTIC LOG: About to execute tactical action =====
        UE_LOG(LogTemp, Warning, TEXT("[DIAG-TRAINER] %s executing tactical action at step %d"),
            *ControlledCharacter->GetName(), CurrentEpisodeSteps);
        UE_LOG(LogTemp, Warning, TEXT("[DIAG-TRAINER]   Strategy: %s"), *UEnum::GetValueAsString(CachedCommandedStrategy));

        ControlledCharacter->PerformTacticalAction();

        // Update observation state for reward computation
        PreviousObservation = CurrentObservation;
        CurrentObservation = GatherStateObservation();

        // Pre-compute reward for this action step.
        // ComputeReward() is called every tick by Schola's Think(), but the
        // meaningful state diff only exists right after a new action is applied.
        // Cache the reward here so ComputeReward() returns the correct value
        // on every subsequent tick until the next action arrives.
        EStrategyType CommandedStrategy = ControlledCharacter->GetCommandedStrategy();
        CachedStepReward = ComputeCommandedStrategyReward(
            CommandedStrategy,
            PreviousObservation,
            CurrentObservation,
            LastAction
        );
        bHasNewReward = true;
    }

    // Detect enemies and report to Fog of War (replaces BT service)
    DetectAndReportEnemies();

    // Handle combat (replaces BT task)
    HandleCombat();

    // Debug visualization
    if (bEnableDebugVisualization)
    {
        DrawTrainingDebug(DeltaTime);
    }
}

// ==================== Schola Interface Implementation ====================

TArray<float> AMocTrainer::GetObservation()
{
    // NOTE: This method is NOT called by Schola (Schola uses MocTacticalObserver::CollectObservations).
    // It exists as a legacy/utility interface. Tactical execution happens in Tick().

    // Gather current state
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

float AMocTrainer::ComputeReward()
{
    // v10.2 FIX: ComputeReward() is called every tick by Schola's Think() loop,
    // but meaningful state changes only occur when a new action is applied (ConsumeNewWeights).
    // Use cached reward to ensure Python receives the correct per-action reward,
    // not the near-zero diff between identical consecutive-tick observations.

    if (!MocAgent || !IsValid(MocAgent))
    {
        return 0.0f;
    }

    if (!ControlledCharacter || !IsValid(ControlledCharacter))
    {
        return 0.0f;
    }

    // Return cached reward from the last action step.
    // bHasNewReward is set in Tick() when ConsumeNewWeights() triggers.
    // On the first call after a new action, return the real reward and log.
    // On subsequent ticks (before next action), return 0 to avoid double-counting.
    if (bHasNewReward)
    {
        bHasNewReward = false;

        float StepReward = CachedStepReward;
        EpisodeReward += StepReward;

        // Transition 로깅 (World Model 학습용)
        if (bLogTransitions && TransitionLogger)
        {
            EStrategyType CommandedStrategy = ControlledCharacter->GetCommandedStrategy();
            LogTransition(
                PreviousObservation,
                CommandedStrategy,
                LastAction,
                StepReward,
                CurrentObservation,
                IsEpisodeDone()
            );
        }

        return StepReward;
    }

    // No new action since last reward computation - return 0
    return 0.0f;
}

bool AMocTrainer::IsEpisodeDone()
{
    // 종료 조건
    if (CurrentEpisodeSteps >= MaxEpisodeSteps)
    {
        UE_LOG(LogTemp, Log, TEXT("Episode ended: Max steps reached"));
        return true;
    }
    
    if (!ControlledCharacter || ControlledCharacter->GetHealthPercentage_Implementation() <= 0.0f)
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
}

// ==================== Helper Functions ====================

FObservation AMocTrainer::GatherStateObservation()
{
    FObservation Obs;

    if (!ControlledCharacter) return Obs;

    // Self state
    Obs.Position = ControlledCharacter->GetActorLocation();
    Obs.Health = ControlledCharacter->GetHealthPercentage_Implementation(); // [0.0-1.0]
    Obs.Velocity = ControlledCharacter->GetVelocity();
    Obs.WeaponCooldown = ControlledCharacter->GetWeaponCooldown_Implementation();
    Obs.CurrentStrategy = ControlledCharacter->GetCommandedStrategy();
    Obs.bIsAlive = ControlledCharacter->IsAlive_Implementation();

    int32 MyTeamID = ControlledCharacter->GetTeamID_Implementation();

    // Get TeamManager and FogOfWarManager
    AMocGameMode* GameMode = Cast<AMocGameMode>(UGameplayStatics::GetGameMode(GetWorld()));
    if (!GameMode)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] GameMode not found"));
        return Obs;
    }

    ATeamManager* TeamManager = GameMode->GetTeamManager();
    if (!TeamManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] TeamManager not found"));
        return Obs;
    }

    AFogOfWarManager* FogManager = TeamManager->GetFogOfWarManager();
    if (!FogManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] FogOfWarManager not found"));
        return Obs;
    }

    // Collect ally information (allies are always known)
    TArray<AActor*> AllCharacters;
    UGameplayStatics::GetAllActorsOfClass(
        GetWorld(),
        AMocCharacter::StaticClass(),
        AllCharacters
    );

    int32 AllyIndex = 0;
    for (AActor* Actor : AllCharacters)
    {
        AMocCharacter* FoundCharacter = Cast<AMocCharacter>(Actor);
        if (!FoundCharacter || FoundCharacter == ControlledCharacter) continue;

        if (FoundCharacter->GetTeamID_Implementation() == MyTeamID)
        {
            // Ally - always known
            if (AllyIndex < 4)
            {
                Obs.AllyPositions[AllyIndex] = FoundCharacter->GetActorLocation();
                Obs.AllyHealths[AllyIndex] = FoundCharacter->GetHealthPercentage_Implementation();
                Obs.AllyStrategies[AllyIndex] = FoundCharacter->GetCommandedStrategy();
                AllyIndex++;
            }
        }
    }

    // Collect enemy information from Fog of War (only known enemies)
    TArray<AActor*> RememberedEnemies = FogManager->GetRememberedEnemies(MyTeamID);
    int32 EnemyIndex = 0;

    for (AActor* EnemyActor : RememberedEnemies)
    {
        if (EnemyIndex >= 5) break;

        AMocCharacter* Enemy = Cast<AMocCharacter>(EnemyActor);
        if (!Enemy) continue;

        // Get last known position from Fog of War
        FVector LastKnownPosition = FogManager->GetLastKnownEnemyPosition(MyTeamID, EnemyActor);
        Obs.EnemyPositions[EnemyIndex] = LastKnownPosition;

        // Check if enemy is currently visible (line of sight)
        FVector ToEnemy = Enemy->GetActorLocation() - ControlledCharacter->GetActorLocation();
        float Distance = ToEnemy.Size();
        bool bVisible = false;

        if (Distance < 8000.0f) // Vision range
        {
            FHitResult HitResult;
            FCollisionQueryParams QueryParams;
            QueryParams.AddIgnoredActor(ControlledCharacter);

            bVisible = !GetWorld()->LineTraceSingleByChannel(
                HitResult,
                ControlledCharacter->GetActorLocation() + FVector(0, 0, 90), // Eye height
                Enemy->GetActorLocation() + FVector(0, 0, 90),
                ECC_Visibility,
                QueryParams
            );
        }

        Obs.EnemyVisible[EnemyIndex] = bVisible;
        EnemyIndex++;
    }

    // Map state (capture points)
    // TODO: Implement capture point balance calculation
    Obs.CapturePointBalance = 0;
    Obs.TimeRemaining = 1.0f; // TODO: Get from game mode

    return Obs;
}

void AMocTrainer::DetectAndReportEnemies()
{
    if (!ControlledCharacter) return;

    // Get managers
    AMocGameMode* GameMode = Cast<AMocGameMode>(UGameplayStatics::GetGameMode(GetWorld()));
    if (!GameMode) return;

    ATeamManager* TeamManager = GameMode->GetTeamManager();
    if (!TeamManager) return;

    int32 MyTeamID = ControlledCharacter->GetTeamID_Implementation();
    FVector MyLocation = ControlledCharacter->GetActorLocation();

    // Find all characters in the level
    TArray<AActor*> AllCharacters;
    UGameplayStatics::GetAllActorsOfClass(
        GetWorld(),
        AMocCharacter::StaticClass(),
        AllCharacters
    );

    // Check each character for visibility
    for (AActor* Actor : AllCharacters)
    {
        AMocCharacter* OtherCharacter = Cast<AMocCharacter>(Actor);
        if (!OtherCharacter || OtherCharacter == ControlledCharacter) continue;

        // Only check enemies
        if (OtherCharacter->GetTeamID_Implementation() == MyTeamID) continue;

        // Check distance
        FVector ToEnemy = OtherCharacter->GetActorLocation() - MyLocation;
        float Distance = ToEnemy.Size();

        if (Distance > 8000.0f) continue; // Outside vision range

        // Line of sight check
        FHitResult HitResult;
        FCollisionQueryParams QueryParams;
        QueryParams.AddIgnoredActor(ControlledCharacter);

        bool bHasLineOfSight = !GetWorld()->LineTraceSingleByChannel(
            HitResult,
            MyLocation + FVector(0, 0, 90), // Eye height
            OtherCharacter->GetActorLocation() + FVector(0, 0, 90),
            ECC_Visibility,
            QueryParams
        );

        // Report to Fog of War if visible
        if (bHasLineOfSight)
        {
            TeamManager->ReportEnemySighting(MyTeamID, OtherCharacter, OtherCharacter->GetActorLocation());
        }
    }
}

void AMocTrainer::HandleCombat()
{
    if (!ControlledCharacter) return;

    UWeaponComponent* Weapon = ControlledCharacter->GetWeaponComponent();
    if (!Weapon || !Weapon->CanFire()) return;

    // Get visible enemies from current observation
    AActor* ClosestEnemy = nullptr;
    float ClosestDistance = FLT_MAX;

    int32 MyTeamID = ControlledCharacter->GetTeamID_Implementation();
    FVector MyLocation = ControlledCharacter->GetActorLocation();

    // Get managers
    AMocGameMode* GameMode = Cast<AMocGameMode>(UGameplayStatics::GetGameMode(GetWorld()));
    if (!GameMode) return;

    ATeamManager* TeamManager = GameMode->GetTeamManager();
    if (!TeamManager) return;

    AFogOfWarManager* FogManager = TeamManager->GetFogOfWarManager();
    if (!FogManager) return;

    // Get remembered enemies
    TArray<AActor*> RememberedEnemies = FogManager->GetRememberedEnemies(MyTeamID);

    for (AActor* EnemyActor : RememberedEnemies)
    {
        AMocCharacter* Enemy = Cast<AMocCharacter>(EnemyActor);
        if (!Enemy || !Enemy->IsAlive_Implementation()) continue;

        // Check if currently visible (line of sight)
        FVector ToEnemy = Enemy->GetActorLocation() - MyLocation;
        float Distance = ToEnemy.Size();

        if (Distance > 8000.0f) continue; // Outside vision range

        FHitResult HitResult;
        FCollisionQueryParams QueryParams;
        QueryParams.AddIgnoredActor(ControlledCharacter);

        bool bVisible = !GetWorld()->LineTraceSingleByChannel(
            HitResult,
            MyLocation + FVector(0, 0, 90),
            Enemy->GetActorLocation() + FVector(0, 0, 90),
            ECC_Visibility,
            QueryParams
        );

        // Only fire at visible enemies
        if (bVisible && Distance < ClosestDistance)
        {
            ClosestEnemy = Enemy;
            ClosestDistance = Distance;
        }
    }

    // Fire at closest visible enemy
    if (ClosestEnemy)
    {
        // Use predictive aiming for training (helps agents learn tactical positioning)
        bool bUsePrediction = true;
        Weapon->FireAtTarget(ClosestEnemy, bUsePrediction);
    }
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

void AMocTrainer::DrawTrainingDebug(float DeltaTime)
{
    if (!ControlledCharacter || !GetWorld()) return;

    float DrawDuration = DeltaTime * 1.2f;

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
        TEXT("Pickup: %.2f"),
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
        LastAction.PickupProximity
    );

    DrawDebugString(
        GetWorld(),
        CharLocation + FVector(0, 0, 300),
        DebugText,
        nullptr,
        FColor::Cyan,
        DrawDuration,
        true
    );

    // === EQS Target Location ===
    FVector LastEQSTargetLocation = ControlledCharacter->GetLastEQSTargetLocation();
    if (!LastEQSTargetLocation.IsZero())
    {
        DrawDebugSphere(
            GetWorld(),
            LastEQSTargetLocation,
            100.0f,
            16,
            FColor::Yellow,
            false,
            DrawDuration
        );

        DrawDebugLine(
            GetWorld(),
            CharLocation,
            LastEQSTargetLocation,
            FColor::Yellow,
            false,
            DrawDuration
        );

        float DistToTarget = FVector::Dist(CharLocation, LastEQSTargetLocation);
        DrawDebugString(
            GetWorld(),
            LastEQSTargetLocation + FVector(0, 0, 150),
            FString::Printf(TEXT("Target\nDist: %.0f"), DistToTarget),
            nullptr,
            FColor::Yellow,
            DrawDuration,
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
                DrawDuration
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
                DrawDuration
            );

            DrawDebugLine(
                GetWorld(),
                CharLocation + FVector(0, 0, 90),
                CurrentObservation.EnemyPositions[i] + FVector(0, 0, 90),
                FColor::Red,
                false,
                DrawDuration
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
        DrawDuration,
        0,
        3.0f
    );
}

// ==================== AAbstractTrainer Pure Virtual Implementations ====================

EAgentTrainingStatus AMocTrainer::ComputeStatus()
{
    // v10.2 FIX: Added detailed logging to track episode completion causes

    // Check termination conditions
    if (CurrentEpisodeSteps >= MaxEpisodeSteps)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] Episode TRUNCATED - MaxSteps reached (Step %d/%d) - Agent: %s"),
            CurrentEpisodeSteps, MaxEpisodeSteps, *GetName());
        return EAgentTrainingStatus::Truncated; // Episode truncated due to max steps
    }

    // v10.2 FIX: Use IsValid() to guard against dangling pointers,
    // and distinguish "not initialized yet" from "agent actually died"
    if (!ControlledCharacter || !IsValid(ControlledCharacter))
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] ComputeStatus: ControlledCharacter is %s - returning Running to avoid false completion"),
            ControlledCharacter ? TEXT("INVALID (dangling)") : TEXT("NULL (not initialized)"));
        return EAgentTrainingStatus::Running; // Don't trigger completion for uninitialized state
    }

    // v10.2 FIX: Grace period to prevent false death detection during initialization
    // Don't check death status until we've taken at least a few actions
    const int32 InitializationGracePeriod = 3; // Wait for 3 steps before checking death

    if (CurrentEpisodeSteps < InitializationGracePeriod)
    {
        // During initialization, only return Running unless character is explicitly invalid
        UE_LOG(LogTemp, Verbose, TEXT("[MocTrainer] In grace period (Step %d/%d) - skipping death check"),
            CurrentEpisodeSteps, InitializationGracePeriod);
        return EAgentTrainingStatus::Running;
    }

    // After grace period, check if agent actually died
    if (!ControlledCharacter->IsAlive_Implementation())
    {
        float currentHealth = ControlledCharacter->GetHealthPercentage_Implementation();

        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] Episode COMPLETED - Agent died (Step %d/%d, Health=%.1f%%) - Agent: %s, Strategy: %s"),
            CurrentEpisodeSteps,
            MaxEpisodeSteps,
            currentHealth * 100.0f,
            *GetName(),
            *UEnum::GetValueAsString(CachedCommandedStrategy));

        // Additional validation: Only mark as dead if health is actually 0
        // This prevents false positives from uninitialized health values
        if (currentHealth <= 0.0f)
        {
            return EAgentTrainingStatus::Completed; // Episode complete (agent died)
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[MocTrainer] IsAlive() returned false but Health=%.1f%% > 0! Possible bug in IsAlive_Implementation(). Continuing episode."),
                currentHealth * 100.0f);
            return EAgentTrainingStatus::Running;
        }
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
            ControlledCharacter->GetHealthPercentage_Implementation() * 100.0f));
        Info.Add(TEXT("IsAlive"), ControlledCharacter->IsAlive_Implementation() ? TEXT("true") : TEXT("false"));

        FVector EQSTarget = ControlledCharacter->GetLastEQSTargetLocation();
        if (!EQSTarget.IsZero())
        {
            float DistToTarget = FVector::Dist(
                ControlledCharacter->GetActorLocation(),
                EQSTarget
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
    // Note: UpdateTrainingStatistics() is called in OnCompletion().
    // Do NOT call it here to avoid double-counting episodes and rewards.
    // If OnCompletion() is not called for some code path, the statistics
    // for that episode will be lost, which is preferable to double-counting.

    // Reset per-episode state
    CurrentEpisodeSteps = 0;
    EpisodeReward = 0.0f;
    CachedStepReward = 0.0f;
    bHasNewReward = false;

    // v10.2 FIX: Re-validate agent and character references after reset
    // References may become stale if characters were destroyed/recreated during reset
    if (!MocAgent || !IsValid(MocAgent))
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] MocAgent reference invalid after reset, attempting to re-acquire..."));

        // Try to get agent from the pawn we're controlling
        APawn* ControlledPawn = GetPawn();
        if (ControlledPawn)
        {
            MocAgent = ControlledPawn->FindComponentByClass<UScholaMocAgent>();
            if (MocAgent)
            {
                UE_LOG(LogTemp, Log, TEXT("[MocTrainer] ✓ MocAgent re-acquired successfully"));
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[MocTrainer] ✗ Failed to re-acquire MocAgent - pawn has no ScholaMocAgent component!"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[MocTrainer] ✗ Failed to re-acquire MocAgent - no controlled pawn!"));
        }
    }

    if (!ControlledCharacter || !IsValid(ControlledCharacter))
    {
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] ControlledCharacter reference invalid after reset, attempting to re-acquire..."));

        // Try to get character from the pawn we're controlling
        APawn* ControlledPawn = GetPawn();
        if (ControlledPawn)
        {
            ControlledCharacter = Cast<AMocCharacter>(ControlledPawn);
            if (ControlledCharacter)
            {
                UE_LOG(LogTemp, Log, TEXT("[MocTrainer] ✓ ControlledCharacter re-acquired successfully: %s"), *ControlledCharacter->GetName());
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[MocTrainer] ✗ Failed to re-acquire ControlledCharacter - pawn is not AMocCharacter!"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[MocTrainer] ✗ Failed to re-acquire ControlledCharacter - no controlled pawn!"));
        }
    }

    // Reset observations
    PreviousObservation = FObservation();

    if (ControlledCharacter && IsValid(ControlledCharacter))
    {
        CurrentObservation = GatherStateObservation();
        CachedCommandedStrategy = ControlledCharacter->GetCommandedStrategy();

        UE_LOG(LogTemp, Log, TEXT("[MocTrainer] v10.2 Trainer reset for episode %d - Agent: %s, Strategy: %s"),
            TotalEpisodes + 1,
            *ControlledCharacter->GetName(),
            *UEnum::GetValueAsString(CachedCommandedStrategy));
    }
    else
    {
        CurrentObservation = FObservation();
        UE_LOG(LogTemp, Error, TEXT("[MocTrainer] v10.2 Trainer reset for episode %d - NO VALID CHARACTER!"), TotalEpisodes + 1);
    }

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
