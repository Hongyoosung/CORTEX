#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EQSDynamicWeightApplicator.generated.h"


UCLASS()
class GAMEAI_PROJECT_API UEQSDynamicWeightApplicator : public UObject {

    GENERATED_BODY()

public:
    /**
     * RL Policy 출력을 받아 EQS Query에 실시간 가중치 주입
     * 
     * @param QueryTemplate - UE5 에셋으로 생성한 EQ_TacticalMovement
     * @param RLWeights - RL Policy가 예측한 8-dim 가중치 벡터
     * @return 실행 가능한 EQS Query 인스턴스
     */
    FEnvQueryRequest CreateDynamicQuery(
        UEnvQuery* QueryTemplate,
        const FEQSWeightParameters& RLWeights
    ) {
        FEnvQueryRequest QueryRequest(QueryTemplate, GetOwner());
        
        // Test별 가중치 동적 바인딩
        QueryRequest.SetFloatParam("EnemyObjectiveWeight", 
            NormalizeWeight(RLWeights.EnemyObjectiveProximity));
        
        QueryRequest.SetFloatParam("AllyObjectiveWeight", 
            NormalizeWeight(RLWeights.AllyObjectiveProximity));
        
        QueryRequest.SetFloatParam("CoverDensityWeight", 
            NormalizeWeight(RLWeights.CoverDensity));
        
        QueryRequest.SetFloatParam("EnemyVisibilityWeight", 
            NormalizeWeight(RLWeights.EnemyVisibility));
        
        QueryRequest.SetFloatParam("AllyProximityWeight", 
            NormalizeWeight(RLWeights.AllyProximity));
        
        QueryRequest.SetFloatParam("CombatRangeWeight", 
            NormalizeWeight(RLWeights.CombatRange));
        
        QueryRequest.SetFloatParam("PickupWeight", 
            NormalizeWeight(RLWeights.PickupProximity));
        
        QueryRequest.SetFloatParam("HeightWeight", 
            NormalizeWeight(RLWeights.HeightAdvantage));
        
        return QueryRequest;
    }
    
private:
    /**
     * RL 출력 [-1, 1]을 EQS 스케일로 변환
     * Game AI Pro 권장사항: 가중치는 [-2, 2] 범위 사용
     */
    float NormalizeWeight(float RLOutput) {
        return FMath::Clamp(RLOutput * 2.0f, -2.0f, 2.0f);
    }
};
