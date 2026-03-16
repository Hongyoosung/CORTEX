// DEThrottledScholaSubsystem.cpp

#include "Schola/Subsystems/DEThrottledScholaSubsystem.h"

void UDEThrottledScholaSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bThrottleInitialized = false;
}

void UDEThrottledScholaSubsystem::Deinitialize()
{
	GymConnector = nullptr;
	Super::Deinitialize();
}

void UDEThrottledScholaSubsystem::SetGymConnector(UAbstractGymConnector* InConnector)
{
	GymConnector = InConnector;
	bThrottleInitialized = false;
}

TStatId UDEThrottledScholaSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDEThrottledScholaSubsystem, STATGROUP_Tickables);
}

void UDEThrottledScholaSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!GymConnector) return;

	double CurrentTime = FPlatformTime::Seconds();
	bool bShouldStep = false;

	if (!bThrottleInitialized)
	{
		LastDecisionTime = CurrentTime;
		bThrottleInitialized = true;
		bShouldStep = true;
		UE_LOG(LogTemp, Warning,
			TEXT("[DEThrottledSubsystem] Initialized (Interval=%.2fs = %.1f Hz)"),
			DecisionInterval, 1.0f / DecisionInterval);
	}
	else
	{
		if ((CurrentTime - LastDecisionTime) >= DecisionInterval)
		{
			bShouldStep = true;
			LastDecisionTime = CurrentTime;
		}
	}

	// Phase 0: Poll for connector start every frame (not throttled).
	if (GymConnector->IsNotStarted())
	{
		GymConnector->CheckForStart();
	}

	// Throttled phase: run the full step at the configured decision rate.
	if (bShouldStep && GymConnector->IsRunning())
	{
		double StepStart = FPlatformTime::Seconds();

		GymConnector->Step();

		double StepEnd = FPlatformTime::Seconds();
		double StepMS = (StepEnd - StepStart) * 1000.0;

		DecisionCount++;
		TotalCollectionOverhead += StepMS;
		MaxCollectionOverhead = FMath::Max(MaxCollectionOverhead, StepMS);

		if (bEnableTimingDiagnostics && DecisionCount % 100 == 0)
		{
			double AvgMS = TotalCollectionOverhead / DecisionCount;
			UE_LOG(LogTemp, Warning,
				TEXT("[DEThrottledSubsystem] Decision #%d | step avg=%.2fms max=%.2fms last=%.2fms"),
				DecisionCount, AvgMS, MaxCollectionOverhead, StepMS);
		}
	}
}
