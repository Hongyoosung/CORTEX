// File: Schola/Trainers/MocTrainer.h

#pragma once

#include "CoreMinimal.h"
#include "Training/AbstractTrainer.h"
#include "Perception/AIPerceptionComponent.h"
#include "Types/RewardTypes.h"
#include "Types/EQSTypes.h"
#include "Types/ObservationTypes.h"
#include "Types/StrategyTypes.h"
#include "MocTrainer.generated.h"


class UScholaMocAgent;
class UScholaTransitionLogger;
class AMocCharacter;
class ACapturePoint;
class ATeamManager;
class AScholaEnvironment;


/**
 * MOC v10.2 전용 Schola Trainer
 *
 * 역할:
 * - Executor Agent RL Policy 학습 (Commander의 전략 지시를 EQS 가중치로 변환)
 * - Python (RLlib/Gym)과 UE5 사이의 브릿지
 * - Commanded Strategy를 조건으로 한 Policy 학습
 * - EQS 가중치 형태의 Action 수집 및 적용
 * - Team-level Reward 계산 (Squad Commander에서 할당받은 전략 기준)
 *
 * Action Space: Box(6) - Continuous EQS weights [-1, 1]
 * Observation Space: Box(52) - Agent State only (Strategy는 외부에서 commanded)
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API AMocTrainer : public AAbstractTrainer
{
    GENERATED_BODY()

public:
    AMocTrainer();

    // ==================== Lifecycle Overrides ====================

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    /** Initialize the trainer after spawning */
    void InitializeMocTrainer(UScholaMocAgent* InAgent);

    // ==================== AAbstractTrainer Overrides ====================

    virtual float ComputeReward() override;
    virtual EAgentTrainingStatus ComputeStatus() override;
    virtual void GetInfo(TMap<FString, FString>& Info) override;
    virtual void ResetTrainer() override;
    virtual void OnCompletion() override;

    // ==================== Schola Interface ====================

    /**
     * 현재 Observation 반환 (Python으로 전송)
     * @return 52-dim vector: Agent state (commanded strategy는 Character에서 이미 설정됨)
     */
    TArray<float> GetObservation();

    /**
     * Episode 종료 체크
     * @return true if episode should terminate
     */
    bool IsEpisodeDone();

    /**
     * Episode 리셋 (legacy Schola interface)
     */
    void ResetEpisode();

    

protected:
// ==================== Helper Functions ====================

    /** 52-dim State 수집 */
    FObservation GatherStateObservation();

    /** Team-aligned Reward 계산 (commanded strategy 수행 품질) */
    float ComputeCommandedStrategyReward(EStrategyType CommandedStrategy,
                                         const FObservation& Prev,
                                         const FObservation& Current,
                                         const FEQSWeightParameters& Action);

    /** Transition 로깅 (World Model 학습용) */
    void LogTransition(const FObservation& InState,
                      EStrategyType CommandedStrategy,
                      const FEQSWeightParameters& Action,
                      float Reward,
                      const FObservation& NextState,
                      bool bDone);


public:
    // ==================== Training Configuration ====================

    /** Episode 최대 길이 */
    UPROPERTY(EditAnywhere, Category="Training")
    int32 MaxEpisodeSteps = 3000;



    /** Transition 로깅 활성화 (World Model 학습용) */
    UPROPERTY(EditAnywhere, Category="Training|Logging")
    bool bLogTransitions = true;

    /** 로그 파일 경로 */
    UPROPERTY(EditAnywhere, Category="Training|Logging")
    FString TransitionLogPath = TEXT("Saved/Transitions/cortex_v10.2_executor.json");

    /** 비주얼 디버깅 활성화 */
    UPROPERTY(EditAnywhere, Category="Debug")
    bool bEnableDebugVisualization = false;

    // NOTE: Reward shaping parameters have moved to UMocRewardCalculator (on MocCharacter).
    // Edit them in the RewardCalculator component of the MocCharacter Blueprint.

protected:
    // ==================== Internal State ====================

    /** 캐스팅된 Agent 참조 */
    UPROPERTY()
    UScholaMocAgent* MocAgent;

    /** 제어 중인 Character */
    UPROPERTY()
    AMocCharacter* ControlledCharacter;

    /** TeamManager reference — cached for O(1) ally/enemy list access */
    UPROPERTY()
    ATeamManager* CachedTeamManager;

    /** ScholaEnvironment that owns this trainer's arena — cached for match state queries */
    UPROPERTY()
    AScholaEnvironment* CachedScholaEnvironment = nullptr;

    /** Episode 통계 */
    int32 CurrentEpisodeSteps;
    float EpisodeReward;

    /** 이전 상태 (Reward 계산용) */
    FObservation PreviousObservation;
    FObservation CurrentObservation;

    /** 마지막 Action */
    FEQSWeightParameters LastAction;

    /** Cached reward from last action step (prevents re-computation on every tick) */
    float CachedStepReward = 0.0f;
    bool bHasNewReward = false;

    /** Tracks dead→alive transition for post-respawn diagnostics */
    bool bWasDeadLastTick = false;

    /**
     * Monotonically increasing lifetime reward counter.
     * NEVER reset — Python computes per-step rewards as deltas.
     * This bypasses Schola's reward channel which consumes ComputeReward()
     * return values before they reach Python via gRPC.
     */
    float CumulativeLifetimeReward = 0.0f;

    /** Transition 로거 */
    UPROPERTY()
    UScholaTransitionLogger* TransitionLogger;

    // ==================== Training Statistics ====================

    /** 총 에피소드 수 */
    int32 TotalEpisodes;

    /** 누적 보상 (전체 에피소드) */
    float CumulativeReward;

    /** 평균 에피소드 길이 */
    float AverageEpisodeLength;

    /** 현재 commanded strategy 캐시 */
    EStrategyType CachedCommandedStrategy;

    /** Cached capture point references for observation gathering */
    UPROPERTY()
    TArray<ACapturePoint*> CachedCapturePoints;

    /** Freeze watchdog: ticks elapsed since the last new action was received from Python */
    int32 TicksWithoutNewWeights = 0;

    /** How many ticks between freeze warning logs (300 ticks ≈ 5 s at 60 Hz) */
    static constexpr int32 FreezeWatchdogInterval = 300;


public:
    // ==================== Debug & Utilities ====================

    /** 현재 훈련 통계 반환 */
    UFUNCTION(BlueprintCallable, Category="Training|Stats")
    void GetTrainingStats(int32& OutEpisodes, float& OutAvgReward, float& OutAvgLength) const;

    /** 현재 보상 breakdown 로깅 */
    UFUNCTION(BlueprintCallable, Category="Debug")
    void LogRewardBreakdown() const;

    /** 비주얼 디버깅 그리기 */
    UFUNCTION(BlueprintCallable, Category="Debug")
    void DrawTrainingDebug(float DeltaTime);

    /** EQS 가중치 유효성 검사 */
    bool ValidateEQSWeights(const FEQSWeightParameters& Weights) const;

    /** Observation 유효성 검사 */
    bool ValidateObservation(const FObservation& Obs) const;

private:
    /** 훈련 통계 업데이트 */
    void UpdateTrainingStatistics();
};
