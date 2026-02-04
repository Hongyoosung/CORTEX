// ScholaAgentComponent.h - Schola agent component for follower pawns

#pragma once

#include "CoreMinimal.h"
#include "Inference/InferenceComponent.h"
#include "Inference/IInferenceAgent.h"
#include "ScholaAgentComponent.generated.h"


class AFollowerCharacter;
class UTacticalObserver;
class UTacticalRewardProvider;
class UCombinedTacticalActuator;
struct FDamageEventData;


/** //============================================================
 * Schola Agent Component - Integrates RL training via Schola/RLlib
 *
 * This component attaches to follower pawns and:
 * - Exposes 56-feature tactical observations (TacticalObserver)
 * - Provides combat rewards (TacticalRewardProvider)
 * - Inherits from Schola's InferenceComponent (concrete implementation)
 *
 * Architecture:
 * Training: UE5.6 + Schola ←→ gRPC ←→ OpenAI Gym ←→ RLlib
 * Inference: UE5.6 + NNE + ONNX Runtime (no Python)
 *
 * Usage:
 * 1. Add ScholaAgentComponent to follower pawn (replaces abstract InferenceComponent)
 * 2. Ensure FollowerAgentComponent is on the same pawn
 * 3. Component auto-configures observers/rewards/actuators
 * 4. Start UE with Schola server enabled
 * 5. Run Python training script (train_rllib.py)
 */ //============================================================
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UScholaAgentComponent : public UInferenceComponent
{
	GENERATED_BODY()

public:
	UScholaAgentComponent(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	

	//============================================================
	// UTILITY
	//============================================================
	UFUNCTION(BlueprintCallable, Category = "Schola")
	void InitializeScholaComponents();

	UFUNCTION(BlueprintPure, Category = "Schola")
	float GetCurrentReward() const;

	UFUNCTION(BlueprintCallable, Category = "Schola")
	void ResetEpisode();



private:
	void FindFollowerAgent();
	void ConfigureObservers();
	void ConfigureRewardProvider();
	void ConfigureActuators();



private:
	//============= COMPONENTS =================
	TObjectPtr<UTacticalObserver>			TacticalObserver;

	TObjectPtr<UTacticalRewardProvider>		RewardProvider;

	TObjectPtr<UCombinedTacticalActuator>	CombinedTacticalActuator;

	TObjectPtr<AFollowerCharacter>			FollowerAgent;

	TObjectPtr<AScholaCombatEnvironment>	ScholaEnvironment;
};
