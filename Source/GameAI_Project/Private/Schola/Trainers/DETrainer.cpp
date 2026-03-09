// File: Schola/Trainers/DETrainer.cpp

#include "Schola/Trainers/DETrainer.h"
#include "Schola/Components/DEScholaAgent.h"
#include "Schola/Logging/DEScholaTransitionLogger.h"
#include "Schola/Logging/DETransitionLogger.h"
#include "Schola/DEScholaEnvironment.h"
#include "Characters/DECharacter.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Types/DERewardTypes.h"
#include "AIController.h"
#include "Training/StateStructs/TrainerState.h"
#include "Team/DEMatchManager.h"
#include "Core/DEGameMode.h"
#include "Actors/DECapturePoint.h"

ADETrainer::ADETrainer()
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
    DEAgent = nullptr;
    ControlledCharacter = nullptr;
    TransitionLogger = nullptr;
    CachedMatchManager = nullptr;

    // Default strategy
    CachedCommandedStrategy = EDEStrategyType::Assault;
}

void ADETrainer::InitializeDETrainer(UDEScholaAgent* InAgent)
{
    DEAgent = InAgent;
    if (!DEAgent)
    {
        UE_LOG(LogTemp, Error, TEXT("[DETrainer] Invalid agent reference! Initialization failed."));
        return;
    }

    // Character 참조 획득
    ControlledCharacter = Cast<ADECharacter>(DEAgent->GetOwner());
    if (!ControlledCharacter)
    {
        UE_LOG(LogTemp, Error, TEXT("[DETrainer] Agent owner is not ADECharacter! Initialization failed."));
        return;
    }

    // Transition Logger 초기화
    if (bLogTransitions)
    {
        TransitionLogger = NewObject<UDEScholaTransitionLogger>(this);
        if (TransitionLogger)
        {
            TransitionLogger->Initialize(TransitionLogPath);
            UE_LOG(LogTemp, Log, TEXT("[DETrainer] Transition logging enabled: %s"), *TransitionLogPath);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[DETrainer] Failed to create TransitionLogger!"));
        }
    }

    // Cache DEMatchManager reference (avoids GetAllActorsOfClass in hot paths)
    CachedMatchManager = ControlledCharacter->GetMatchManager();
    if (!CachedMatchManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DETrainer] DEMatchManager not yet available — GatherStateObservation will fall back to world scan"));
    }

    // Cache DEScholaEnvironment — find the one whose OwnedMatchManager matches ours
    {
        TArray<AActor*> EnvActors;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADEScholaEnvironment::StaticClass(), EnvActors);
        for (AActor* Actor : EnvActors)
        {
            if (ADEScholaEnvironment* Env = Cast<ADEScholaEnvironment>(Actor))
            {
                if (Env->GetMatchManager() == CachedMatchManager)
                {
                    CachedScholaEnvironment = Env;
                    break;
                }
            }
        }
        if (!CachedScholaEnvironment)
        {
            UE_LOG(LogTemp, Warning, TEXT("[DETrainer] Could not find owning ADEScholaEnvironment — match termination signal unavailable"));
        }
    }

    // Cache capture point references filtered by EnvID (static actors — populated once)
    const int32 MyEnvID = ControlledCharacter->GetEnvID_Implementation();
    TArray<AActor*> FoundPoints;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADECapturePoint::StaticClass(), FoundPoints);
    CachedCapturePoints.Reset();
    for (AActor* Actor : FoundPoints)
    {
        if (ADECapturePoint* CP = Cast<ADECapturePoint>(Actor))
        {
            if (CP->GetEnvID_Implementation() == MyEnvID)
            {
                CachedCapturePoints.Add(CP);
            }
        }
    }
    UE_LOG(LogTemp, Log, TEXT("[DETrainer] Cached %d capture points for EnvID %d"), CachedCapturePoints.Num(), MyEnvID);

    // 초기 commanded strategy 캐시
    CachedCommandedStrategy = ControlledCharacter->GetCommandedStrategy();

    UE_LOG(LogTemp, Log, TEXT("[DETrainer] v10.2 Executor initialized for agent: %s (Strategy: %s)"),
        *ControlledCharacter->GetName(),
        *UEnum::GetValueAsString(CachedCommandedStrategy));
}

