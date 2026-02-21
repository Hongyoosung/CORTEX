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
        // Dead agents MUST still drain any queued action so Schola's multi-agent
        // step barrier can advance. Schola waits for all agents to acknowledge their
        // action before releasing the barrier and sending the next batch to Python.
        // If dead agents skip ConsumeNewWeights(), the barrier never releases and
        // all agents (including alive ones) stop receiving actions.
        if (ControlledCharacter && ControlledCharacter->ConsumeNewWeights())
        {
            // Action drained — signal that ComputeReward() can be called this step.
            // ComputeReward() will return CachedStepReward (last death-penalty value).
            bHasNewReward = true;
            TicksWithoutNewWeights = 0;
            // Count dead-agent drains toward MaxEpisodeSteps.
            // Without this, dead agents never reach MaxEpisodeSteps while alive agents do,
            // so ComputeStatus() returns Running for the dead agent indefinitely.
            // That blocks AllAgentsThink()'s AllDone=true check, preventing the SAME_STEP
            // auto-reset from firing and leaving alive agents permanently stuck in Truncated.
            CurrentEpisodeSteps++;
            UE_LOG(LogTemp, Log, TEXT("[MocTrainer] %s (DEAD): action drained (step %d/%d)"),
                *ControlledCharacter->GetName(), CurrentEpisodeSteps, MaxEpisodeSteps);
        }
        bWasDeadLastTick = true;
        return;
    }

    // Detect respawn: agent was dead last tick, now alive
    if (bWasDeadLastTick)
    {
        bWasDeadLastTick = false;
        UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] %s RESPAWN DETECTED - awaiting RL action, Location=%s"),
            *ControlledCharacter->GetName(),
            *ControlledCharacter->GetActorLocation().ToString());
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

    if (ControlledCharacter->ConsumeNewWeights())
    {
        // New action received from Python — reset freeze watchdog
        if (TicksWithoutNewWeights >= FreezeWatchdogInterval)
        {
            UE_LOG(LogTemp, Warning, TEXT("[MocTrainer] FREEZE RESOLVED: %s received new weights after %d ticks (%.1fs frozen)"),
                *ControlledCharacter->GetName(), TicksWithoutNewWeights, TicksWithoutNewWeights * 0.0167f);
        }
        TicksWithoutNewWeights = 0;

        // Actuator가 설정한 최신 가중치 가져오기
        LastAction = ControlledCharacter->GetEQSWeights();
        CurrentEpisodeSteps++;

        // Capture state BEFORE executing action (for reward computation)
        PreviousObservation = CurrentObservation;

        // 물리/로직적 행동 실행
        ControlledCharacter->PerformTacticalAction();

        // Signal that a new reward needs to be computed
        // ComputeReward() will compute and cache the reward on first call
        bHasNewReward = true;
    }
    else
    {
        // No new weights this tick — increment watchdog counter
        TicksWithoutNewWeights++;

        // Log at regular intervals to diagnose freeze
        if (TicksWithoutNewWeights % FreezeWatchdogInterval == 0)
        {
            const float FrozenSeconds = TicksWithoutNewWeights * 0.0167f;
            UE_LOG(LogTemp, Warning,
                TEXT("[MocTrainer] FREEZE WATCHDOG: %s — %d ticks (%.1fs) without new weights | Alive=%s | Steps=%d | bHasNewReward=%s"),
                *ControlledCharacter->GetName(),
                TicksWithoutNewWeights,
                FrozenSeconds,
                ControlledCharacter->IsAlive_Implementation() ? TEXT("true") : TEXT("false"),
                CurrentEpisodeSteps,
                bHasNewReward ? TEXT("true") : TEXT("false"));

            // Diagnose why weights are not arriving
            UE_LOG(LogTemp, Warning,
                TEXT("[MocTrainer] FREEZE DIAG: %s — AIController=%s | Health=%.1f%% | LastAction=[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]"),
                *ControlledCharacter->GetName(),
                ControlledCharacter->GetController() ? TEXT("Valid") : TEXT("NULL"),
                ControlledCharacter->GetHealthPercentage_Implementation() * 100.0f,
                LastAction.EnemyObjectiveProximity, LastAction.AllyObjectiveProximity,
                LastAction.CoverDensity, LastAction.EnemyVisibility,
                LastAction.AllyProximity, LastAction.CombatRange, LastAction.PickupProximity);
        }
    }

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
    if (Observation.Num() != 49)
    {
        UE_LOG(LogTemp, Error, TEXT("[MocTrainer] Observation dimension mismatch: %d (expected 49)"),
            Observation.Num());
    }

    return Observation;
}

