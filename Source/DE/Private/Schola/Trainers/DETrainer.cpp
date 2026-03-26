// File: Schola/Trainers/DETrainer.cpp

#include "Schola/Trainers/DETrainer.h"
#include "Schola/Components/DEScholaAgent.h"
#include "Schola/Logging/DEScholaTransitionLogger.h"
#include "Schola/Logging/DETransitionLogger.h"
#include "Schola/DEScholaEnvironment.h"
#include "Characters/DEAgent.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Types/DERewardTypes.h"
#include "AIController.h"
#include "Team/DEMatchManager.h"
#include "Core/DETrainingGameMode.h"
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

    // Default class
    CachedCommandedClass = EDEClassType::Strike;
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
    ControlledCharacter = Cast<ADEAgent>(DEAgent->GetOwner());
    if (!ControlledCharacter)
    {
        UE_LOG(LogTemp, Error, TEXT("[DETrainer] Agent owner is not ADEAgent! Initialization failed."));
        return;
    }

    // Transition Logger 초기화
    if (bLogTransitions)
    {
        TransitionLogger = NewObject<UDEScholaTransitionLogger>(this);
        if (TransitionLogger)
        {
            TransitionLogger->Initialize(TransitionLogPath);
            //UE_LOG(LogTemp, Log, TEXT("[DETrainer] Transition logging enabled: %s"), *TC->TransitionLogPath.ToString());
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
        UE_LOG(LogTemp, Warning, TEXT("[DETrainer] DEMatchManager not yet available — GatherStateSnapshot will fall back to world scan"));
    }
    else
    {
        CachedMatchManager->OnMatchConditionMet.AddDynamic(this, &ADETrainer::OnMatchEnded);
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

    // 초기 commanded class 캐시
    CachedCommandedClass = ControlledCharacter->GetCommandedClass();

    UE_LOG(LogTemp, Log, TEXT("[DETrainer] v10.2 Executor initialized for agent: %s (Class: %s)"),
        *ControlledCharacter->GetName(),
        *UEnum::GetValueAsString(CachedCommandedClass));
}

void ADETrainer::BeginPlay()
{
    Super::BeginPlay();

    // v10.2: Class는 Squad Commander가 할당
    // Executor는 commanded class를 수행하는 데만 집중

    // Initialize observation state
    PreviousObservation = FDEAgentSnapshot();
    CurrentObservation = GatherStateSnapshot();

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
            UE_LOG(LogTemp, Log, TEXT("[DETrainer] %s (DEAD): action drained (step %d)"),
                *ControlledCharacter->GetName(), CurrentEpisodeSteps);
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

    // Update commanded class cache (may change from Squad Commander)
    EDEClassType NewClass = ControlledCharacter->GetCommandedClass();
    if (NewClass != CachedCommandedClass)
    {
        UE_LOG(LogTemp, Log, TEXT("[DETrainer] Class changed: %s -> %s"),
            *UEnum::GetValueAsString(CachedCommandedClass),
            *UEnum::GetValueAsString(NewClass));
        CachedCommandedClass = NewClass;
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
            //const float FrozenSeconds = TicksWithoutNewWeights * 0.0167f;
            //UE_LOG(LogTemp, Warning,
            //    TEXT("[DETrainer] FREEZE WATCHDOG: %s — %d ticks (%.1fs) without new weights | Alive=%s | Steps=%d | bHasNewReward=%s"),
            //    *ControlledCharacter->GetName(),
            //    TicksWithoutNewWeights,
            //    FrozenSeconds,
            //    ControlledCharacter->IsAlive() ? TEXT("true") : TEXT("false"),
            //    CurrentEpisodeSteps,
            //    bHasNewReward ? TEXT("true") : TEXT("false"));

            //// Diagnose why weights are not arriving
            //UE_LOG(LogTemp, Warning,
            //    TEXT("[DETrainer] FREEZE DIAG: %s — Controller=%s | Pawn=%s | DEAgent=%s | Health=%.1f%%"),
            //    *ControlledCharacter->GetName(),
            //    ControlledCharacter->GetController() ? *ControlledCharacter->GetController()->GetName() : TEXT("NULL"),
            //    GetPawn() ? *GetPawn()->GetName() : TEXT("NULL"),
            //    DEAgent ? *DEAgent->GetName() : TEXT("NULL"),
            //    ControlledCharacter->GetHealthPercentage() * 100.0f);

            //// Check if this trainer is actually possessing the character
            //UE_LOG(LogTemp, Warning,
            //    TEXT("[DETrainer] FREEZE DIAG: %s — Trainer(%s) Pawn==%s? %s | ControlledChar==%s"),
            //    *ControlledCharacter->GetName(),
            //    *GetName(),
            //    GetPawn() ? *GetPawn()->GetName() : TEXT("NULL"),
            //    (GetPawn() == ControlledCharacter) ? TEXT("YES") : TEXT("NO - MISMATCH!"),
            //    *ControlledCharacter->GetName());
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
    // NOTE: Not called by Schola — DETacticalObserver::CollectObservations() handles observations.
    // Kept as a no-op stub to satisfy any external references.
    return TArray<float>();
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
        // Gather LATEST snapshot (state after action effects have propagated)
        CurrentObservation = GatherStateSnapshot();

        // Compute reward from state transition (PreviousObs → CurrentObs)
        CachedStepReward = ComputeCommandedClassReward(
            CachedCommandedClass,
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
                CachedCommandedClass,
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
    // Primary: DEMatchManager fired OnMatchConditionMet (score threshold / timeout)
    if (bMatchEnded)
        return true;

    // Safety net: should never fire in normal play (set MaxEpisodeSteps=9999 in editor)
    if (CurrentEpisodeSteps >= MaxEpisodeSteps)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DETrainer] Safety-net step limit reached (%d) — "
            "DEMatchManager did not end the episode. Check MaxMatchDuration / WinScoreThreshold."),
            MaxEpisodeSteps);
        return true;
    }

    return false;
}