void ADETrainer::BeginPlay()
{
    Super::BeginPlay();

    // v10.2: Strategy는 Squad Commander가 할당
    // Executor는 commanded strategy를 수행하는 데만 집중

    // Initialize observation state
    PreviousObservation = FDEObservation();
    CurrentObservation = GatherStateObservation();

    // === DIAGNOSTIC: Log full pipeline state at startup ===
    UE_LOG(LogTemp, Warning, TEXT("========== [DETrainer] BeginPlay DIAGNOSTIC =========="));
    UE_LOG(LogTemp, Warning, TEXT("[DETrainer] Trainer: %s"), *GetName());
    UE_LOG(LogTemp, Warning, TEXT("[DETrainer] Pawn: %s"), GetPawn() ? *GetPawn()->GetName() : TEXT("NULL"));
    UE_LOG(LogTemp, Warning, TEXT("[DETrainer] ControlledCharacter: %s"), ControlledCharacter ? *ControlledCharacter->GetName() : TEXT("NULL"));
    UE_LOG(LogTemp, Warning, TEXT("[DETrainer] DEAgent: %s"), DEAgent ? *DEAgent->GetName() : TEXT("NULL"));
    UE_LOG(LogTemp, Warning, TEXT("[DETrainer] Pawn == ControlledCharacter? %s"),
        (GetPawn() == ControlledCharacter) ? TEXT("YES") : TEXT("NO - ACTIONS WILL NOT REACH THIS TRAINER!"));
    if (ControlledCharacter)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DETrainer] Character's Controller: %s"),
            ControlledCharacter->GetController() ? *ControlledCharacter->GetController()->GetName() : TEXT("NULL"));
        UE_LOG(LogTemp, Warning, TEXT("[DETrainer] Character's Controller == this? %s"),
            (ControlledCharacter->GetController() == this) ? TEXT("YES") : TEXT("NO - POSSESSION MISMATCH!"));
    }
    UE_LOG(LogTemp, Warning, TEXT("========================================================"));
}

void ADETrainer::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!ControlledCharacter || !ControlledCharacter->IsAlive())
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
            UE_LOG(LogTemp, Log, TEXT("[DETrainer] %s (DEAD): action drained (step %d/%d)"),
                *ControlledCharacter->GetName(), CurrentEpisodeSteps, MaxEpisodeSteps);
        }
        bWasDeadLastTick = true;
        return;
    }

    // Detect respawn: agent was dead last tick, now alive
    if (bWasDeadLastTick)
    {
        bWasDeadLastTick = false;
        UE_LOG(LogTemp, Warning, TEXT("[DETrainer] %s RESPAWN DETECTED - awaiting RL action, Location=%s"),
            *ControlledCharacter->GetName(),
            *ControlledCharacter->GetActorLocation().ToString());
    }

    // Update commanded strategy cache (may change from Squad Commander)
    EDEStrategyType NewStrategy = ControlledCharacter->GetCommandedStrategy();
    if (NewStrategy != CachedCommandedStrategy)
    {
        UE_LOG(LogTemp, Log, TEXT("[DETrainer] Strategy changed: %s -> %s"),
            *UEnum::GetValueAsString(CachedCommandedStrategy),
            *UEnum::GetValueAsString(NewStrategy));
        CachedCommandedStrategy = NewStrategy;
    }

    if (ControlledCharacter->ConsumeNewWeights())
    {
        // New action received from Python — reset freeze watchdog
        if (TicksWithoutNewWeights >= FreezeWatchdogInterval)
        {
            UE_LOG(LogTemp, Warning, TEXT("[DETrainer] FREEZE RESOLVED: %s received new weights after %d ticks (%.1fs frozen)"),
                *ControlledCharacter->GetName(), TicksWithoutNewWeights, TicksWithoutNewWeights * 0.0167f);
        }
        TicksWithoutNewWeights = 0;

        // Actuator가 설정한 최신 가중치 가져오기
        LastAction = ControlledCharacter->GetEQSWeights();
        CurrentEpisodeSteps++;

        // Capture state BEFORE executing action (for reward computation)
        PreviousObservation = CurrentObservation;

        // Movement is triggered by DETacticalParameterActuator::TakeAction() → PerformTacticalAction()

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
                TEXT("[DETrainer] FREEZE WATCHDOG: %s — %d ticks (%.1fs) without new weights | Alive=%s | Steps=%d | bHasNewReward=%s"),
                *ControlledCharacter->GetName(),
                TicksWithoutNewWeights,
                FrozenSeconds,
                ControlledCharacter->IsAlive() ? TEXT("true") : TEXT("false"),
                CurrentEpisodeSteps,
                bHasNewReward ? TEXT("true") : TEXT("false"));

            // Diagnose why weights are not arriving
            UE_LOG(LogTemp, Warning,
                TEXT("[DETrainer] FREEZE DIAG: %s — Controller=%s | Pawn=%s | DEAgent=%s | Health=%.1f%%"),
                *ControlledCharacter->GetName(),
                ControlledCharacter->GetController() ? *ControlledCharacter->GetController()->GetName() : TEXT("NULL"),
                GetPawn() ? *GetPawn()->GetName() : TEXT("NULL"),
                DEAgent ? *DEAgent->GetName() : TEXT("NULL"),
                ControlledCharacter->GetHealthPercentage() * 100.0f);

            // Check if this trainer is actually possessing the character
            UE_LOG(LogTemp, Warning,
                TEXT("[DETrainer] FREEZE DIAG: %s — Trainer(%s) Pawn==%s? %s | ControlledChar==%s"),
                *ControlledCharacter->GetName(),
                *GetName(),
                GetPawn() ? *GetPawn()->GetName() : TEXT("NULL"),
                (GetPawn() == ControlledCharacter) ? TEXT("YES") : TEXT("NO - MISMATCH!"),
                *ControlledCharacter->GetName());
        }
    }

    // Debug visualization
    if (bEnableDebugVisualization)
    {
        DrawTrainingDebug(DeltaTime);
    }
}

