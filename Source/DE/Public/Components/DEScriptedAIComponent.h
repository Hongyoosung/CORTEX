// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/DEClassTypes.h"
#include "Types/DEEQSTypes.h"
#include "DEScriptedAIComponent.generated.h"

class ADEAgent;

/**
 * Combat state for the ScriptedAI state machine (active at Tier 2 only).
 *
 * Patrol   — no enemies detected; push aggressively toward objective
 * Approach — enemy spotted; close distance using base class weights
 * Engage   — enemy within engagement range; maximize visibility / combat
 * Retreat  — health critical; seek cover and fall back
 */
UENUM(BlueprintType)
enum class EScriptedAIState : uint8
{
	Patrol   UMETA(DisplayName = "Patrol"),
	Approach UMETA(DisplayName = "Approach"),
	Engage   UMETA(DisplayName = "Engage"),
	Retreat  UMETA(DisplayName = "Retreat"),
};

/**
 * UDEScriptedAIComponent — Scripted AI for Fixed-Opponent Training
 *
 * Attaches to Red team ADEAgent actors and drives their EQS weights
 * with hardcoded per-class weight profiles. Replaces the Schola RL
 * pipeline for the opponent team, making the environment stationary
 * from the Blue (RL) team's perspective.
 *
 * Features:
 *  - Per-class (Strike/Vanguard/Support) hardcoded weight profiles
 *  - 4 difficulty tiers (0=Random, 1=Directed, 2=Full, 3=Expert)
 *  - Per-episode weight noise (±0.1) for robustness
 *  - Lightweight state machine (Patrol→Approach→Engage→Retreat)
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class DE_API UDEScriptedAIComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDEScriptedAIComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	//========================================
	// Difficulty Tier System
	//========================================

	/** Set the difficulty tier (0=Random, 1=Directed, 2=Full, 3=Expert) */
	UFUNCTION(BlueprintCallable, Category = "ScriptedAI")
	void SetDifficultyTier(int32 Tier);

	/** Get current difficulty tier */
	UFUNCTION(BlueprintPure, Category = "ScriptedAI")
	int32 GetDifficultyTier() const { return CurrentTier; }

	/** Get the scripted EQS weights for a given class at the current tier + state */
	FDEEQSWeightParameters GetScriptedWeights(EDEClassType Class) const;

	/** Resample per-episode noise (call on episode reset) */
	void ResampleNoise(const FRandomStream& Stream);

	/** Apply scripted weights to the owning agent */
	void ApplyWeightsToAgent();

	/** Current combat state (read-only, driven by state machine) */
	UFUNCTION(BlueprintPure, Category = "ScriptedAI")
	EScriptedAIState GetAIState() const { return CurrentState; }

protected:
	/** Current difficulty tier (set in editor: 0=Random, 1=Directed, 2=Full, 3=Expert) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScriptedAI")
	int32 CurrentTier = 0;

	/** Per-episode weight noise magnitude */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScriptedAI")
	float NoiseMagnitude = 0.1f;

	/** Weight update interval in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScriptedAI")
	float UpdateInterval = 0.5f;

	//========================================
	// State Machine Configuration
	//========================================

	/** Sphere radius to detect any enemy (Patrol→Approach transition) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScriptedAI|StateMachine")
	float DetectionRadius = 2500.0f;

	/** Sphere radius considered "in engagement range" (Approach→Engage) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScriptedAI|StateMachine")
	float EngagementRadius = 1000.0f;

	/** Health fraction below which the agent retreats */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScriptedAI|StateMachine")
	float RetreatHealthThreshold = 0.30f;

	/** Health fraction above which the agent stops retreating */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScriptedAI|StateMachine")
	float RecoverHealthThreshold = 0.60f;

	//========================================
	// Support Follow Configuration
	//========================================

	/** Desired follow distance for Support agents — stops issuing MoveToActor within this range */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScriptedAI|Support")
	float SupportFollowDistance = 400.0f;

	/** How often (seconds) Support re-picks a random follow ally at Tier 1 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScriptedAI|Support")
	float SupportFollowRepickInterval = 5.0f;

private:
	/** Cached owning agent */
	TWeakObjectPtr<ADEAgent> OwnerAgent;

	/** Per-episode noise offsets (7 dimensions) */
	TArray<float> NoiseOffsets;

	/** Time accumulator for update interval */
	float TimeSinceLastUpdate = 0.0f;

	/** Current state machine state */
	EScriptedAIState CurrentState = EScriptedAIState::Patrol;

	/** Get base weights for a class (before tier/noise/state modification) */
	static FDEEQSWeightParameters GetBaseWeights(EDEClassType Class);

	/** Apply tier modifiers to weights */
	FDEEQSWeightParameters ApplyTierModifiers(const FDEEQSWeightParameters& Base) const;

	/** Apply noise offsets to weights */
	FDEEQSWeightParameters ApplyNoise(const FDEEQSWeightParameters& Weights) const;

	/** Apply state-machine weight overrides (called last, after tier+noise) */
	FDEEQSWeightParameters ApplyStateBehavior(const FDEEQSWeightParameters& Weights, EDEClassType Class) const;

	/** Update CurrentState based on health and enemy proximity */
	void UpdateAIState();

	/** Returns true if any enemy pawn is within Radius of the owning agent */
	bool IsEnemyWithinRange(float Radius) const;

	//========================================
	// Support Follow Behavior (Tier 1+)
	//========================================

	/** Find a random alive non-Support ally on the same team (Tier 1: initial follow target) */
	ADEAgent* PickRandomNonSupportAlly() const;

	/** Find the alive ally with the lowest health percentage (Tier 2+: heal follow target) */
	ADEAgent* FindMostInjuredAlly() const;

	/** Re-evaluate SupportFollowTarget based on current tier */
	void UpdateSupportFollowTarget();

	/**
	 * For Support agents at Tier 1+: override EQS movement with a direct MoveToActor
	 * toward SupportFollowTarget, stopping at SupportFollowDistance.
	 */
	void UpdateSupportMovement();

	/** Tracked follow target for Support (Tier 1: random ally, Tier 2+: most injured ally) */
	TWeakObjectPtr<ADEAgent> SupportFollowTarget;

	/** Countdown until Support re-picks a new random follow ally (Tier 1) */
	float SupportFollowRepickTimer = 0.0f;
};
