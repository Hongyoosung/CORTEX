// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MCTS/MCTSAsyncTask.h"
#include "AI/MCTS/MCTS.h"
#include "Team/ObjectiveManager.h"

void FMCTSAsyncTask::DoWork()
{
    if (!MCTS || !ObjectiveManager)
    {
        UE_LOG(LogTemp, Error, TEXT("FMCTSAsyncTask: Invalid MCTS or ObjectiveManager"));
        bCompleted = true;
        return;
    }

    float StartTime = FPlatformTime::Seconds();

    // Run MCTS with objectives (parallel simulations will be used if enabled)
    Results = MCTS->RunTeamMCTSWithObjectives(
        TeamObservation,
        Followers,
        ObjectiveManager
    );

    ExecutionTime = (FPlatformTime::Seconds() - StartTime) * 1000.0f; // ms
    bCompleted = true;

    UE_LOG(LogTemp, Verbose, TEXT("FMCTSAsyncTask: Completed in %.2fms, generated %d objectives"),
        ExecutionTime, Results.Num());
}