// ==================== Schola Interface Implementation ====================

TArray<float> ADETrainer::GetObservation()
{
    // NOTE: This method is NOT called by Schola (Schola uses DETacticalObserver::CollectObservations).
    // It exists as a legacy/utility interface. Tactical execution happens in Tick().

    // Gather current state
    CurrentObservation = GatherStateObservation();

    // Observation 유효성 검사
    if (!ValidateObservation(CurrentObservation))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DETrainer] Observation validation failed, returning zeroed observation"));
        return TArray<float>();
    }

    TArray<float> Observation = CurrentObservation.ToArray();

    // NOTE: This returns FDEObservation::ToArray() = 48-dim base only.
    // Schola uses DETacticalObserver which outputs 54-dim (48 base + 3 composition + 3 strategy one-hot).
    // This path is unused by Schola but kept for legacy/debug use.

    return Observation;
}

float ADETrainer::ComputeReward()
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

        // Log transition — skip dead-agent drain steps (observations are meaningless)
        if (bLogTransitions && TransitionLogger && ControlledCharacter->IsAlive())
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

bool ADETrainer::IsEpisodeDone()
{
    // 종료 조건
    if (CurrentEpisodeSteps >= MaxEpisodeSteps)
    {
        UE_LOG(LogTemp, Log, TEXT("Episode ended: Max steps reached"));
        return true;
    }
    
    return false;
}

