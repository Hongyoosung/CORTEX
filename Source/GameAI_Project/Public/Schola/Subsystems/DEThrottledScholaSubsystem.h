// DEThrottledScholaSubsystem.h - Custom Schola subsystem with time-based decision throttling

#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ScholaManagerSubsystem.h"
#include "DEThrottledScholaSubsystem.generated.h"

/**
 * Custom Schola Manager Subsystem with Time-Based Throttling
 *
 * Replaces UScholaManagerSubsystem to add FPS-independent decision rate limiting.
 * Overrides Tick() to throttle CollectEnvironmentStates() calls to a fixed interval.
 *
 * Architecture Integration:
 * - Aligns observation collection (2 Hz) with Squad Commander planning (2 Hz)
 * - Prevents wasted observations between centralized planning cycles
 * - Ensures each state → action → reward transition is meaningful
 *
 * Why This Works:
 * - UE5's subsystem architecture allows overriding base subsystems
 * - Tick() is virtual in UScholaManagerSubsystem
 * - This intercepts at the highest level (before any Think() calls)
 *
 * Usage:
 * 1. This class is automatically used instead of UScholaManagerSubsystem (subsystem override)
 * 2. Configure DecisionInterval in Project Settings > Plugins > Schola (default: 0.5s = 2 Hz)
 * 3. Training will now run at consistent rate regardless of FPS
 */
UCLASS(config = Schola)
class GAMEAI_PROJECT_API UDEThrottledScholaSubsystem : public UScholaManagerSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Override Tick() to add time-based throttling
	 * Only calls CollectEnvironmentStates() / SubmitEnvironmentStates() once per DecisionInterval
	 */
	virtual void Tick(float DeltaTime) override;

	/** Decision interval in seconds (v10.2: 0.5s = 2 Hz, aligned with Squad Commander) */
	UPROPERTY(Config, EditAnywhere, Category = "Schola|Throttling", meta = (ClampMin = "0.01", ClampMax = "10.0"))
	float DecisionInterval = 0.5f;

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


	//========================================
	// TIMING DIAGNOSTICS 
	//========================================

	/** Total observation collection overhead (ms) */
	double TotalCollectionOverhead = 0.0;

	/** Number of decisions made */
	int32 DecisionCount = 0;

	/** Max single collection overhead (ms) */
	double MaxCollectionOverhead = 0.0;
};
