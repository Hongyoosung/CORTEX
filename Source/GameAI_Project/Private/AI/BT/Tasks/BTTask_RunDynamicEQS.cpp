#include "AI/BT/Tasks/BTTask_RunDynamicEQS.h"
#include "Types/EQSTypes.h"
#include "AI/AIController/MocAIController.h"


UBTTask_RunDynamicEQS::UBTTask_RunDynamicEQS()
{
}

EBTNodeResult::Type UBTTask_RunDynamicEQS::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp, 
    uint8* NodeMemory
)
{
    AMocAIController* AIController = Cast<AMocAIController>(
        OwnerComp.GetAIOwner()
    );
    
    if (!AIController || !QueryTemplate) 
        return EBTNodeResult::Failed;
    
    // Blackboard에서 EQS 가중치 읽기
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
    
    // EQS 쿼리 생성
    FEnvQueryRequest QueryRequest = AIController->CreateDynamicEQSQuery(Weights);
    
    // 비동기 실행
    CachedOwnerComp = &OwnerComp;
    FQueryFinishedSignature Delegate;
    Delegate.BindUObject(this, &UBTTask_RunDynamicEQS::OnQueryFinished);
    QueryRequest.Execute(EEnvQueryRunMode::SingleResult, Delegate);
    
    return EBTNodeResult::InProgress;
}

void UBTTask_RunDynamicEQS::OnQueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
    if (Result->IsSuccessful())
    {
        FVector Location = Result->GetItemAsLocation(0);
        UBlackboardComponent* BB = CachedOwnerComp->GetBlackboardComponent();
        BB->SetValueAsVector(EQSResultKey.SelectedKeyName, Location);
        
        FinishLatentTask(*CachedOwnerComp, EBTNodeResult::Succeeded);
    }
    else
    {
        FinishLatentTask(*CachedOwnerComp, EBTNodeResult::Failed);
    }
}