void ADETrainer::ResetEpisode()
{
    UE_LOG(LogTemp, Log, TEXT("[DETrainer] Resetting episode. Total reward: %.2f, Steps: %d"),
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
    PreviousObservation = FDEObservation();
    CurrentObservation = GatherStateObservation();
}

// ==================== Helper Functions ====================

FDEObservation ADETrainer::GatherStateObservation()
{
    FDEObservation Obs;

    if (!ControlledCharacter) return Obs;

    // Self state
    Obs.Position = ControlledCharacter->GetActorLocation();
    Obs.EnvironmentOrigin = CachedScholaEnvironment ? CachedScholaEnvironment->GetActorLocation() : FVector::ZeroVector;
    Obs.Health = ControlledCharacter->GetHealthPercentage();
    Obs.Velocity = ControlledCharacter->GetVelocity();
    Obs.WeaponCooldown = ControlledCharacter->GetWeaponCooldown();
    Obs.CurrentStrategy = ControlledCharacter->GetCommandedStrategy();
    Obs.bIsAlive = ControlledCharacter->IsAlive();

    const int32 MyTeamID = ControlledCharacter->GetTeamID_Implementation();
    const FVector MyLocation = ControlledCharacter->GetActorLocation();

    int32 AllyIndex = 0;
    int32 EnemyIndex = 0;

    // Allies — use DEMatchManager cached list (O(1) lookup, no world scan)
    if (CachedMatchManager)
    {
        TArray<ADECharacter*> Allies = CachedMatchManager->GetTeamAgents(MyTeamID);
        for (ADECharacter* Ally : Allies)
        {
            if (!Ally || Ally == ControlledCharacter) continue;
            if (AllyIndex >= 4) break;

            Obs.AllyPositions[AllyIndex] = Ally->GetActorLocation();
            Obs.AllyHealths[AllyIndex] = Ally->GetHealthPercentage();
            AllyIndex++;
        }

        // Enemies — direct line-of-sight only (DEFogOfWarManager deprecated)
        TArray<ADECharacter*> Enemies = CachedMatchManager->GetEnemyAgents(MyTeamID);
        for (ADECharacter* Enemy : Enemies)
        {
            if (!Enemy) continue;
            if (EnemyIndex >= 5) break;

            const float Distance = FVector::Dist(Enemy->GetActorLocation(), MyLocation);
            bool bVisible = false;

            if (Distance < 8000.0f)
            {
                FHitResult HitResult;
                FCollisionQueryParams QueryParams;
                QueryParams.AddIgnoredActor(ControlledCharacter);

                bVisible = !GetWorld()->LineTraceSingleByChannel(
                    HitResult,
                    MyLocation + FVector(0, 0, 90),
                    Enemy->GetActorLocation() + FVector(0, 0, 90),
                    ECC_Visibility,
                    QueryParams
                );
            }

            // Store actual position only when visible; ToArray zeros non-visible slots
            Obs.EnemyPositions[EnemyIndex] = bVisible ? Enemy->GetActorLocation() : FVector::ZeroVector;
            Obs.EnemyVisible[EnemyIndex] = bVisible;
            EnemyIndex++;
        }
    }
    else
    {
        // Fallback: world scan filtered by EnvID (CachedMatchManager unavailable at initialization)
        const int32 MyEnvID = ControlledCharacter->GetEnvID_Implementation();
        TArray<AActor*> AllCharacters;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADECharacter::StaticClass(), AllCharacters);

        for (AActor* Actor : AllCharacters)
        {
            ADECharacter* OtherChar = Cast<ADECharacter>(Actor);
            if (!OtherChar || OtherChar == ControlledCharacter) continue;

            // Filter by EnvID — only observe agents in the same environment
            if (OtherChar->GetEnvID_Implementation() != MyEnvID) continue;

            if (OtherChar->GetTeamID_Implementation() == MyTeamID)
            {
                if (AllyIndex < 4)
                {
                    Obs.AllyPositions[AllyIndex] = OtherChar->GetActorLocation();
                    Obs.AllyHealths[AllyIndex] = OtherChar->GetHealthPercentage();
                    AllyIndex++;
                }
            }
            else
            {
                if (EnemyIndex < 5)
                {
                    const float Distance = FVector::Dist(OtherChar->GetActorLocation(), MyLocation);
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

                    Obs.EnemyPositions[EnemyIndex] = bVisible ? OtherChar->GetActorLocation() : FVector::ZeroVector;
                    Obs.EnemyVisible[EnemyIndex] = bVisible;
                    EnemyIndex++;
                }
            }
        }
    }

    // Map state: per-point ownership relative to this agent's team
    Obs.CapturePointStatuses.Init(0.0f, 5);
    for (ADECapturePoint* CP : CachedCapturePoints)
    {
        if (!CP) continue;
        const int32 Idx = static_cast<int32>(CP->PointID);
        if (Idx < 0 || Idx >= 5) continue;

        const int32 OwnerTeam = CP->GetTeamID_Implementation();
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

float ADETrainer::ComputeCommandedStrategyReward(
    EDEStrategyType CommandedStrategy,
    const FDEObservation& Prev,
    const FDEObservation& Current,
    const FDEEQSWeightParameters& Action
)
{
    // Delegate to DECharacter → UDERewardCalculator.
    // All reward math and momentum state live in the calculator component.
    if (!ControlledCharacter) return 0.0f;
    return ControlledCharacter->ComputeStepReward(CommandedStrategy, Prev, Current, Action);
}

void ADETrainer::LogTransition(
    const FDEObservation& InState,
    EDEStrategyType CommandedStrategy,
    const FDEEQSWeightParameters& Action,
    float Reward,
    const FDEObservation& NextState,
    bool bDone
)
{
    if (!TransitionLogger) return;

    // Convert to UDEScholaTransitionLogger format
    TArray<float> StateBefore = InState.ToArray();
    TArray<float> StateAfter = NextState.ToArray();

    // Create FDETacticalOption from commanded strategy
    // v10.2: Individual agents don't choose options, they execute commanded strategies
    // We log the commanded strategy as a tactical option for consistency
    FDETacticalOption Option;
    Option.Strategy = CommandedStrategy;
    Option.Confidence = 1.0f; // Full confidence in commanded strategy

    // Convert scalar reward to FDECompositeReward
    // v10.2: For simplicity, put the entire reward in ObjectiveScore
    FDECompositeReward CompositeReward;
    CompositeReward.WinProb = 0.0f;
    CompositeReward.HealthDelta = (NextState.Health - InState.Health);
    CompositeReward.ObjectiveScore = Reward;


    // Record transition
    TransitionLogger->RecordTransition(StateBefore, Option, CompositeReward, StateAfter, bDone);
}

void ADETrainer::DrawTrainingDebug(float DeltaTime)
{
    if (!ControlledCharacter || !GetWorld()) return;

    // Skip world-space debug visuals while a spectator is viewing via the HUD camera
    if (ControlledCharacter->bIsBeingObserved) return;

    float DrawDuration = DeltaTime * 1.2f;

    FVector CharLocation = ControlledCharacter->GetActorLocation();

    // === Agent Info Text ===
    // Check if training override is active
    FString StrategyInfo = UEnum::GetValueAsString(CachedCommandedStrategy);
    if (DEAgent && DEAgent->bUseTrainingStrategyOverride)
    {
        StrategyInfo += TEXT(" [TRAINING OVERRIDE]");
    }

    /*FString DebugText = FString::Printf(
        TEXT("Strategy: %s\n")
        TEXT("Steps: %d / %d\n")
        TEXT("Episode Reward: %.2f\n")
        TEXT("Total Episodes: %d\n")
        TEXT("---EQS Weights---\n")
        TEXT("EnemyObj: %.2f | AllyObj: %.2f\n")
        TEXT("Cover: %.2f | Visibility: %.2f\n")
        TEXT("AllyProx: %.2f | Range: %.2f\n")
        *StrategyInfo,
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
    );*/

    FString DebugText = FString::Printf(
        TEXT("Steps: %d / %d\n")
        TEXT("Episode Reward: %.2f\n")
        TEXT("Total Episodes: %d\n"),
        CurrentEpisodeSteps,
        MaxEpisodeSteps,
        EpisodeReward,
        TotalEpisodes
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
    case EDEStrategyType::Assault:
        StrategyColor = FColor::Orange;
        break;
    case EDEStrategyType::Defend:
        StrategyColor = FColor::Blue;
        break;
    case EDEStrategyType::Support:
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

EAgentTrainingStatus ADETrainer::ComputeStatus()
{
    // v10.2 FIX: Added detailed logging to track episode completion causes

    // Check termination conditions
    if (CurrentEpisodeSteps >= MaxEpisodeSteps)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DETrainer] Episode TRUNCATED - MaxSteps reached (Step %d/%d) - Agent: %s"),
            CurrentEpisodeSteps, MaxEpisodeSteps, *GetName());
        return EAgentTrainingStatus::Truncated; // Episode truncated due to max steps
    }

    // v10.2 FIX: Use IsValid() to guard against dangling pointers,
    // and distinguish "not initialized yet" from "agent actually died"
    if (!ControlledCharacter || !IsValid(ControlledCharacter))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DETrainer] ComputeStatus: ControlledCharacter is %s - returning Running to avoid false completion"),
            ControlledCharacter ? TEXT("INVALID (dangling)") : TEXT("NULL (not initialized)"));
        return EAgentTrainingStatus::Running; // Don't trigger completion for uninitialized state
    }

    // v10.2 FIX: Grace period to prevent false death detection during initialization
    // Don't check death status until we've taken at least a few actions
    const int32 InitializationGracePeriod = 3; // Wait for 3 steps before checking death

    if (CurrentEpisodeSteps < InitializationGracePeriod)
    {
        // During initialization, only return Running unless character is explicitly invalid
        UE_LOG(LogTemp, Verbose, TEXT("[DETrainer] In grace period (Step %d/%d) - skipping death check"),
            CurrentEpisodeSteps, InitializationGracePeriod);
        return EAgentTrainingStatus::Running;
    }

    // Agent death does NOT end the episode.
    // In v10.2 team-based architecture, agents stay dead until their entire team
    // is eliminated, then the team respawns as a group via DEMatchManager::ProcessRespawnQueue().
    // The episode only ends on match termination (time expired, score limit) or max steps.
    if (!ControlledCharacter->IsAlive())
    {
        // Agent is dead — continue episode, let DEMatchManager handle group respawn
        return EAgentTrainingStatus::Running;
    }

    // Match termination: score threshold reached, timeout, etc.
    // Only signal Truncated after bHasNewReward == false, meaning ComputeReward() has
    // already been called and drained the terminal win/loss reward from FDERewardState.
    // This guarantees Python receives the terminal reward in the last reported step.
    if (CachedScholaEnvironment && IsValid(CachedScholaEnvironment))
    {
        const EDEMatchState MatchState = CachedScholaEnvironment->GetMatchState();
        const bool bMatchOver = (MatchState != EDEMatchState::InProgress &&
                                 MatchState != EDEMatchState::WaitingToStart);
        if (bMatchOver && !bHasNewReward)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[DETrainer] Episode TRUNCATED — Match ended (State=%d) after %d steps — Agent: %s"),
                static_cast<int32>(MatchState), CurrentEpisodeSteps, *GetName());
            return EAgentTrainingStatus::Truncated;
        }
    }

    return EAgentTrainingStatus::Running;
}

