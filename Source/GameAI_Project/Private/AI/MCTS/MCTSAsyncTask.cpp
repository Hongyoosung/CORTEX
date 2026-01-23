// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MCTS/MCTSAsyncTask.h"
#include "AI/MCTS/MCTS.h"

//==============================================================================
// v8.0: ASYNC STRATEGY ASSIGNMENT
//==============================================================================

void FMCTSAsyncTask::DoWork()
{
    if (!MCTS)
    {
        UE_LOG(LogTemp, Error, TEXT("[FMCTSAsyncTask v8.0] Invalid MCTS"));
        bCompleted = true;
        return;
    }

    if (Agents.Num() == 0 || Objectives.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FMCTSAsyncTask v8.0] No agents or objectives"));
        bCompleted = true;
        return;
    }

    float StartTime = FPlatformTime::Seconds();

    // Run MCTS strategy assignment (v8.0)
    ResultAssignments = MCTS->RunStrategyAssignment(
        Agents,
        Objectives,
        Simulations,
        CachedObservations  // v8.0: Pass pre-cached observations for thread safety
    );

    ExecutionTime = (FPlatformTime::Seconds() - StartTime) * 1000.0f; // ms
    bCompleted = true;

    UE_LOG(LogTemp, Verbose, TEXT("[FMCTSAsyncTask v8.0] Completed in %.2fms - %d strategy assignments"),
        ExecutionTime, ResultAssignments.Num());
}
