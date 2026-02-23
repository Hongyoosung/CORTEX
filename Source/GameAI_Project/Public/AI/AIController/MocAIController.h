// File: AI/MocAIController.h

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "MocAIController.generated.h"


class UAIPerceptionComponent;
class UBehaviorTreeComponent;
class UBlackboardComponent;
class UBehaviorTree;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UAISenseConfig_Damage;


/**
 * MOC v10.2 AI Controller (Perception + Combat BT only)
 *
 * 역할:
 * - Perception 데이터 수집 및 Blackboard 업데이트 (TargetEnemy, HasTarget)
 * - Behavior Tree 실행 (Combat BT only)
 *
 * Movement/EQS weight inference is handled by ScholaMocAgent (UInferenceComponent).
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
    virtual void OnUnPossess() override;


public:
    // ==================== Perception Callbacks ====================

    /** Perception 업데이트 콜백 */
    UFUNCTION()
    void OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors);

    /** 타겟 감지 콜백 */
    UFUNCTION()
    void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);


protected:
    //==================== Components ====================

    /** AI 지각 시스템 (시야, 청각, 피격 감지) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Perception")
    UAIPerceptionComponent* AIPerception;

    //==================== Configuration ====================

    /** Behavior Tree Asset */
    UPROPERTY(EditDefaultsOnly, Category="AI")
    UBehaviorTree* BehaviorTreeAsset;

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
};