void ADETrainer::GetInfo(TMap<FString, FString>& Info)
{
    // Match termination signal — Python reads this to end the episode when UE5 decides match is over
    bool bMatchOver = CachedScholaEnvironment && IsValid(CachedScholaEnvironment) &&
                      (CachedScholaEnvironment->GetMatchState() != EDEMatchState::InProgress &&
                       CachedScholaEnvironment->GetMatchState() != EDEMatchState::WaitingToStart);
    Info.Add(TEXT("MatchEnded"), bMatchOver ? TEXT("true") : TEXT("false"));
    if (bMatchOver)
    {
        Info.Add(TEXT("MatchResult"), FString::FromInt(static_cast<int32>(CachedScholaEnvironment->GetMatchState())));
    }

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
            ControlledCharacter->GetHealthPercentage() * 100.0f));
        Info.Add(TEXT("IsAlive"), ControlledCharacter->IsAlive() ? TEXT("true") : TEXT("false"));

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

}

void ADETrainer::ResetTrainer()
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
    if (!DEAgent || !IsValid(DEAgent))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DETrainer] DEAgent reference invalid after reset, attempting to re-acquire..."));

        // Try to get agent from the pawn we're controlling
        APawn* ControlledPawn = GetPawn();
        if (ControlledPawn)
        {
            DEAgent = ControlledPawn->FindComponentByClass<UDEScholaAgent>();
            if (DEAgent)
            {
                UE_LOG(LogTemp, Log, TEXT("[DETrainer] ✓ DEAgent re-acquired successfully"));
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[DETrainer] ✗ Failed to re-acquire DEAgent - pawn has no DEScholaAgent component!"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[DETrainer] ✗ Failed to re-acquire DEAgent - no controlled pawn!"));
        }
    }

    if (!ControlledCharacter || !IsValid(ControlledCharacter))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DETrainer] ControlledCharacter reference invalid after reset, attempting to re-acquire..."));

        // Try to get character from the pawn we're controlling
        APawn* ControlledPawn = GetPawn();
        if (ControlledPawn)
        {
            ControlledCharacter = Cast<ADECharacter>(ControlledPawn);
            if (ControlledCharacter)
            {
                UE_LOG(LogTemp, Log, TEXT("[DETrainer] ✓ ControlledCharacter re-acquired successfully: %s"), *ControlledCharacter->GetName());
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[DETrainer] ✗ Failed to re-acquire ControlledCharacter - pawn is not ADECharacter!"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[DETrainer] ✗ Failed to re-acquire ControlledCharacter - no controlled pawn!"));
        }
    }

    // Reset observations
    PreviousObservation = FDEObservation();

    if (ControlledCharacter && IsValid(ControlledCharacter))
    {
        CurrentObservation = GatherStateObservation();
        CachedCommandedStrategy = ControlledCharacter->GetCommandedStrategy();

        UE_LOG(LogTemp, Log, TEXT("[DETrainer] v10.2 Trainer reset for episode %d - Agent: %s, Strategy: %s"),
            TotalEpisodes + 1,
            *ControlledCharacter->GetName(),
            *UEnum::GetValueAsString(CachedCommandedStrategy));
    }
    else
    {
        CurrentObservation = FDEObservation();
        UE_LOG(LogTemp, Error, TEXT("[DETrainer] v10.2 Trainer reset for episode %d - NO VALID CHARACTER!"), TotalEpisodes + 1);
    }

}

