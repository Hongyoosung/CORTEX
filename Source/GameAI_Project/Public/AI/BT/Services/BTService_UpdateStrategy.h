#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"         
#include "BehaviorTree/BlackboardComponent.h" 
#include "BehaviorTree/BehaviorTreeComponent.h" 
#include "AIController.h"                    
         
#include "AI/AIController/MocAIController.h"   
#include "Types/MocTypes.h"
#include "BTService_UpdateStrategy.generated.h"



UCLASS()
class GAMEAI_PROJECT_API UBTService_UpdateStrategy : public UBTService
{
    GENERATED_BODY()

protected:
    virtual void TickNode(UBehaviorTreeComponent& OwnerComp, 
                         uint8* NodeMemory, 
                         float DeltaSeconds) override
    {
        AMocAIController* AIController = Cast<AMocAIController>(
            OwnerComp.GetAIOwner()
        );
        
        if (!AIController) return;
        
        // AIController의 Tick에서 이미 업데이트됨
        // 여기서는 BT가 최신 Blackboard 값을 사용하는지만 확인
        UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
        
        // 전략 변경 감지 시 BT 일부 재시작 가능
        EStrategyType CurrentStrategy = static_cast<EStrategyType>(
            BB->GetValueAsEnum(TEXT("CurrentStrategy"))
        );
        
        if (CurrentStrategy != PreviousStrategy)
        {
            UE_LOG(LogCortex, Log, TEXT("Strategy changed: %d -> %d"), 
                PreviousStrategy, CurrentStrategy);
            PreviousStrategy = CurrentStrategy;
        }
    }

private:
    EStrategyType PreviousStrategy = EStrategyType::Defend;
};