void ADETrainer::OnMatchEnded(EDEMatchState WinnerState, int32 WinningTeamID)
{
    bMatchEnded = true;
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
    bMatchEnded = false;

    // v10.2: Squad Commander가 새로운 전략을 할당
    if (ControlledCharacter)
    {
        CachedCommandedClass = ControlledCharacter->GetCommandedClass();
    }

    // 상태 초기화
    PreviousObservation = FDEAgentSnapshot();
    CurrentObservation = GatherStateSnapshot();
}

// ==================== Helper Functions ====================

FDEAgentSnapshot ADETrainer::GatherStateSnapshot()
{
    FDEAgentSnapshot Obs;

    if (!ControlledCharacter) return Obs;

    // Self state
    Obs.Position = ControlledCharacter->GetActorLocation();
    Obs.Health = ControlledCharacter->GetHealthPercentage();
    Obs.bIsAlive = ControlledCharacter->IsAlive();

    const int32 MyTeamID = ControlledCharacter->GetTeamID_Implementation();
    const FVector MyLocation = ControlledCharacter->GetActorLocation();

    int32 AllyIndex = 0;
    int32 EnemyIndex = 0;

    // Allies — use DEMatchManager cached list (O(1) lookup, no world scan)
    if (CachedMatchManager)
    {
        TArray<ADEAgent*> Allies = CachedMatchManager->GetTeamAgents(MyTeamID);
        for (ADEAgent* Ally : Allies)
        {
            if (!Ally || Ally == ControlledCharacter) continue;
            if (AllyIndex >= 4) break;

            Obs.AllyPositions[AllyIndex] = Ally->GetActorLocation();
            Obs.AllyHealths[AllyIndex] = Ally->GetHealthPercentage();
            AllyIndex++;
        }

        // Enemies — direct line-of-sight only (DEFogOfWarManager deprecated)
        TArray<ADEAgent*> Enemies = CachedMatchManager->GetEnemyAgents(MyTeamID);
        for (ADEAgent* Enemy : Enemies)
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
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADEAgent::StaticClass(), AllCharacters);

        for (AActor* Actor : AllCharacters)
        {
            ADEAgent* OtherChar = Cast<ADEAgent>(Actor);
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

float ADETrainer::ComputeCommandedClassReward(
    EDEClassType CommandedClass,
    const FDEAgentSnapshot& Prev,
    const FDEAgentSnapshot& Current,
    const FDEEQSWeightParameters& Action
)
{
    // Delegate to DEAgent → UDERewardCalculator.
    // All reward math and momentum state live in the calculator component.
    if (!ControlledCharacter) return 0.0f;
    return ControlledCharacter->ComputeStepReward(CommandedClass, Prev, Current, Action);
}

void ADETrainer::LogTransition(
    const FDEAgentSnapshot& InState,
    EDEClassType CommandedClass,
    const FDEEQSWeightParameters& Action,
    float Reward,
    const FDEAgentSnapshot& NextState,
    bool bDone
)
{
    if (!TransitionLogger) return;

    // Transition logger uses TArray<float> state arrays — not used for NN input,
    // just for offline data collection. Pass empty arrays as placeholder.
    TArray<float> StateBefore;
    TArray<float> StateAfter;

    // Create FDETacticalOption from commanded class
    // v10.2: Individual agents don't choose options, they execute commanded classes
    // We log the commanded class as a tactical option for consistency
    FDETacticalOption Option;
    Option.Class = CommandedClass;
    Option.Confidence = 1.0f; // Full confidence in commanded class

    // Convert scalar reward to FDECompositeReward
    // v10.2: For simplicity, put the entire reward in ObjectiveScore
    FDECompositeReward CompositeReward;
    CompositeReward.WinProb = 0.0f;
    CompositeReward.HealthDelta = NextState.Health - InState.Health;
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

    FString DebugText = FString::Printf(
        TEXT("Steps: %d / %d\n")
        TEXT("Episode Reward: %.2f\n")
        TEXT("Total Episodes: %d\n")
        TEXT("---EQS Weights---\n")
        TEXT("EnemyObj: %.2f | AllyObj: %.2f\n")
        TEXT("Cover: %.2f | Visibility: %.2f\n")
        TEXT("AllyProx: %.2f | Range: %.2f\n"),
        CurrentEpisodeSteps,
        MaxEpisodeSteps,
        EpisodeReward,
        TotalEpisodes,
        LastAction.EnemyObjectiveProximity,
        LastAction.AllyObjectiveProximity,
        LastAction.CoverDensity,
        LastAction.EnemyVisibility,
        LastAction.AllyProximity,
        LastAction.CombatRange
    );

    const int32 MaxStepsDebug = MaxEpisodeSteps;
    FString DebugText2 = FString::Printf(
        TEXT("Steps: %d / %d\n")
        TEXT("Episode Reward: %.2f\n")
        TEXT("Total Episodes: %d\n"),
        CurrentEpisodeSteps,
        MaxStepsDebug,
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
        const FVector& AllyPos = CurrentObservation.AllyPositions[i];
        if (!AllyPos.IsZero())
        {
            DrawDebugSphere(GetWorld(), AllyPos, 50.0f, 12, FColor::Green, false, DrawDuration);
        }
    }

    // === Enemies (Red - visible only) ===
    for (int32 i = 0; i < CurrentObservation.EnemyPositions.Num(); ++i)
    {
        if (i < CurrentObservation.EnemyVisible.Num() && CurrentObservation.EnemyVisible[i]
            && !CurrentObservation.EnemyPositions[i].IsZero())
        {
            const FVector& EnemyPos = CurrentObservation.EnemyPositions[i];
            DrawDebugSphere(GetWorld(), EnemyPos, 50.0f, 12, FColor::Red, false, DrawDuration);
            DrawDebugLine(GetWorld(), CharLocation + FVector(0, 0, 90), EnemyPos + FVector(0, 0, 90),
                FColor::Red, false, DrawDuration);
        }
    }

    // === Class-specific indicators ===
    FColor ClassColor;
    switch (CachedCommandedClass)
    {
    case EDEClassType::Strike:
        ClassColor = FColor::Orange;
        break;
    case EDEClassType::Vanguard:
        ClassColor = FColor::Blue;
        break;
    case EDEClassType::Support:
        ClassColor = FColor::Purple;
        break;
    default:
        ClassColor = FColor::White;
        break;
    }

    DrawDebugSphere(
        GetWorld(),
        CharLocation,
        150.0f,
        8,
        ClassColor,
        false,
        0.0f, 
        0,
        3.0f
    );
}

// ==================== AAbstractTrainer Pure Virtual Implementations ====================

EAgentTrainingStatus ADETrainer::ComputeStatus()
{
    // Once we've already signalled completion for this episode, return Running
    // so Schola doesn't keep sending repeated trunc signals to Python.
    // The reset path (ResetTrainer) clears bEpisodeCompleted.
    if (bEpisodeCompleted)
    {
        return EAgentTrainingStatus::Running;
    }

    // Check termination conditions
    const int32 MaxStepsCS = MaxEpisodeSteps;
    if (CurrentEpisodeSteps >= MaxStepsCS)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DETrainer] Episode TRUNCATED - MaxSteps reached (Step %d/%d) - Agent: %s"),
            CurrentEpisodeSteps, MaxStepsCS, *GetName());
        bEpisodeCompleted = true;
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
            bEpisodeCompleted = true;
            if (MatchState == EDEMatchState::TeamWon)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[DETrainer] Episode TERMINATED — Team won after %d steps — Agent: %s"),
                    CurrentEpisodeSteps, *GetName());
                return EAgentTrainingStatus::Completed;
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[DETrainer] Episode TRUNCATED — Match ended (State=%d) after %d steps — Agent: %s"),
                    static_cast<int32>(MatchState), CurrentEpisodeSteps, *GetName());
                return EAgentTrainingStatus::Truncated;
            }
        }
    }

    return EAgentTrainingStatus::Running;
}

