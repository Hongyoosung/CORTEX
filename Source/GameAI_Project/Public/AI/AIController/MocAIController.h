// File: AI/MocAIController.h

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Types/StrategyTypes.h"
#include "Types/ObservationTypes.h"
#include "MocAIController.generated.h"


class UAIPerceptionComponent;
class UBehaviorTreeComponent;
class UBlackboardComponent;
class UBehaviorTree;
class UEnvQuery;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UAISenseConfig_Damage;
class UMocPolicyExecutor;
struct FEQSWeightParameters;
struct FEnvQueryRequest;
struct FObservation;


/**
 * MOC v10.2 AI Controller (Executor Layer)
 *
 * 역할:
 * - Perception 데이터 수집 및 상태 업데이트
 * - Squad Commander로부터 전략 명령 수신
 * - RL Policy를 통한 EQS 가중치 생성
 * - Behavior Tree 실행 제어
 */
UCLASS()
class GAMEAI_PROJECT_API AMocAIController : public AAIController
{
    GENERATED_BODY()

public:
    AMocAIController(const FObjectInitializer& ObjectInitializer);

protected:
    virtual void BeginPlay() override;
    virtual void OnPossess(APawn* InPawn) override;
    virtual void Tick(float DeltaTime) override;


public:
    // ==================== Perception Callbacks ====================
    
    /** Perception 업데이트 콜백 */
    UFUNCTION()
    void OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors);
    
    /** 타겟 감지 콜백 */
    UFUNCTION()
    void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);



    /** Blackboard 업데이트 (EQS weights only) - v10.2 */
    void UpdateBlackboardWeights(const FEQSWeightParameters& Weights);

    /**
     * Build local observation from perception data (v10.2)
     * Gathers 52-dim observation: self state, allies, enemies, map state
     * Based on UMocTacticalObserver::GatherBaseObservation()
     */
    FObservation BuildObservationFromPerception();

    // ==================== Blueprint Accessible ====================

    /** 현재 전략 타입 가져오기 */
    EStrategyType GetCurrentStrategy() const { return CurrentOption.Strategy; }

    /**
     * EQS 쿼리 동적 실행 (v10.2 consolidated)
     * RL Policy 출력을 EQS 가중치로 변환하여 Query 생성
     */
    FEnvQueryRequest CreateDynamicEQSQuery(const FEQSWeightParameters& Weights) const;



protected:
// ==================== Components ====================
    
    /** AI 지각 시스템 (시야, 청각, 피격 감지) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Perception")
    UAIPerceptionComponent* AIPerception;
    
    /** Behavior Tree 실행기 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI")
    UBehaviorTreeComponent* BehaviorTreeComp;
    
    /** Blackboard 데이터 저장소 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AI")
    UBlackboardComponent* BlackboardComp;
    
    // ==================== MOC Custom Components ====================

    /** RL Policy Executor (ONNX inference) - Generates EQS weights from commanded strategy */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MOC")
    UMocPolicyExecutor* PolicyExecutor;


    // ==================== Configuration ====================
    
    /** Behavior Tree Asset */
    UPROPERTY(EditDefaultsOnly, Category="AI")
    UBehaviorTree* BehaviorTree;
    
    /** EQS Query Template */
    UPROPERTY(EditDefaultsOnly, Category="EQS")
    const UEnvQuery* EQS_TacticalMovement;
    
    /** AI Perception Configs */
    UPROPERTY(EditDefaultsOnly, Category="Perception")
    UAISenseConfig_Sight* SightConfig;

    UPROPERTY(EditDefaultsOnly, Category="Perception")
    UAISenseConfig_Hearing* HearingConfig;

    UPROPERTY(EditDefaultsOnly, Category="Perception")
    UAISenseConfig_Damage* DamageConfig;

    /** Enable debug visualization */
    UPROPERTY(EditAnywhere, Category="Debug")
    bool bShowDebugInfo = false;

    // ==================== State Management ====================

    /** 현재 관측 상태 (52-dim) */
    UPROPERTY(BlueprintReadOnly, Category="State")
    FObservation CurrentObservation;

    /** 현재 실행 중인 전술 옵션 */
    UPROPERTY(BlueprintReadOnly, Category="Strategy")
    FTacticalOption CurrentOption;

    /** 마지막 리플래닝 시간 */
    float TimeSinceLastReplan;
};
