
#pragma once

#include "CoreMinimal.h"
#include "Inference/InferenceComponent.h"
#include "Inference/IInferenceAgent.h"
#include "AI/MCTS/ModelBasedMCTS.h"
#include "AI/Models/LearnedWorldModel.h"
#include "AI/Networks/ValueNetwork.h"
#include "Schola/Logging/ScholaTransitionLogger.h"
#include "ScholaMocAgent.generated.h"

UENUM(BlueprintType)
enum class EAgentMode : uint8 {
    Training,   // Python(RLlib)이 액션을 결정 (데이터 수집용)
    Inference   // 내부 MCTS가 액션을 결정 (실전용)
};

UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UScholaMocAgent : public UInferenceComponent
{
    GENERATED_BODY()

public:
    UScholaMocAgent(const FObjectInitializer& ObjectInitializer);

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ===== 1. Action Interface =====
    /** Gym의 MultiDiscrete 액션을 게임 내 FTacticalOption으로 변환하여 실행 */
    void ExecuteGymAction(const TArray<int32>& GymActionVector);
    
    /** MCTS가 결정한 FTacticalOption을 직접 실행 */
    void ExecuteOption(const FTacticalOption& Option);

    // ===== 2. Model Management =====
    /** 런타임에 ONNX 모델 핫로딩 */
    UFUNCTION(BlueprintCallable, Category="MOC|Models")
    void ReloadModels(FString WorldModelPath, FString ValueNetPath);

    // ===== 3. State Access =====
    /** 현재 Observation(State) 반환 */
    TArray<float> GetCurrentState() const;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOC")
    EAgentMode CurrentMode = EAgentMode::Inference;

private:
    // MOC Core Systems
    UPROPERTY()
    UModelBasedMCTS* MCTSAlgorithm;

    UPROPERTY()
    ULearnedWorldModel* WorldModel;

    UPROPERTY()
    UValueNetwork* ValueNetwork;

    UPROPERTY()
    UScholaTransitionLogger* DataLogger;

    // State Tracking for Logger (s_t, a_t -> s_t+1)
    TArray<float> LastState;
    FTacticalOption LastOption;
    bool bHasLastAction = false;
};