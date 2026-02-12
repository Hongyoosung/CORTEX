// ThrottledScholaSubsystem.cpp - Custom Schola subsystem implementation

#include "Schola/Subsystems/ThrottledScholaSubsystem.h"
#include "GymConnectors/AbstractGymConnector.h"
#include "Environment/AbstractEnvironment.h"


void UThrottledScholaSubsystem::Tick(float DeltaTime)
{
	// v10.2: Time-based throttling for the Schola decision cycle.
	// Throttles to DecisionInterval (default 0.5s = 2 Hz), aligned with Squad Commander.
	// Mirrors UScholaManagerSubsystem::Tick() with throttle gating.

	double CurrentTime = FPlatformTime::Seconds();
	bool bShouldCollectStates = false;

	// Initialize on first tick
	if (!bThrottleInitialized)
	{
		LastDecisionTime = CurrentTime;
		bThrottleInitialized = true;
		bFirstStep = true;
		bShouldCollectStates = true;
		UE_LOG(LogTemp, Warning, TEXT("[THROTTLE v10.2] Subsystem initialized (Interval=%.2fs = %.1f Hz)"),
			DecisionInterval, 1.0f / DecisionInterval);
	}
	else
	{
		double ElapsedTime = CurrentTime - LastDecisionTime;
		if (ElapsedTime >= DecisionInterval)
		{
			bShouldCollectStates = true;
			LastDecisionTime = CurrentTime;
		}
	}

	// ===== SCHOLA TICK LOGIC (THROTTLED) =====
	// Mirrors UScholaManagerSubsystem::Tick() from the Schola plugin.
	// Phase order: CheckStart → Resolve → Apply → Reset → Collect → Submit → AutoReset

	// Phase 0: Check for start (runs every frame, not throttled)
	if (this->GymConnector && this->GymConnector->IsNotStarted())
	{
		this->GymConnector->CheckForStart();
	}

	// Throttled phases: only run at DecisionInterval
	if (bShouldCollectStates && this->GymConnector && this->GymConnector->IsRunning())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Schola: Agents Acting & Thinking (Throttled)");

		// Phase 1: Resolve - receive Python's gRPC request (BLOCKING)
		FTrainingStateUpdate* StateUpdate = this->GymConnector->ResolveEnvironmentStateUpdate();
		if (StateUpdate)
		{
			this->GymConnector->UpdateConnectorStatus(*StateUpdate);
			this->GymConnector->UpdateEnvironments(*StateUpdate);
		}

		// Phase 2: Reset completed environments
		// v10.2 FIX: NOT guarded by bFirstStep - matches standard Schola behavior.
		// The standard UScholaManagerSubsystem always calls ResetCompletedEnvironments().
		// Skipping it on the first step left the environment in Completed state
		// (from Python's initial reset request), which cascaded into spurious resets
		// on subsequent ticks since the stale Completed status was never cleared.
		if (this->GymConnector && this->GymConnector->IsRunning())
		{
			this->GymConnector->ResetCompletedEnvironments();
		}

		// Phase 3: Collect observations and submit to Python
		if (this->GymConnector && this->GymConnector->IsRunning())
		{
			double CollectStartTime = FPlatformTime::Seconds();
			this->GymConnector->CollectEnvironmentStates();
			double CollectEndTime = FPlatformTime::Seconds();
			double CollectionOverheadMS = (CollectEndTime - CollectStartTime) * 1000.0;

			// Track timing metrics
			DecisionCount++;
			TotalCollectionOverhead += CollectionOverheadMS;
			MaxCollectionOverhead = FMath::Max(MaxCollectionOverhead, CollectionOverheadMS);

			// Log timing diagnostics every 100 decisions
			if (bEnableTimingDiagnostics && DecisionCount % 100 == 0)
			{
				double AvgOverheadMS = TotalCollectionOverhead / DecisionCount;
				UE_LOG(LogTemp, Warning, TEXT("[THROTTLE v10.2] Decision #%d | Collection avg=%.2fms max=%.2fms last=%.2fms"),
					DecisionCount, AvgOverheadMS, MaxCollectionOverhead, CollectionOverheadMS);
			}

			// Phase 4: Submit state to Python (responds to the gRPC exchange)
			if (this->GymConnector->IsRunning())
			{
				this->GymConnector->SubmitEnvironmentStates();
			}
		}
	}

	// Phase 5: Auto-reset (same-step) - only after the first step
	if (bShouldCollectStates && this->GymConnector && !bFirstStep && this->GymConnector->IsRunning())
	{
		this->GymConnector->AutoReset();
	}

	bFirstStep = false;
}
