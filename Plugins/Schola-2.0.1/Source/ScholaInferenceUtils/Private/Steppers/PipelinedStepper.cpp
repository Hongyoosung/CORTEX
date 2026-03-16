// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Steppers/PipelinedStepper.h"
#include "Async/Async.h"
#include "LogScholaInferenceUtils.h"

void UPipelinedStepper::Step()
{
    if (!Policy || Agents.Num() == 0)
    {
        UE_LOG(LogScholaInferenceUtils, Error, TEXT("PipelinedStepper: Invalid state; missing policy or agents"));
        return;
    }

    const int32 CurrentFrame = TickCounter % PIPELINE_STAGES;
    const bool bHavePrevious = TickCounter > 0;
    const int32 PrevFrame = bHavePrevious ? (TickCounter - 1) % PIPELINE_STAGES : -1;

	if (TickCounter > 0 && Frames[PrevFrame].bActionsReady)
	{
		UE_LOG(LogScholaInferenceUtils, Verbose, TEXT("PrevFrame actions ready; DispatchId=%llu TickCounter=%llu ThreadId=%u"),
			static_cast<unsigned long long>(Frames[PrevFrame].DebugDispatchId),
			static_cast<unsigned long long>(TickCounter),
			(unsigned)FPlatformTLS::GetCurrentThreadId());

        auto& Frame = Frames[PrevFrame];

        if (Frame.Actions.Num() != Agents.Num())
        {
            UE_LOG(LogScholaInferenceUtils, Error, TEXT("PipelinedStepper: Action count mismatch (%d actions for %d agents)"), Frame.Actions.Num(), Agents.Num());
        }
        else
        {
            for (int i = 0; i < Agents.Num(); ++i)
            {
                IAgent::Execute_Act(Agents[i].GetObject(),Frame.Actions[i]);
            }
        }

        Frame.Actions.Reset();
        Frame.bActionsReady = false;
    }
    
    auto& Frame = Frames[CurrentFrame];
    Frame.Observations.Reset();
    Frame.Actions.Reset();
    Frame.bActionsReady = false;
    Frame.bThinkInFlight = true;

    Frame.Observations.Reserve(Agents.Num());

    for (int i = 0; i < Agents.Num(); ++i)
    {
        TInstancedStruct<FPoint> Obs;
        IAgent::Execute_Observe(Agents[i].GetObject(),Obs);
        Frame.Observations.Add(Obs);
    }
    
    if (Policy->IsInferenceBusy())
    {
		return;
    }
    DispatchThink(CurrentFrame);
    
    ++TickCounter;
}

void UPipelinedStepper::DispatchThink(int32 FrameIndex)
{
    FPipelinedStepperFrame* FramePtr = &Frames[FrameIndex];
    TArray<TInstancedStruct<FPoint>> ObservationsCopy = FramePtr->Observations;

    TWeakObjectPtr<UPipelinedStepper> WeakThis(this);
    TScriptInterface<IPolicy> PolicyLocal = Policy;

    // Reserve a unique dispatch id on the Game Thread
    const uint64 DispatchId = DebugDispatchSeq.fetch_add(1, std::memory_order_relaxed) + 1;
    FramePtr->DebugDispatchId = DispatchId;
    UE_LOG(LogScholaInferenceUtils, Verbose, TEXT("DispatchThink scheduled; DispatchId=%llu FrameIndex=%d ThreadId=%u"),
        (unsigned long long)DispatchId, FrameIndex, (unsigned)FPlatformTLS::GetCurrentThreadId());

    Async(EAsyncExecution::ThreadPool, [WeakThis, FrameIndex, Observations = MoveTemp(ObservationsCopy), PolicyLocal, DispatchId]() {
        if (!WeakThis.IsValid() || !PolicyLocal)
            return;

        UE_LOG(LogScholaInferenceUtils, Verbose, TEXT("Think start; DispatchId=%llu FrameIndex=%d ThreadId=%u"),
            (unsigned long long)DispatchId, FrameIndex, (unsigned)FPlatformTLS::GetCurrentThreadId());

        TArray<TInstancedStruct<FPoint>> ActionsLocal;
        const bool bSuccess = PolicyLocal->BatchedThink(const_cast<TArray<TInstancedStruct<FPoint>>&>(Observations), ActionsLocal);

        AsyncTask(ENamedThreads::GameThread, [WeakThis, FrameIndex, bSuccess, Actions = MoveTemp(ActionsLocal), DispatchId]() mutable {
            if (!WeakThis.IsValid())
                return;
            if (WeakThis->bShuttingDown)
                return;

            auto& Frame = WeakThis->Frames[FrameIndex];
            if (!bSuccess)
            {
                UE_LOG(LogScholaInferenceUtils, Error, TEXT("Think failed; DispatchId=%llu FrameIndex=%d"),
                    (unsigned long long)DispatchId, FrameIndex);
                Frame.bThinkInFlight = false;
                return;
            }
            Frame.Actions = MoveTemp(Actions);
            Frame.bActionsReady = true;
            Frame.bThinkInFlight = false;

            UE_LOG(LogScholaInferenceUtils, Verbose, TEXT("Think complete; DispatchId=%llu FrameIndex=%d ThreadId=%u"),
                (unsigned long long)DispatchId, FrameIndex, (unsigned)FPlatformTLS::GetCurrentThreadId());
        });
    });
}