float AMocTrainer::ComputeReward()
{
    // v10.2 FIX: IDEMPOTENT reward computation.
    // Schola calls ComputeReward() MULTIPLE TIMES per Python step (once per game tick).
    // Without idempotency, the first call computes the real reward and updates state,
    // then subsequent calls see no state change and return ~0 (just TimePenalty).
    // EpisodeReward accumulated all calls (correct total), but Python received only
    // the LAST call's near-zero value.
    //
    // Fix: Compute reward ONCE when bHasNewReward is set (by Tick's ConsumeNewWeights),
    // cache it, and return the cached value on all subsequent calls.

    if (!ControlledCharacter || !IsValid(ControlledCharacter))
    {
        return 0.0f;
    }

    if (bHasNewReward)
    {
        // Gather LATEST observation (state after action effects have propagated)
        CurrentObservation = GatherStateObservation();

        // Compute reward from state transition (PreviousObs → CurrentObs)
        CachedStepReward = ComputeCommandedStrategyReward(
            CachedCommandedStrategy,
            PreviousObservation,
            CurrentObservation,
            LastAction
        );

        // Accumulate ONCE per action
        EpisodeReward += CachedStepReward;
        CumulativeLifetimeReward += CachedStepReward;

        // Log transition
        if (bLogTransitions && TransitionLogger)
        {
            LogTransition(
                PreviousObservation,
                CachedCommandedStrategy,
                LastAction,
                CachedStepReward,
                CurrentObservation,
                IsEpisodeDone()
            );
        }

        // Mark as consumed — subsequent calls return the same cached value
        bHasNewReward = false;
    }

    return CachedStepReward;
}

bool AMocTrainer::IsEpisodeDone()
{
    // 종료 조건
    if (CurrentEpisodeSteps >= MaxEpisodeSteps)
    {
        UE_LOG(LogTemp, Log, TEXT("Episode ended: Max steps reached"));
        return true;
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
    Obs.Health = ControlledCharacter->GetHealthPercentage_Implementation();
    Obs.Velocity = ControlledCharacter->GetVelocity();
    Obs.WeaponCooldown = ControlledCharacter->GetWeaponCooldown_Implementation();
    Obs.CurrentStrategy = ControlledCharacter->GetCommandedStrategy();
    Obs.bIsAlive = ControlledCharacter->IsAlive_Implementation();

    const int32 MyTeamID = ControlledCharacter->GetTeamID_Implementation();
    const FVector MyLocation = ControlledCharacter->GetActorLocation();

    // Scan all characters for allies and enemies
    TArray<AActor*> AllCharacters;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMocCharacter::StaticClass(), AllCharacters);

    int32 AllyIndex = 0;
    int32 EnemyIndex = 0;

    for (AActor* Actor : AllCharacters)
    {
        AMocCharacter* OtherChar = Cast<AMocCharacter>(Actor);
        if (!OtherChar || OtherChar == ControlledCharacter) continue;

        if (OtherChar->GetTeamID_Implementation() == MyTeamID)
        {
            // Ally — always known, no LoS required
            if (AllyIndex < 4)
            {
                Obs.AllyPositions[AllyIndex] = OtherChar->GetActorLocation();
                Obs.AllyHealths[AllyIndex] = OtherChar->GetHealthPercentage_Implementation();
                AllyIndex++;
            }
        }
        else
        {
            // Enemy — direct line-of-sight only
            if (EnemyIndex < 5)
            {
                const FVector ToEnemy = OtherChar->GetActorLocation() - MyLocation;
                const float Distance = ToEnemy.Size();
                bool bVisible = false;

                if (Distance < 8000.0f)
                {
                    FHitResult HitResult;
                    FCollisionQueryParams QueryParams;
                    QueryParams.AddIgnoredActor(ControlledCharacter);

                    bVisible = !GetWorld()->LineTraceSingleByChannel(
                        HitResult,
                        MyLocation + FVector(0, 0, 90),
                        OtherChar->GetActorLocation() + FVector(0, 0, 90),
                        ECC_Visibility,
                        QueryParams
                    );
                }

                // Store actual position only when visible; ToArray zeros non-visible slots
                Obs.EnemyPositions[EnemyIndex] = bVisible ? OtherChar->GetActorLocation() : FVector::ZeroVector;
                Obs.EnemyVisible[EnemyIndex] = bVisible;
                EnemyIndex++;
            }
        }
    }

    // Map state: per-point ownership relative to this agent's team
    Obs.CapturePointStatuses.Init(0.0f, 5);
    for (ACapturePoint* CP : CachedCapturePoints)
    {
        if (!CP) continue;
        const int32 Idx = static_cast<int32>(CP->PointID);
        if (Idx < 0 || Idx >= 5) continue;

        const int32 OwnerTeam = CP->GetOwningTeamID();
        if (OwnerTeam == MyTeamID)
        {
            Obs.CapturePointStatuses[Idx] = 1.0f;
        }
        else if (OwnerTeam != -1)
        {
            Obs.CapturePointStatuses[Idx] = -1.0f;
        }
        // neutral → stays 0.0f
    }

    return Obs;
}

