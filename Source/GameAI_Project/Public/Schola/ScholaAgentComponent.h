// ScholaAgentComponent.h - Schola agent component for follower pawns

#pragma once

#include "CoreMinimal.h"
#include "Inference/InferenceComponent.h"
#include "Inference/IInferenceAgent.h"
#include "ScholaAgentComponent.generated.h"

class UFollowerAgentComponent;
class UHealthComponent;
class UTacticalObserver;
class UTacticalRewardProvider;
struct FDamageEventData;

/**
 * Schola Agent Component - Integrates RL training via Schola/RLlib
 *
 * This component attaches to follower pawns and:
 * - Exposes 71-feature tactical observations (TacticalObserver)
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
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UScholaAgentComponent : public UInferenceComponent
{
	GENERATED_BODY()

public:
	UScholaAgentComponent(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------

	/** Auto-find and configure FollowerAgentComponent */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bAutoConfigureFollower = true;

	/** Enable time-based decision throttling (FPS-independent training) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
	bool bEnableTimeBasedDecisions = true;

	/** Time interval between decisions (seconds). Default: 1.0s = 1 Hz for macro actions */
	UPROPERTY(EditAnywhere, Category = "Schola|Config", meta = (EditCondition = "bEnableTimeBasedDecisions", ClampMin = "0.01", ClampMax = "5.0"))
	float DecisionInterval = 1.0f;

	//--------------------------------------------------------------------------
	// COMPONENTS
	//--------------------------------------------------------------------------

	/** Tactical observer (71 features) */
	UPROPERTY(EditAnywhere, Instanced, Category = "Schola|Components")
	UTacticalObserver* TacticalObserver = nullptr;

	/** Tactical reward provider (combat events) */
	UPROPERTY(EditAnywhere, Instanced, Category = "Schola|Components")
	UTacticalRewardProvider* RewardProvider = nullptr;

	/** v8.0: Combined tactical actuator (5 continuous values: 4 tactical params + 1 combat priority) */
	UPROPERTY(EditAnywhere, Instanced, Category = "Schola|Components")
	class UCombinedTacticalActuator* CombinedTacticalActuator = nullptr;

	/** Reference to follower agent component (auto-found) */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|Components")
	TObjectPtr<UFollowerAgentComponent> FollowerAgent = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Schola|Components")
	TObjectPtr<UHealthComponent> HealthComponent = nullptr;

	/** Reference to Schola environment (set by environment during registration) */
	UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
	class AScholaCombatEnvironment* ScholaEnvironment = nullptr;

	/** Time accumulated since last decision (for time-based throttling) */
	float TimeSinceLastDecision = 0.0f;

	double LastDecisionTime = 0.0;

	/** Time of last reset (to prevent multiple rapid resets) */
	double LastResetTime = 0.0;

	//--------------------------------------------------------------------------
	// UTILITY
	//--------------------------------------------------------------------------

	/** Initialize Schola components (observers, actuators, rewards) */
	UFUNCTION(BlueprintCallable, Category = "Schola")
	void InitializeScholaComponents();

	/** Get reward from combat events */
	UFUNCTION(BlueprintPure, Category = "Schola")
	float GetCurrentReward() const;

	/** Reset episode for new training round */
	UFUNCTION(BlueprintCallable, Category = "Schola")
	void ResetEpisode();

	//--------------------------------------------------------------------------
	// FPS-INDEPENDENT DECISION LOGIC
	//--------------------------------------------------------------------------

	/**
	 * Override Think() to implement time-based decision throttling
	 * Schola's default implementation uses frame-based DecisionRequestFrequency
	 * which causes FPS-dependent behavior (more FPS = more decisions/sec)
	 */
	virtual void Think() override;

private:
	/** Find follower agent component on owner */
	UFollowerAgentComponent* FindFollowerAgent() const;

	/** Configure observers (TacticalObserver) */
	void ConfigureObservers();

	/** Configure reward provider */
	void ConfigureRewardProvider();

	/** Configure v8.0 actuator (CombinedTacticalActuator) */
	void ConfigureActuators();

	//--------------------------------------------------------------------------
	// EVENT HANDLERS (v4.0 Event-Driven Decisions)
	//--------------------------------------------------------------------------

	/** Enemy spotted event - triggers immediate action request */
	UFUNCTION()
	void OnEnemySpottedEvent(AActor* Enemy);

	/** All enemies lost event - triggers strategic decision */
	UFUNCTION()
	void OnAllEnemiesLostEvent();

	/** Damage taken event - triggers immediate tactical response */
	UFUNCTION()
	void OnDamageTakenEventHandler(const FDamageEventData& DamageEvent, float CurrentHealth);
};
