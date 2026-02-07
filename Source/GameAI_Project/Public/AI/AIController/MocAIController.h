// File: AI/MocAIController.h

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "MocAIController.generated.h"

/**
 * MOC v10.1 메인 AI Controller
 * 
 * 역할:
 * - Perception 데이터 수집 및 상태 업데이트
 * - MCTS를 통한 전략 선택 (event-driven)
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


    // ==================== Perception Callbacks ====================
    
    /** Perception 업데이트 콜백 */
    UFUNCTION()
    void OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors);
    
    /** 타겟 감지 콜백 */
    UFUNCTION()
    void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);


    UE_DEPRECATED(5.6, "EQS weights now from centralized policy")
    FEQSWeightParameters GetCurrentEQSWeights();

    /** Blackboard 업데이트 (BT와 통신) - Still used for commanded strategies */
    void UpdateBlackboard(const FTacticalOption& Option,
                         const FEQSWeightParameters& Weights);

    /** Blackboard 업데이트 (EQS weights only) - v10.2 simplified */
    void UpdateBlackboardWeights(const FEQSWeightParameters& Weights);


                         
public:
    // ==================== Blueprint Accessible ====================
    
    /** 현재 전략 타입 가져오기 */
    UFUNCTION(BlueprintCallable, Category="MOC")
    EStrategyType GetCurrentStrategy() const { return CurrentOption.Strategy; }
    
    /** EQS 쿼리 동적 실행 */
    UFUNCTION(BlueprintCallable, Category="MOC")
    FEnvQueryRequest CreateDynamicEQSQuery(const FEQSWeightParameters& Weights);
    
    /** 디버그 정보 출력 */
    UFUNCTION(BlueprintCallable, Category="Debug")
    void DrawDebugInfo();



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

    /** RL Policy Executor (ONNX inference) - Still used for EQS weights */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MOC")
    UMocPolicyExecutor* PolicyExecutor;

    /** EQS Weight Applicator - Still used for spatial reasoning */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MOC")
    UEQSDynamicWeightApplicator* EQSApplicator;

    // ==================== REMOVED (v10.2) ====================
    // The following components moved to centralized Squad Commander:
    // - UMocMCTSPlanner* MCTSPlanner → ASquadManager::TeamMCTSPlanner
    // - UMocWorldModel* WorldModel → ASquadManager::TeamWorldModel
    // - UMocValueNetwork* ValueNetwork → Centralized evaluation
    // - UMocEventMonitor* EventMonitor → ASquadManager::OnCriticalEvent()

    // ==================== Configuration ====================
    
    /** Behavior Tree Asset */
    UPROPERTY(EditDefaultsOnly, Category="AI")
    UBehaviorTree* BehaviorTree;
    
    /** EQS Query Template */
    UPROPERTY(EditDefaultsOnly, Category="EQS")
    UEnvQuery* EQS_TacticalMovement;
    
    /** AI Perception Configs */
    UPROPERTY(EditDefaultsOnly, Category="Perception")
    UAISenseConfig_Sight* SightConfig;
    
    UPROPERTY(EditDefaultsOnly, Category="Perception")
    UAISenseConfig_Hearing* HearingConfig;
    
    UPROPERTY(EditDefaultsOnly, Category="Perception")
    UAISenseConfig_Damage* DamageConfig;

    // ==================== State Management ====================
    
    /** 현재 관측 상태 (52-dim) */
    UPROPERTY(BlueprintReadOnly, Category="State")
    FObservation CurrentObservation;
    
    /** 현재 실행 중인 전술 옵션 */
    UPROPERTY(BlueprintReadOnly, Category="Strategy")
    FTacticalOption CurrentOption;
    
    /** 마지막 리플래닝 시간 */
    float TimeSinceLastReplan;
    
    /** MCTS 계획 캐시 */
    TSharedPtr<FMCTSNode> CachedPlanRoot;
};
