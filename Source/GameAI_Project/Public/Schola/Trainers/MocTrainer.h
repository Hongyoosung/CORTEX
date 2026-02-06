// File: Schola/Trainers/MocTrainer.h

#pragma once

#include "CoreMinimal.h"
#include "Training/AbstractTrainer.h"
#include "Perception/AIPerceptionComponent.h"
#include "MocTrainer.generated.h"

class UScholaMocAgent;
class AMocCharacter;

/**
 * MOC v10.1 전용 Schola Trainer
 * 
 * 역할:
 * - Phase 1 Training: RL Policy의 Option-Conditioned 학습
 * - Python (RLlib/Gym)과 UE5 사이의 브릿지
 * - Multi-Head Policy를 위한 Option Sampling
 * - EQS 가중치 형태의 Action 수집 및 적용
 * - Strategy-specific Reward 계산
 * 
 * Action Space: Box(8) - Continuous EQS weights [-1, 1]
 * Observation Space: Box(57) - [State(52), Option(1), Target(3), Duration(1)]
 */
UCLASS()
class GAMEAI_PROJECT_API AMocTrainer : public AAbstractTrainer
{
    GENERATED_BODY()

public:
    AMocTrainer();

    // ==================== Lifecycle Overrides ====================
    
    virtual void Initialize(UScholaAgentComponent* InAgent) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    // ==================== Schola Interface ====================
    
    /**
     * Python에서 받은 Action 적용
     * @param ActionValues - [8-dim] EQS Weights: 
     *   [EnemyObjProx, AllyObjProx, Cover, Visibility, AllyProx, Range, Pickup, Height]
     */
    virtual void ApplyAction(const TArray<float>& ActionValues) override;
    
    /**
     * 현재 Observation 반환 (Python으로 전송)
     * @return 57-dim vector: [State, Option, Target, Duration]
     */
    virtual TArray<float> GetObservation() override;
    
    /**
     * v10.1 Strategy-Specific Reward 계산
     * @return Scalar reward (스칼라화된 복합 보상)
     */
    virtual float ComputeReward() override;
    
    /**
     * Episode 종료 체크
     * @return true if episode should terminate
     */
    virtual bool IsEpisodeDone() override;
    
    /**
     * Episode 리셋
     */
    virtual void ResetEpisode() override;



protected:
// ==================== Helper Functions ====================
    
    /** 52-dim State 수집 */
    FObservation GatherStateObservation();
    
    /** 새로운 Option 샘플링 */
    void SampleNewOption();
    
    /** Target 위치 선택 (전략에 맞는 거점) */
    FVector SelectTargetForOption(EStrategyType Strategy);
    
    /** State Volatility 계산 */
    float CalculateVolatility(const FObservation& Prev, const FObservation& Current);
    
    /** Strategy-specific Reward 계산 */
    float ComputeOptionReward(EStrategyType Strategy, 
                             const FObservation& Prev, 
                             const FObservation& Current,
                             const FEQSWeightParameters& Action);
    
    /** Transition 로깅 (World Model 학습용) */
    void LogTransition(const FObservation& State, 
                      EStrategyType Option,
                      const FEQSWeightParameters& Action,
                      float Reward,
                      const FObservation& NextState,
                      bool bDone);
    
    /** EQS 가중치를 Character의 이동에 실제 적용 */
    void ApplyEQSWeightsToCharacter(const FEQSWeightParameters& Weights);



public:
    // ==================== Training Configuration ====================
    
    /** Option Sampling: 에피소드 시작 시 전략 할당 */
    UPROPERTY(EditAnywhere, Category="Training|Option")
    TArray<EStrategyType> AvailableStrategies = {
        EStrategyType::Attack, 
        EStrategyType::Defend
    };
    
    /** Option 전환 주기 (steps) */
    UPROPERTY(EditAnywhere, Category="Training|Option")
    int32 OptionSwitchInterval = 200;
    
    /** Volatility 기반 Option 전환 임계값 */
    UPROPERTY(EditAnywhere, Category="Training|Option")
    float VolatilityThreshold = 0.5f;
    
    /** Episode 최대 길이 */
    UPROPERTY(EditAnywhere, Category="Training")
    int32 MaxEpisodeSteps = 3000;
    
    /** Transition 로깅 활성화 (World Model 학습용) */
    UPROPERTY(EditAnywhere, Category="Training|Logging")
    bool bLogTransitions = true;
    
    /** 로그 파일 경로 */
    UPROPERTY(EditAnywhere, Category="Training|Logging")
    FString TransitionLogPath = TEXT("Saved/Transitions/cortex_phase1.json");

protected:
    // ==================== Internal State ====================
    
    /** 캐스팅된 Agent 참조 */
    UPROPERTY()
    UScholaMocAgent* MocAgent;
    
    /** 제어 중인 Character */
    UPROPERTY()
    AMocCharacter* ControlledCharacter;
    
    /** 현재 할당된 전략 */
    UPROPERTY(BlueprintReadOnly, Category="State")
    EStrategyType CurrentOption;
    
    /** 현재 타겟 위치 */
    UPROPERTY(BlueprintReadOnly, Category="State")
    FVector CurrentTarget;
    
    /** Option 지속 시간 */
    UPROPERTY(BlueprintReadOnly, Category="State")
    float CurrentOptionDuration;
    
    /** Episode 통계 */
    int32 CurrentEpisodeSteps;
    float EpisodeReward;
    
    /** 이전 상태 (Reward 계산용) */
    FObservation PreviousObservation;
    FObservation CurrentObservation;
    
    /** 마지막 Action */
    FEQSWeightParameters LastAction;
    
    /** Transition 로거 */
    TSharedPtr<FTransitionLogger> TransitionLogger;

    

public:
    // ==================== Debug ====================
    
    UFUNCTION(BlueprintCallable, Category="Debug")
    void DrawTrainingDebug();
};
