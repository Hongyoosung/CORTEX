// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/DEClassTypes.h"
#include "Types/DEEQSTypes.h"
#include "DEScriptedAIComponent.generated.h"

class ADEAgent;

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
 *  - 4 difficulty tiers (Passive → Basic → Standard → Aggressive)
 *  - Per-episode weight noise (±0.1) for robustness
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

	/** Set the difficulty tier (0=Passive, 1=Basic, 2=Standard, 3=Aggressive) */
	UFUNCTION(BlueprintCallable, Category = "ScriptedAI")
	void SetDifficultyTier(int32 Tier);

	/** Get current difficulty tier */
	UFUNCTION(BlueprintPure, Category = "ScriptedAI")
	int32 GetDifficultyTier() const { return CurrentTier; }

	/** Get the scripted EQS weights for a given class at the current tier */
	FDEEQSWeightParameters GetScriptedWeights(EDEClassType Class) const;

	/** Resample per-episode noise (call on episode reset) */
	void ResampleNoise(const FRandomStream& Stream);

	/** Apply scripted weights to the owning agent */
	void ApplyWeightsToAgent();

protected:
	/** Current difficulty tier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScriptedAI")
	int32 CurrentTier = 2;

	/** Per-episode weight noise magnitude */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScriptedAI")
	float NoiseMagnitude = 0.1f;

	/** Weight update interval in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScriptedAI")
	float UpdateInterval = 0.5f;

private:
	/** Cached owning agent */
	TWeakObjectPtr<ADEAgent> OwnerAgent;

	/** Per-episode noise offsets (7 dimensions) */
	TArray<float> NoiseOffsets;

	/** Time accumulator for update interval */
	float TimeSinceLastUpdate = 0.0f;

	/** Get base weights for a class (before tier/noise modification) */
	static FDEEQSWeightParameters GetBaseWeights(EDEClassType Class);

	/** Apply tier modifiers to weights */
	FDEEQSWeightParameters ApplyTierModifiers(const FDEEQSWeightParameters& Base) const;

	/** Apply noise offsets to weights */
	FDEEQSWeightParameters ApplyNoise(const FDEEQSWeightParameters& Weights) const;
};
