#include "CortexTrainer.h"

void ACortexTrainer::Initialize(UScholaAgentComponent* InAgent)
{
    Super::Initialize(InAgent);
    
    CortexAgent = Cast<UScholaCortexAgent>(InAgent);
    
    // Gym Space 정의 (Schola 설정과 일치해야 함)
    // Action Space: MultiDiscrete [Strategy(4), Objective(10)]
    // Observation Space: Box(54) - 이미 AgentComponent에서 설정됨
}

void ACortexTrainer::ApplyAction(const TArray<float>& ActionValues)
{
    if (!CortexAgent) return;

    // Schola로부터 들어온 Float 배열을 Int로 변환 (MultiDiscrete 지원)
    TArray<int32> DiscreteActions;
    for (float Val : ActionValues)
    {
        DiscreteActions.Add(FMath::RoundToInt(Val));
    }

    // CortexAgent에게 실행 위임 (Training Mode 로직)
    CortexAgent->ExecuteGymAction(DiscreteActions);
}

float ACortexTrainer::ComputeReward()
{
    if (!CortexAgent) return 0.0f;

    // v10.1 복합 보상 가져오기
    // AgentRewardManager가 이제 FCompositeReward 구조체를 계산한다고 가정
    // 학습 시에는 이를 스칼라 값(하나의 float)으로 합쳐서 RLlib에 줘야 함 (PPO 등은 스칼라 보상 필요)
    
    // 단, World Model 학습용 로그에는 벡터 보상이 따로 기록되고 있음 (Logger 통해)
    // 여기서는 RLlib의 Policy Gradient를 위한 "현재 정책의 좋음"을 나타내는 스칼라 반환
    
    float StepReward = CortexAgent->GetCurrentReward(); // 내부적으로 Weight 적용된 값 호출
    
    EpisodeReward += StepReward;
    EpisodeSteps++;
    
    return StepReward;
}