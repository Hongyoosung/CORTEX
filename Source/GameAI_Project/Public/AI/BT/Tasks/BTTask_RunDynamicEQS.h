#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"           
#include "BehaviorTree/BehaviorTreeComponent.h"    
#include "BehaviorTree/BlackboardComponent.h"       
#include "EnvironmentQuery/EnvQueryManager.h"     
#include "EnvironmentQuery/EnvQueryTypes.h"       
#include "EnvironmentQuery/EnvQuery.h"            
#include "BTTask_RunDynamicEQS.generated.h"


UCLASS()
class GAMEAI_PROJECT_API UBTTask_RunDynamicEQS : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_RunDynamicEQS();

    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, 
                                            uint8* NodeMemory) override;

protected:
    void OnQueryFinished(TSharedPtr<FEnvQueryResult> Result);

    UPROPERTY(EditAnywhere, Category="EQS")
    UEnvQuery* QueryTemplate;
    
    UPROPERTY(EditAnywhere, Category="EQS")
    FBlackboardKeySelector EQSResultKey;  // 결과를 저장할 Blackboard 키

private:
    UBehaviorTreeComponent* CachedOwnerComp;
};