void ADETrainer::OnCompletion()
{
    // Episode completion callback
    UE_LOG(LogTemp, Log, TEXT("[DETrainer] Episode completed. Final reward: %.2f, Steps: %d"),
        EpisodeReward, CurrentEpisodeSteps);

    // 훈련 통계 업데이트
    UpdateTrainingStatistics();

    // Flush transition logs if enabled
    if (bLogTransitions && TransitionLogger)
    {
        TransitionLogger->FlushToDisk();
        UE_LOG(LogTemp, Log, TEXT("[DETrainer] Transition logs flushed to disk"));
    }
}

// ==================== Utility Functions ====================

bool ADETrainer::ValidateEQSWeights(const FDEEQSWeightParameters& Weights) const
{
    // 모든 가중치가 유효 범위 내에 있는지 확인
    bool bValid = true;

    auto CheckRange = [&bValid](float Value, const FString& Name)
    {
        if (FMath::IsNaN(Value) || FMath::IsFinite(Value) == false)
        {
            UE_LOG(LogTemp, Error, TEXT("[DETrainer] Invalid EQS weight %s: %f"), *Name, Value);
            bValid = false;
        }
    };

    CheckRange(Weights.EnemyObjectiveProximity, TEXT("EnemyObjectiveProximity"));
    CheckRange(Weights.AllyObjectiveProximity, TEXT("AllyObjectiveProximity"));
    CheckRange(Weights.CoverDensity, TEXT("CoverDensity"));
    CheckRange(Weights.EnemyVisibility, TEXT("EnemyVisibility"));
    CheckRange(Weights.AllyProximity, TEXT("AllyProximity"));
    CheckRange(Weights.CombatRange, TEXT("CombatRange"));

    return bValid;
}

