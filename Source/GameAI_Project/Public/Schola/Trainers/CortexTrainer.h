// File: Schola/Trainers/CortexTrainer.h

#pragma once

#include "CoreMinimal.h"
#include "Training/AbstractTrainer.h"
#include "CortexTrainer.generated.h"

class UScholaCortexAgent;

/**
 * CORTEX 전용 트레이너
 * RLlib의 MultiDiscrete Action을 받아 Agent에게 전달하고,
 * CORTEX v10.1의 복합 보상을 계산하여 반환합니다.
 */
UCLASS()
class GAMEAI_PROJECT_API ACortexTrainer : public AAbstractTrainer
{
    GENERATED_BODY()

public:
    virtual void Initialize(UScholaAgentComponent* InAgent) override;

    // ===== Action Handling =====
    /** Python에서 받은 Action을 Agent가 이해하는 형태로 변환하여 전달 */
    virtual void ApplyAction(const TArray<float>& ActionValues) override; 
    // 주의: Schola는 기본적으로 Action을 float 배열로 줍니다. (Discrete도 인덱스를 float로 줌)

    // ===== Reward Handling =====
    /** v10.1 Multi-Objective Reward 계산 */
    virtual float ComputeReward() override;

private:
    // 캐스팅된 에이전트 참조
    UPROPERTY()
    UScholaCortexAgent* CortexAgent;
};