// ThrottledScholaSubsystem.h - Custom Schola subsystem with time-based decision throttling

#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ScholaManagerSubsystem.h"
#include "ThrottledScholaSubsystem.generated.h"

/**
 * Custom Schola Manager Subsystem with Time-Based Throttling (v7.5)
 *
 * Replaces UScholaManagerSubsystem to add FPS-independent decision rate limiting.
 * Overrides Tick() to throttle CollectEnvironmentStates() calls to a fixed interval.
 *
 * Why This Works:
 * - UE5's subsystem architecture allows overriding base subsystems
 * - Tick() is virtual in UScholaManagerSubsystem
 * - This intercepts at the highest level (before any Think() calls)
 *
 * Usage:
 * 1. This class is automatically used instead of UScholaManagerSubsystem (subsystem override)
 * 2. Configure DecisionInterval in Project Settings > Plugins > Schola (default: 1.0s = 1 Hz)
 * 3. Training will now run at consistent rate regardless of FPS
 */
UCLASS(config = Schola)
class GAMEAI_PROJECT_API UThrottledScholaSubsystem : public UScholaManagerSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Override Tick() to add time-based throttling
	 * Only calls CollectEnvironmentStates() / SubmitEnvironmentStates() once per DecisionInterval
	 */
	virtual void Tick(float DeltaTime) override;

	/** Decision interval in seconds (default: 1.0s = 1 Hz) */
	UPROPERTY(Config, EditAnywhere, Category = "Schola|Throttling", meta = (ClampMin = "0.01", ClampMax = "10.0"))
	float DecisionInterval = 0.1f;

	/** Enable timing diagnostics (logs overhead every 100 decisions) */
	UPROPERTY(Config, EditAnywhere, Category = "Schola|Throttling")
	bool bEnableTimingDiagnostics = true;

private:
	/** Last decision time (FPlatformTime::Seconds()) */
	double LastDecisionTime = 0.0;

	/** Initialization flag */
	bool bThrottleInitialized = false;

	/** First step flag (for auto-reset logic) */
	bool bFirstStep = true;

	//--------------------------------------------------------------------------
	// TIMING DIAGNOSTICS (v8.0)
	//--------------------------------------------------------------------------

	/** Total observation collection overhead (ms) */
	double TotalCollectionOverhead = 0.0;

	/** Number of decisions made */
	int32 DecisionCount = 0;

	/** Max single collection overhead (ms) */
	double MaxCollectionOverhead = 0.0;
};
