#pragma once

#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AIController.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "DrawDebugHelpers.h"

#include "AI/AIController/MocAIController.h"

#include "BTTask_MoveToEQSLocation.generated.h"


UCLASS()
class GAMEAI_PROJECT_API UBTTask_MoveToEQSLocation : public UBTTaskNode {

    GENERATED_BODY()
    
public:
    virtual EBTNodeResult::Type ExecuteTask(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory
    ) override {
        AMocAIController* AIController = Cast<AMocAIController>(
            OwnerComp.GetAIOwner()
        );

        if (!AIController)
        {
            return EBTNodeResult::Failed;
        }

        // 1. Blackboard에서 EQS 가중치 읽기 (v10.2: weights set by controller)
        UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
        FEQSWeightParameters Weights;
        Weights.EnemyObjectiveProximity = BB->GetValueAsFloat(TEXT("Weight_EnemyObj"));
        Weights.AllyObjectiveProximity = BB->GetValueAsFloat(TEXT("Weight_AllyObj"));
        Weights.CoverDensity = BB->GetValueAsFloat(TEXT("Weight_Cover"));
        Weights.EnemyVisibility = BB->GetValueAsFloat(TEXT("Weight_Visibility"));
        Weights.AllyProximity = BB->GetValueAsFloat(TEXT("Weight_AllyProx"));
        Weights.CombatRange = BB->GetValueAsFloat(TEXT("Weight_Range"));
        Weights.PickupProximity = BB->GetValueAsFloat(TEXT("Weight_Pickup"));
        Weights.HeightAdvantage = BB->GetValueAsFloat(TEXT("Weight_Height"));

        // 2. EQS Query 생성 (consolidated method)
        FEnvQueryRequest QueryRequest = AIController->CreateDynamicEQSQuery(Weights);

        // 3. EQS 실행 (비동기)
        CachedOwnerComp = &OwnerComp;
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
    void OnQueryFinished(TSharedPtr<FEnvQueryResult> Result)
    {
        if (Result->IsSuccessful())
        {
            FVector TargetLocation = Result->GetItemAsLocation(0);

            // 4. UE5 Navigation System으로 이동
            AAIController* AIController = Cast<AAIController>(CachedOwnerComp->GetAIOwner());
            if (AIController)
            {
                AIController->MoveToLocation(
                    TargetLocation,
                    100.0f,  // AcceptanceRadius
                    true,    // bStopOnOverlap
                    true     // bUsePathfinding
                );

                // 디버그 시각화
                DrawDebugSphere(
                    AIController->GetWorld(),
                    TargetLocation,
                    50.0f,
                    12,
                    FColor::Green,
                    false,
                    2.0f
                );

                FinishLatentTask(*CachedOwnerComp, EBTNodeResult::Succeeded);
            }
            else
            {
                FinishLatentTask(*CachedOwnerComp, EBTNodeResult::Failed);
            }
        }
        else
        {
            FinishLatentTask(*CachedOwnerComp, EBTNodeResult::Failed);
        }
    }

    UBehaviorTreeComponent* CachedOwnerComp;
};