FDETeamWorldState ADETrainer::BuildTeamWorldState() const
{
    FDETeamWorldState State;

    if (!CachedMatchManager || !ControlledCharacter)
    {
        return State; // Return default-initialized (zeros/defaults)
    }

    const int32 InTeamID = ControlledCharacter->GetTeamID_Implementation();
    const int32 EnemyTeamID = (InTeamID == 0) ? 1 : 0;

    // ── Friendly units ──────────────────────────────────────────────────
    const TArray<ADEAgent*>& Friendlies = CachedMatchManager->GetTeamAgents(InTeamID);
    for (int32 i = 0; i < 5; ++i)
    {
        if (i < Friendlies.Num() && Friendlies[i] && IsValid(Friendlies[i]))
        {
            State.FriendlyPositions[i] = Friendlies[i]->GetActorLocation();
            State.FriendlyHealths[i]   = Friendlies[i]->GetHealthPercentage();
            State.FriendlyClasses[i]   = Friendlies[i]->GetCommandedClass();
            State.FriendlyCooldowns[i] = Friendlies[i]->GetWeaponCooldown();
            State.FriendlyAlive[i]     = Friendlies[i]->IsAlive();
        }
        else
        {
            State.FriendlyPositions[i] = FVector::ZeroVector;
            State.FriendlyHealths[i]   = 0.0f;
            State.FriendlyClasses[i]   = EDEClassType::Strike;
            State.FriendlyCooldowns[i] = 0.0f;
            State.FriendlyAlive[i]     = false;
        }
    }

    // ── Enemy estimates ─────────────────────────────────────────────────
    const TArray<ADEAgent*> Enemies = CachedMatchManager->GetEnemyAgents(InTeamID);
    for (int32 i = 0; i < 5; ++i)
    {
        if (i < Enemies.Num() && Enemies[i] && IsValid(Enemies[i]))
        {
            State.EnemyPositions[i]   = Enemies[i]->GetActorLocation();
            State.EnemyConfidences[i] = 1.0f; // Direct observation — full confidence
            State.EnemyHealths[i]     = Enemies[i]->GetHealthPercentage();
            State.EnemyAlive[i]       = Enemies[i]->IsAlive();
        }
        else
        {
            State.EnemyPositions[i]   = FVector::ZeroVector;
            State.EnemyConfidences[i] = 0.0f;
            State.EnemyHealths[i]     = 0.0f;
            State.EnemyAlive[i]       = false;
        }
    }

    // ── Map state ───────────────────────────────────────────────────────
    const TArray<ADECapturePoint*>& CPs = CachedMatchManager->GetCapturePoints();
    for (int32 i = 0; i < 5; ++i)
    {
        if (i < CPs.Num() && CPs[i] && IsValid(CPs[i]))
        {
            // Map ownership relative to this agent's team:
            // +1 = friendly, -1 = enemy, 0 = neutral
            const int32 InOwner = CPs[i]->GetOwnership();
            if (InOwner == InTeamID)
                State.CapturePointOwnership[i] = 1;
            else if (InOwner == EnemyTeamID)
                State.CapturePointOwnership[i] = -1;
            else
                State.CapturePointOwnership[i] = 0;
        }
    }

    // Time remaining — read from match manager if available
    if (CachedScholaEnvironment)
    {
        const float MaxTime = static_cast<float>(MaxEpisodeSteps);
        State.TimeRemaining = MaxTime > 0.0f
            ? FMath::Clamp(1.0f - static_cast<float>(CurrentEpisodeSteps) / MaxTime, 0.0f, 1.0f)
            : 1.0f;
    }

    return State;
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

    // Team scores and winner (for win rate tracking in Python)
    if (CachedMatchManager)
    {
        Info.Add(TEXT("TeamScore0"), FString::FromInt(CachedMatchManager->GetTeamScore(0)));
        Info.Add(TEXT("TeamScore1"), FString::FromInt(CachedMatchManager->GetTeamScore(1)));
        Info.Add(TEXT("WinnerTeamID"), FString::FromInt(CachedMatchManager->GetFinalWinnerTeamID()));
    }

    // ── MAPPO: global state for centralized critic ──────────────────────
    // Serialize FDETeamWorldState::ToTensor() as comma-separated floats.
    // Python env_wrapper reads this under key "global_state" and appends
    // the 71-dim vector to each agent's 226-dim obs → 297-dim MAPPO obs.
    {
        FDETeamWorldState TeamState = BuildTeamWorldState();
        TArray<float> Tensor = TeamState.ToTensor();

        FString GlobalStateStr;
        GlobalStateStr.Reserve(Tensor.Num() * 8); // ~8 chars per float
        for (int32 i = 0; i < Tensor.Num(); ++i)
        {
            if (i > 0) GlobalStateStr.AppendChar(TEXT(','));
            GlobalStateStr.Append(FString::Printf(TEXT("%.6f"), Tensor[i]));
        }
        Info.Add(TEXT("global_state"), GlobalStateStr);
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
        Info.Add(TEXT("Class"), UEnum::GetValueAsString(CachedCommandedClass));
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
    bEpisodeCompleted = false;

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
            ControlledCharacter = Cast<ADEAgent>(ControlledPawn);
            if (ControlledCharacter)
            {
                UE_LOG(LogTemp, Log, TEXT("[DETrainer] ✓ ControlledCharacter re-acquired successfully: %s"), *ControlledCharacter->GetName());
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[DETrainer] ✗ Failed to re-acquire ControlledCharacter - pawn is not ADEAgent!"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[DETrainer] ✗ Failed to re-acquire ControlledCharacter - no controlled pawn!"));
        }
    }

    // Reset observations
    PreviousObservation = FDEAgentSnapshot();

    if (ControlledCharacter && IsValid(ControlledCharacter))
    {
        CurrentObservation = GatherStateSnapshot();
        CachedCommandedClass = ControlledCharacter->GetCommandedClass();

        UE_LOG(LogTemp, Log, TEXT("[DETrainer] v10.2 Trainer reset for episode %d - Agent: %s, Class: %s"),
            TotalEpisodes + 1,
            *ControlledCharacter->GetName(),
            *UEnum::GetValueAsString(CachedCommandedClass));
    }
    else
    {
        CurrentObservation = FDEAgentSnapshot();
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
