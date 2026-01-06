// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MCTS/MCTSAsyncTask.h"
#include "AI/MCTS/MCTS.h"
#include "Team/ObjectiveManager.h"

//==============================================================================
// v6.0: ASYNC OBJECTIVE ASSIGNMENT
//==============================================================================

void FMCTSAsyncTask::DoWork()
{
    if (!MCTS)
    {
        UE_LOG(LogTemp, Error, TEXT("FMCTSAsyncTask v6.0: Invalid MCTS"));
        bCompleted = true;
        return;
    }

    if (Agents.Num() == 0 || Objectives.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FMCTSAsyncTask v6.0: No agents or objectives"));
        bCompleted = true;
        return;
    }

    float StartTime = FPlatformTime::Seconds();

    // Run MCTS objective assignment (v6.0)
    ResultAssignment = MCTS->RunObjectiveAssignment(
        Agents,
        Objectives,
        Simulations
    );

    ExecutionTime = (FPlatformTime::Seconds() - StartTime) * 1000.0f; // ms
    bCompleted = true;

    UE_LOG(LogTemp, Verbose, TEXT("FMCTSAsyncTask v6.0: Completed in %.2fms - %d assignments, Value=%.2f"),
        ExecutionTime, ResultAssignment.AgentToObjective.Num(), ResultAssignment.ExpectedValue);
}
