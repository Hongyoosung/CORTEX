// TimeBasedBrain.cpp - Implementation of time-based decision brain

#include "Schola/Utils/TimeBasedBrain.h"

UTimeBasedBrain::UTimeBasedBrain()
{
	// Hide parent class's frame-based setting (we use time-based instead)
	bAbstractSettingsVisibility = false;

	// Default: 20 Hz decision rate (0.05s interval)
	DecisionInterval = 0.05f;
	TimeSinceLastDecision = 0.0f;
}

bool UTimeBasedBrain::UpdateDecisionTimer(float DeltaTime)
{
	TimeSinceLastDecision += DeltaTime;

	bool bShouldDecide = (TimeSinceLastDecision >= DecisionInterval);

	if (bShouldDecide)
	{
		// Reset timer for next interval
		TimeSinceLastDecision = 0.0f;
		return true;
	}

	return false;
}

bool UTimeBasedBrain::IsDecisionStep()
{
	// CRITICAL: Schola's Think() doesn't pass DeltaTime to Brain
	// We need to accumulate time from the tick function
	// This is called from IInferenceAgent::Think() which doesn't have DeltaTime access
	//
	// Workaround: We need to override IInferenceAgent::Think() to pass DeltaTime
	// For now, use the parent's frame-based logic but calculate DecisionRequestFrequency dynamically
	//
	// This is a limitation of Schola's architecture - IsDecisionStep() doesn't receive DeltaTime
	// The proper fix requires modifying Schola plugin or overriding Think() in InferenceComponent

	UE_LOG(LogTemp, Warning, TEXT("[TimeBasedBrain] IsDecisionStep() called without DeltaTime - cannot implement time-based logic here. Override Think() instead."));

	// Fallback to parent's frame-based logic
	return Super::IsDecisionStep();
}

void UTimeBasedBrain::Reset()
{
	Super::Reset();
	TimeSinceLastDecision = 0.0f;
}

void UTimeBasedBrain::SetDeltaTime(float DeltaTime)
{
	LastDeltaTime = DeltaTime;
}
