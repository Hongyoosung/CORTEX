#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RL/RLTypes.h"
#include "RLAgentComponent.generated.h"

// Forward declarations
class URLPolicyNetwork;
class URewardCalculator;

/**
 * RL Agent Component
 *
 * Responsibilities:
 * - Accumulated reward tracking
 * - Episode management
 * - Policy network interaction
 * - Experience collection
 * - Reward calculator integration
 *
 * This component was extracted from FollowerAgentComponent to improve separation of concerns.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API URLAgentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URLAgentComponent();

	virtual void BeginPlay() override;

	//--------------------------------------------------------------------------
	// REINFORCEMENT LEARNING
	//--------------------------------------------------------------------------

	/** Provide reward feedback to RL policy */
	UFUNCTION(BlueprintCallable, Category = "RL|Reward")
	void ProvideReward(float Reward, bool bTerminal = false);

	/** Accumulate reward (alias for ProvideReward for compatibility) */
	UFUNCTION(BlueprintCallable, Category = "RL|Reward")
	void AccumulateReward(float Reward) { ProvideReward(Reward, false); }

	/** Get accumulated reward this episode */
	UFUNCTION(BlueprintPure, Category = "RL|Reward")
	float GetAccumulatedReward() const { return AccumulatedReward; }

	/** Reset episode (clear accumulated reward) */
	UFUNCTION(BlueprintCallable, Category = "RL|Episode")
	void ResetEpisode();

	/** Called when episode ends - assigns terminal reward and marks experiences */
	UFUNCTION(BlueprintCallable, Category = "RL|Episode")
	void OnEpisodeEnded(float EpisodeReward);

	/** Is tactical policy ready for queries? */
	UFUNCTION(BlueprintPure, Category = "RL|Policy")
	bool IsTacticalPolicyReady() const { return TacticalPolicy != nullptr; }

	/** Get tactical policy (nullptr if not set) */
	UFUNCTION(BlueprintPure, Category = "RL|Policy")
	URLPolicyNetwork* GetTacticalPolicy() const { return TacticalPolicy; }

	/** Get reward calculator */
	UFUNCTION(BlueprintPure, Category = "RL|Reward")
	URewardCalculator* GetRewardCalculator() const { return RewardCalculator; }

public:
	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------

	/** RL policy for tactical action selection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL|Config")
	URLPolicyNetwork* TacticalPolicy = nullptr;

	/** Enable RL policy (if false, uses rule-based fallback) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL|Config")
	bool bUseRLPolicy = true;

	/** Collect experiences for offline training */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL|Config")
	bool bCollectExperiences = true;

	//--------------------------------------------------------------------------
	// STATE
	//--------------------------------------------------------------------------

	/** Accumulated reward this episode */
	UPROPERTY(BlueprintReadOnly, Category = "RL|State")
	float AccumulatedReward = 0.0f;

	/** Reward calculator for hierarchical rewards */
	UPROPERTY(BlueprintReadOnly, Category = "RL|Components")
	URewardCalculator* RewardCalculator = nullptr;
};
