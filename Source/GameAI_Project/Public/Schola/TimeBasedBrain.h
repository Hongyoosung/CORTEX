// TimeBasedBrain.h - Frame-rate independent brain for Schola
// Replaces frame-based DecisionRequestFrequency with time-based decision interval

#pragma once

#include "CoreMinimal.h"
#include "Brains/SynchronousBrain.h"
#include "TimeBasedBrain.generated.h"

/**
 * Time-Based Synchronous Brain
 *
 * Extends SynchronousBrain to use time-based decision intervals instead of frame-based.
 * This ensures consistent training behavior regardless of Unreal Engine FPS.
 *
 * Key Difference:
 * - SynchronousBrain: Decisions every N frames (FPS-dependent)
 * - TimeBasedBrain: Decisions every N seconds (FPS-independent)
 *
 * Usage:
 * 1. Replace SynchronousBrain with TimeBasedBrain in ScholaAgentComponent Blueprint
 * 2. Set DecisionInterval (default 0.05s = 20 Hz)
 * 3. Training behavior now consistent at any FPS
 */
UCLASS(ClassGroup = (Schola), meta = (BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UTimeBasedBrain : public USynchronousBrain
{
	GENERATED_BODY()

public:
	UTimeBasedBrain();

	/** Time interval between decisions (seconds). Default: 0.05s = 20 Hz */
	UPROPERTY(EditAnywhere, Category = "Brain Settings", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float DecisionInterval = 0.05f;

	/** Time accumulated since last decision (internal state) */
	float TimeSinceLastDecision = 0.0f;

	/**
	 * Update timer and check if it's time for a new decision
	 * Call this from Think() before requesting decisions
	 */
	bool UpdateDecisionTimer(float DeltaTime);

	/**
	 * Override IsDecisionStep to use time-based logic instead of frame-based
	 */
	virtual bool IsDecisionStep() override;

	/**
	 * Reset timer when episode resets
	 */
	virtual void Reset() override;

	/**
	 * Store DeltaTime for time-based calculations
	 * (Schola doesn't pass DeltaTime to Brain, so we need to track it)
	 */
	void SetDeltaTime(float DeltaTime);

private:
	/** Last DeltaTime value (stored for IsDecisionStep) */
	float LastDeltaTime = 0.0f;
};
