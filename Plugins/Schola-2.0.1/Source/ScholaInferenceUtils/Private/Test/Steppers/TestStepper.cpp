// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "TestStepper.h"
#include "Points/BoxPoint.h"
#include "Points/MultiDiscretePoint.h"
#include "Spaces/MultiDiscreteSpace.h"
#include "Points/Point.h"
#include "Misc/ScopeExit.h"

// Static tracking variables (internal linkage)
static TSet<uint32> GTestPolicyThreadIds; // threads that executed Think
static std::atomic<bool> GTestPolicySawNonGameThread { false };
static uint32 GTestPolicyGameThreadId = 0; // captured at first reset
static std::atomic<uint32> GThinkCounter{ 0 }; // Global think counter


UTestAgent::UTestAgent()
{
	TArray<float> Low = { -1.0f, -1.0f, -1.0f };
	TArray<float> High = { 1.0f, 1.0f, 1.0f };
	TArray<int>	  ActionHigh = { 3 };

	this->Defn = FInteractionDefinition(
		TInstancedStruct<FSpace>::Make<FBoxSpace>(Low, High),
		TInstancedStruct<FSpace>::Make<FMultiDiscreteSpace>(ActionHigh)
	);
}

void UTestAgent::SetStatus_Implementation(EAgentStatus NewStatus)
{
	Status = NewStatus;
}

EAgentStatus UTestAgent::GetStatus_Implementation()
{
	return Status;
}

void UTestAgent::Define_Implementation(FInteractionDefinition& OutDefinition)
{
	OutDefinition = Defn;
}

void UTestAgent::Act_Implementation(const FInstancedStruct& InAction)
{
	if (Status == EAgentStatus::Running)
	{
		if (const FMultiDiscretePoint* DiscreteAction = InAction.GetPtr<FMultiDiscretePoint>())
		{
			if (DiscreteAction->Values.Num() > 0)
			{
				// Single-Dim action space
				int ActionIndex = DiscreteAction->Values[0];

				if (ActionIndex >= 0 && ActionIndex < Defn.ActionSpaceDefn.Get<FMultiDiscreteSpace>().GetFlattenedSize())
				{
					LastActionReceived = ActionIndex;

					switch (ActionIndex)
					{
						case 0:
							UE_LOG(LogTemp, Display, TEXT("Case 0"));
							break;
						case 1:
							UE_LOG(LogTemp, Display, TEXT("Case 1"));
							break;
						case 2:
							UE_LOG(LogTemp, Display, TEXT("Case 2"));
							break;
					}
				}
				else
				{
					// Invalid Action
					UE_LOG(LogTemp, Error, TEXT("Invalid Action index: %d"), ActionIndex);
				}
			}
		}
		else if (const FBoxPoint* BoxAction = InAction.GetPtr<FBoxPoint>())
		{
			// Expected a Discrete Action
			UE_LOG(LogTemp, Error, TEXT("Got FBoxPoint Action"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Got unknown Action type"));
		}
	}
	else
	{
		// Agent must be running to act
		UE_LOG(LogTemp, Error, TEXT("Agent must be running to act"));
	}
}

void UTestAgent::Observe_Implementation(FInstancedStruct& OutObservations)
{
	// Random Fixed values to test, that fit within the observation definition range
	TArray<float> MockObservation = { 0.5f, -0.1f, 0.2f };
	
	OutObservations.InitializeAs<FBoxPoint>(MockObservation);

	UE_LOG(LogTemp, Display, TEXT("After move to OutObservations: OutObservations state: %s"), OutObservations.IsValid() ? TEXT("Valid") : TEXT("Invalid"));

	if (const FBoxPoint* BoxPoint = OutObservations.GetPtr<FBoxPoint>())
	{
		UE_LOG(LogTemp, Display, TEXT("Successfully created FBoxPoint with %d values"), BoxPoint->Values.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create FBoxPoint"));
	}
}

UTestPolicy::UTestPolicy()
{
}

bool UTestPolicy::Think(const TInstancedStruct<FPoint>& InObservations, TInstancedStruct<FPoint>& OutAction)
{
	bool Expected = false;
	if(!bInferenceInFlight.compare_exchange_strong(Expected, true, std::memory_order_acq_rel))
	{
		UE_LOG(LogTemp, Verbose, TEXT("UTestPolicy::Think skipped - inference already in flight"));
		return false; // signal stepper to try next tick
	}
	ON_SCOPE_EXIT{ bInferenceInFlight.store(false, std::memory_order_release); };
	const uint32 Count = GThinkCounter.fetch_add(1, std::memory_order_relaxed) + 1;
	UE_LOG(LogTemp, Display, TEXT("Think #%u ThreadId=%u"), (unsigned)Count, (unsigned)FPlatformTLS::GetCurrentThreadId());

	// Thread instrumentation
	const uint32 CurrentTid = FPlatformTLS::GetCurrentThreadId();
	GTestPolicyThreadIds.Add(CurrentTid);
	if (IsInGameThread())
	{
		if (GTestPolicyGameThreadId == 0) { GTestPolicyGameThreadId = CurrentTid; }
	}
	else
	{
		GTestPolicySawNonGameThread.store(true, std::memory_order_relaxed);
	}

	TArray<int> ActionChoice = { 1 };
	OutAction.InitializeAs<FMultiDiscretePoint>(ActionChoice);

	UE_LOG(LogTemp, Display, TEXT("After move to OutAction: OutAction state: %s"), OutAction.IsValid() ? TEXT("Valid") : TEXT("Invalid"));
	UE_LOG(LogTemp, Display, TEXT("Policy chose action: 1"));
	return true;
}

bool UTestPolicy::Init(const FInteractionDefinition& InPolicyDefinition)
{
	PolicyDefinition = InPolicyDefinition;
	UE_LOG(LogTemp, Display, TEXT("Policy initiated"));
	return true;
}

bool UTestPolicy::IsInferenceBusy() const
{
	return bInferenceInFlight.load(std::memory_order_relaxed);
}

void UTestPolicy::ResetThreadTracking()
{
	GTestPolicyThreadIds.Reset();
	GTestPolicySawNonGameThread.store(false, std::memory_order_relaxed);
	GTestPolicyGameThreadId = FPlatformTLS::GetCurrentThreadId(); // snapshot
	GThinkCounter.store(0, std::memory_order_relaxed);
}

TSet<uint32> UTestPolicy::GetThreadIdsCopy()
{
	return GTestPolicyThreadIds; // copy
}

bool UTestPolicy::SawNonGameThread()
{
	return GTestPolicySawNonGameThread.load(std::memory_order_relaxed);
}
