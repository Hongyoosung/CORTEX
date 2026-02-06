#pragma once

#include "BehaviorTree/BTTaskNode.h"        
#include "BehaviorTree/BehaviorTreeComponent.h" 
#include "AIController.h"                  
#include "EnvironmentQuery/EnvQueryManager.h" 
#include "EnvironmentQuery/EnvQueryTypes.h"   
#include "EnvironmentQuery/EnvQuery.h"        
#include "DrawDebugHelpers.h"             


#include "AI/AIController/MocAIController.h"                            
#include "AI/EQS/EQSDynamicWeightApplicator.h"   

#include "BTTask_MoveToEQSLocation.generated.h"


UCLASS()
class UBTTask_MoveToEQSLocation : public UBTTaskNode {
    GENERATED_BODY()
    
public:
    virtual EBTNodeResult::Type ExecuteTask(
        UBehaviorTreeComponent& OwnerComp, 
        uint8* NodeMemory
    ) override {
        AMocAIController* AIController = Cast<AMocAIController>(
            OwnerComp.GetAIOwner()
        );
        
        // 1. RL Policy에서 현재 상황에 맞는 가중치 가져오기
        FRLObservation CurrentObs = AIController->GetObservation();
        FEQSWeightParameters RLWeights = AIController->PolicyExecutor->GetEQSWeights(
            CurrentObs
        );
        
        // 2. EQS Query 생성 및 가중치 주입
        FEnvQueryRequest QueryRequest = WeightApplicator->CreateDynamicQuery(
            EQS_TacticalMovement,  // Blueprint 에셋
            RLWeights
        );
        
        // 3. EQS 실행 (비동기)
        FQueryFinishedSignature QueryFinishedDelegate;
        QueryFinishedDelegate.BindUObject(
            this, 
            &UBTTask_MoveToEQSLocation::OnQueryFinished
        );
        
        QueryRequest.Execute(
            EEnvQueryRunMode::SingleResult,  // 최고 점수 1개만
            QueryFinishedDelegate
        );
        
        return EBTNodeResult::InProgress;
    }
    
private:
    void OnQueryFinished(
        TSharedPtr<FEnvQueryResult> Result
    ) {
        if (Result->IsSuccessful()) {
            FVector TargetLocation = Result->GetItemAsLocation(0);
            
            // 4. UE5 Navigation System으로 이동
            AAIController* AIController = GetAIController();
            AIController->MoveToLocation(
                TargetLocation,
                AcceptanceRadius = 100.0f,
                bStopOnOverlap = true,
                bUsePathfinding = true
            );
            
            // 디버그 시각화
            DrawDebugSphere(
                GetWorld(), 
                TargetLocation, 
                50.0f, 
                12, 
                FColor::Green, 
                false, 
                2.0f
            );
        }
    }
    
    UPROPERTY(EditAnywhere, Category="EQS")
    UEnvQuery* EQS_TacticalMovement;
    
    UEQSDynamicWeightApplicator* WeightApplicator;
};
