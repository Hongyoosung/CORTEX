#pragma once

#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "DrawDebugHelpers.h"
#include "Types/EQSTypes.h"
#include "BTTask_MoveToEQSLocation.generated.h"


/**
 * BTTask_MoveToEQSLocation
 *
 * Inference-mode movement task. Reads EQS weights from Blackboard
 * (written by PerformTacticalAction() → inference path), builds a weighted
 * FEnvQueryRequest, runs it asynchronously, then commands MoveToLocation.
 *
 * Blackboard keys read (set by AMocCharacter::PerformTacticalAction):
 *   Weight_EnemyObj, Weight_AllyObj, Weight_Cover, Weight_EnemyVis,
 *   Weight_AllyProx, Weight_Range
 *
 * No dependency on AMocAIController — works with any AAIController.
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API UBTTask_MoveToEQSLocation : public UBTTaskNode
{
    GENERATED_BODY()

public:
    virtual EBTNodeResult::Type ExecuteTask(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory
    ) override
    {
        AAIController* AIController = Cast<AAIController>(OwnerComp.GetAIOwner());
        if (!AIController || !QueryTemplate)
        {
            return EBTNodeResult::Failed;
        }

        // Read EQS weights from Blackboard
        // Keys match AMocCharacter::PerformTacticalAction() writer
        UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
        if (!BB)
        {
            return EBTNodeResult::Failed;
        }

        FEQSWeightParameters Weights;
        Weights.EnemyObjectiveProximity = BB->GetValueAsFloat(TEXT("Weight_EnemyObj"));
        Weights.AllyObjectiveProximity  = BB->GetValueAsFloat(TEXT("Weight_AllyObj"));
        Weights.CoverDensity            = BB->GetValueAsFloat(TEXT("Weight_Cover"));
        Weights.EnemyVisibility         = BB->GetValueAsFloat(TEXT("Weight_EnemyVis"));
        Weights.AllyProximity           = BB->GetValueAsFloat(TEXT("Weight_AllyProx"));
        Weights.CombatRange             = BB->GetValueAsFloat(TEXT("Weight_Range"));

        // Normalize RL output [-1, 1] → EQS scale [-2, 2]
        auto Normalize = [](float W) { return FMath::Clamp(W * 2.0f, -2.0f, 2.0f); };

        UObject* OwnerPawn = Cast<UObject>(AIController->GetPawn());
        FEnvQueryRequest QueryRequest(QueryTemplate, OwnerPawn);
        QueryRequest.SetFloatParam(TEXT("EnemyObjectiveWeight"), Normalize(Weights.EnemyObjectiveProximity));
        QueryRequest.SetFloatParam(TEXT("AllyObjectiveWeight"),  Normalize(Weights.AllyObjectiveProximity));
        QueryRequest.SetFloatParam(TEXT("CoverDensityWeight"),   Normalize(Weights.CoverDensity));
        QueryRequest.SetFloatParam(TEXT("EnemyVisibilityWeight"),Normalize(Weights.EnemyVisibility));
        QueryRequest.SetFloatParam(TEXT("AllyProximityWeight"),  Normalize(Weights.AllyProximity));
        QueryRequest.SetFloatParam(TEXT("CombatRangeWeight"),    Normalize(Weights.CombatRange));

        CachedOwnerComp = &OwnerComp;
        FQueryFinishedSignature Delegate;
        Delegate.BindUObject(this, &UBTTask_MoveToEQSLocation::OnQueryFinished);
        QueryRequest.Execute(EEnvQueryRunMode::SingleResult, Delegate);

        return EBTNodeResult::InProgress;
    }

    /** EQS query template — assign in BT asset */
    UPROPERTY(EditAnywhere, Category = "EQS")
    UEnvQuery* QueryTemplate;

private:
    void OnQueryFinished(TSharedPtr<FEnvQueryResult> Result)
    {
        if (!CachedOwnerComp)
        {
            return;
        }

        if (Result->IsSuccessful())
        {
            const FVector TargetLocation = Result->GetItemAsLocation(0);
            AAIController* AIController = Cast<AAIController>(CachedOwnerComp->GetAIOwner());
            if (AIController)
            {
                AIController->MoveToLocation(TargetLocation, 100.0f, true, true);
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

    UBehaviorTreeComponent* CachedOwnerComp = nullptr;
};
