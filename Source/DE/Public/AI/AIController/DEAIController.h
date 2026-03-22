// File: AI/DEAIController.h

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "DEAIController.generated.h"


class UAIPerceptionComponent;
class UBehaviorTreeComponent;
class UBlackboardComponent;
class UBehaviorTree;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UAISenseConfig_Damage;


/**
 * DE AI Controller (Perception + Combat BT only)
 *
 * Role:
 * - Collects Perception data and updates the Blackboard (TargetEnemy, HasTarget)
 * - Executes Behavior Trees (Combat BT only)
 *
 * Movement/EQS weight inference is handled by DEScholaAgent (UInferenceComponent).
 */
UCLASS()
class DE_API ADEAIController : public AAIController
{
    GENERATED_BODY()

public:
    ADEAIController(const FObjectInitializer& ObjectInitializer);

protected:
    virtual void BeginPlay() override;
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;


public:

    //========================================
    // Perception Callbacks
    //========================================
    /** Callback Perception Update */
    UFUNCTION()
    void OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors);

    /** Callback Target Perception */
    UFUNCTION()
    void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);


protected:
    //==================== Components ====================

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Perception")
    TObjectPtr<UAIPerceptionComponent> AIPerception;

    //==================== Configuration ====================

    /** Behavior Tree Asset */
    UPROPERTY(EditDefaultsOnly, Category="AI")
    TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

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