void AMocTrainer::HandleCombat()
{
    if (!ControlledCharacter) return;

    UWeaponComponent* Weapon = ControlledCharacter->GetWeaponComponent();
    if (!Weapon || !Weapon->CanFire()) return;

    AActor* ClosestEnemy = nullptr;
    float ClosestDistance = FLT_MAX;

    const int32 MyTeamID = ControlledCharacter->GetTeamID_Implementation();
    const FVector MyLocation = ControlledCharacter->GetActorLocation();

    // Direct scan: fire at the nearest currently-visible enemy
    TArray<AActor*> AllCharacters;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMocCharacter::StaticClass(), AllCharacters);

    for (AActor* Actor : AllCharacters)
    {
        AMocCharacter* Enemy = Cast<AMocCharacter>(Actor);
        if (!Enemy || Enemy == ControlledCharacter) continue;
        if (Enemy->GetTeamID_Implementation() == MyTeamID) continue;
        if (!Enemy->IsAlive_Implementation()) continue;

        const float Distance = FVector::Dist(Enemy->GetActorLocation(), MyLocation);
        if (Distance > 8000.0f) continue;

        FHitResult HitResult;
        FCollisionQueryParams QueryParams;
        QueryParams.AddIgnoredActor(ControlledCharacter);

        const bool bVisible = !GetWorld()->LineTraceSingleByChannel(
            HitResult,
            MyLocation + FVector(0, 0, 90),
            Enemy->GetActorLocation() + FVector(0, 0, 90),
            ECC_Visibility,
            QueryParams
        );

        if (bVisible && Distance < ClosestDistance)
        {
            ClosestEnemy = Enemy;
            ClosestDistance = Distance;
        }
    }

    if (ClosestEnemy)
    {
        Weapon->FireAtTarget(ClosestEnemy, true);
    }
}

float AMocTrainer::ComputeCommandedStrategyReward(
    EStrategyType CommandedStrategy,
    const FObservation& Prev,
    const FObservation& Current,
    const FEQSWeightParameters& Action
)
{
    // Delegate to MocCharacter → UMocRewardCalculator.
    // All reward math and momentum state live in the calculator component.
    if (!ControlledCharacter) return 0.0f;
    return ControlledCharacter->ComputeStepReward(CommandedStrategy, Prev, Current, Action);
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

    // Agent death does NOT end the episode.
    // In v10.2 team-based architecture, agents stay dead until their entire team
    // is eliminated, then the team respawns as a group via TeamManager::ProcessRespawnQueue().
    // The episode only ends on match termination (time expired, score limit) or max steps.
    // Returning Completed here would trigger ResetEnvironment() → ResetMatch() → ResetTeams(),
    // which bypasses the group respawn system and causes ghost agents (invisible but active).
    if (!ControlledCharacter->IsAlive_Implementation())
    {
        // Agent is dead — continue episode, let TeamManager handle group respawn
        return EAgentTrainingStatus::Running;
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
    Info.Add(TEXT("CumulativeLifetimeReward"), FString::Printf(TEXT("%.6f"), CumulativeLifetimeReward));
    Info.Add(TEXT("LastStepReward"), FString::Printf(TEXT("%.6f"), CachedStepReward));
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
        FRewardBreakdown Breakdown = ControlledCharacter->ComputeRewardBreakdown(
            CachedCommandedStrategy,
            PreviousObservation,
            CurrentObservation
        );
        Info.Add(TEXT("RewardPosition"), FString::Printf(TEXT("%.3f"), Breakdown.PositionComponent));
        Info.Add(TEXT("RewardHealth"), FString::Printf(TEXT("%.3f"), Breakdown.HealthComponent));
        Info.Add(TEXT("RewardObjective"), FString::Printf(TEXT("%.3f"), Breakdown.ObjectiveComponent));
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

    if (Obs.AllyPositions.Num() != 4 || Obs.AllyHealths.Num() != 4)
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


void AMocTrainer::GetTrainingStats(int32& OutEpisodes, float& OutAvgReward, float& OutAvgLength) const
{
    OutEpisodes = TotalEpisodes;
    OutAvgReward = TotalEpisodes > 0 ? CumulativeReward / TotalEpisodes : 0.0f;
    OutAvgLength = AverageEpisodeLength;
}

void AMocTrainer::LogRewardBreakdown() const
{
    if (!ControlledCharacter) return;

    FRewardBreakdown Breakdown = ControlledCharacter->ComputeRewardBreakdown(
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