bool ADETrainer::ValidateObservation(const FDEObservation& Obs) const
{
    // Observation의 주요 필드가 유효한지 확인
    if (FMath::IsNaN(Obs.Health) || Obs.Health < 0.0f || Obs.Health > 1.0f)
    {
        UE_LOG(LogTemp, Error, TEXT("[DETrainer] Invalid health value: %f"), Obs.Health);
        return false;
    }

    if (!Obs.Position.ContainsNaN() == false || !Obs.Position.IsNormalized())
    {
        // Position can be any valid vector
        if (Obs.Position.ContainsNaN())
        {
            UE_LOG(LogTemp, Error, TEXT("[DETrainer] Position contains NaN"));
            return false;
        }
    }

    if (Obs.AllyPositions.Num() != 4 || Obs.AllyHealths.Num() != 4)
    {
        UE_LOG(LogTemp, Error, TEXT("[DETrainer] Ally data array size mismatch"));
        return false;
    }

    if (Obs.EnemyPositions.Num() != 5 || Obs.EnemyVisible.Num() != 5)
    {
        UE_LOG(LogTemp, Error, TEXT("[DETrainer] Enemy data array size mismatch"));
        return false;
    }

    return true;
}

void ADETrainer::UpdateTrainingStatistics()
{
    TotalEpisodes++;
    CumulativeReward += EpisodeReward;

    // 이동 평균으로 평균 에피소드 길이 업데이트
    float Alpha = 0.1f; // 지수 이동 평균 계수
    AverageEpisodeLength = Alpha * CurrentEpisodeSteps + (1.0f - Alpha) * AverageEpisodeLength;

    UE_LOG(LogTemp, Log, TEXT("[DETrainer] Episode %d completed - Reward: %.2f, Steps: %d, Avg Length: %.1f"),
        TotalEpisodes, EpisodeReward, CurrentEpisodeSteps, AverageEpisodeLength);
}


void ADETrainer::GetTrainingStats(int32& OutEpisodes, float& OutAvgReward, float& OutAvgLength) const
{
    OutEpisodes = TotalEpisodes;
    OutAvgReward = TotalEpisodes > 0 ? CumulativeReward / TotalEpisodes : 0.0f;
    OutAvgLength = AverageEpisodeLength;
}

void ADETrainer::LogRewardBreakdown() const
{
    if (!ControlledCharacter) return;


}
