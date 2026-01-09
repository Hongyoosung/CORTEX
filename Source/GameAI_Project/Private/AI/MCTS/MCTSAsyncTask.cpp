// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/MCTS/MCTSAsyncTask.h"
#include "AI/MCTS/MCTS.h"
#include "Team/MissionManager.h"

//==============================================================================
// v6.0: ASYNC Mission ASSIGNMENT
//==============================================================================

void FMCTSAsyncTask::DoWork()
{
    if (!MCTS)
    {
        UE_LOG(LogTemp, Error, TEXT("FMCTSAsyncTask v6.0: Invalid MCTS"));
        bCompleted = true;
        return;
    }

    if (Agents.Num() == 0 || Missions.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FMCTSAsyncTask v6.0: No agents or Missions"));
        bCompleted = true;
        return;
    }

    float StartTime = FPlatformTime::Seconds();

    // Run MCTS Mission assignment (v6.0 + thread safety fix)
    ResultAssignment = MCTS->RunMissionAssignment(
        Agents,
        Missions,
        Simulations,
        CachedObservations // v6.0 fix: Pass pre-cached observations for thread safety
    );

    ExecutionTime = (FPlatformTime::Seconds() - StartTime) * 1000.0f; // ms
    bCompleted = true;

    UE_LOG(LogTemp, Verbose, TEXT("FMCTSAsyncTask v6.0: Completed in %.2fms - %d assignments, Value=%.2f"),
        ExecutionTime, ResultAssignment.AgentToMission.Num(), ResultAssignment.ExpectedValue);
}
