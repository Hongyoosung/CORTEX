// ThrottledScholaSubsystem.cpp - Custom Schola subsystem implementation

#include "Schola/ThrottledScholaSubsystem.h"
#include "GymConnectors/AbstractGymConnector.h"


void UThrottledScholaSubsystem::Tick(float DeltaTime)
{
	// v7.5: Time-based throttling for CollectEnvironmentStates() / SubmitEnvironmentStates()
	// This is the PRIMARY rate limiter - prevents UE5 from sending observations to Python too fast

	double CurrentTime = FPlatformTime::Seconds();
	bool bShouldCollectStates = false;

	// Initialize on first tick
	if (!bThrottleInitialized)
	{
		LastDecisionTime = CurrentTime;
		bThrottleInitialized = true;
		bFirstStep = true;
		bShouldCollectStates = true; // Allow first observation immediately
		UE_LOG(LogTemp, Warning, TEXT("[THROTTLE v7.5] Subsystem initialized (Interval=%.2fs = %.1f Hz)"),
			DecisionInterval, 1.0f / DecisionInterval);
	}
	else
	{
		double ElapsedTime = CurrentTime - LastDecisionTime;

		// Check if enough time has passed
		if (ElapsedTime >= DecisionInterval)
		{
			bShouldCollectStates = true;
			LastDecisionTime = CurrentTime;

			static int32 DecisionCounter = 0;
			DecisionCounter++;
			UE_LOG(LogTemp, Warning, TEXT("[THROTTLE v7.5] ✅ Decision #%d allowed (elapsed=%.3fs)"),
				DecisionCounter, ElapsedTime);
		}
		// else: Throttled, don't collect states this frame
	}

	// ===== PARENT'S TICK LOGIC (MODIFIED) =====
	// Based on UScholaManagerSubsystem::Tick() from Schola plugin

	// Check for start
	if (this->GymConnector && this->GymConnector->IsNotStarted())
	{
		bool bStarted = this->GymConnector->CheckForStart();
	}

	// THROTTLED Phase: Only run decision cycle at DecisionInterval
	// Maintains Schola's RPC order: Resolve (Receive) → Apply → Collect → Submit (Respond)
	if (bShouldCollectStates && this->GymConnector && this->GymConnector->IsRunning())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Schola: Agents Acting & Thinking (Throttled)");

		// 1. Resolve action from Python (calls Receive(), sets CurrExchange, waits for Python's RPC)
		FTrainingStateUpdate* StateUpdate = this->GymConnector->ResolveEnvironmentStateUpdate();
		if (StateUpdate)
		{
			this->GymConnector->UpdateConnectorStatus(*StateUpdate);
			this->GymConnector->UpdateEnvironments(*StateUpdate);

			// 2. Reset completed environments (skip on first step)
			if (!bFirstStep)
			{
				this->GymConnector->ResetCompletedEnvironments();
			}

			// 3. Collect new observations
			this->GymConnector->CollectEnvironmentStates();

			// 4. Submit to Python (calls Respond(), sends observation, clears CurrExchange)
			// CRITICAL: Only submit if we received a request (StateUpdate != nullptr)
			// Otherwise CurrExchange is null and SubmitEnvironmentStates() will crash
			this->GymConnector->SubmitEnvironmentStates();
		}
	}

	// Auto-reset phase
	if (this->GymConnector && !bFirstStep && this->GymConnector->IsRunning())
	{
		this->GymConnector->AutoReset();
	}
	bFirstStep = false;
}
