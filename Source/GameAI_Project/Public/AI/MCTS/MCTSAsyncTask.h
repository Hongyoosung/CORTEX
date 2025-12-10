// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/AsyncWork.h"
#include "Observation/TeamObservation.h"

// Forward declarations
class UMCTS;
class UObjective;
class UObjectiveManager;

/**
 * Async task for running MCTS in background thread
 *
 * Provides better lifecycle management than raw AsyncTask lambdas:
 * - Integrated UE5 stat tracking (Insights profiler)
 * - Thread-safe completion checking
 * - Built-in execution time tracking
 * - Manual cleanup control
 *
 * Usage:
 *   auto* Task = new FAsyncTask<FMCTSAsyncTask>(
 *       MCTS, TeamObs, Followers, ObjectiveManager
 *   );
 *   Task->StartBackgroundTask();
 *
 *   // Later (e.g., in Tick):
 *   if (Task->IsDone()) {
 *       Results = Task->GetTask().GetResults();
 *       ExecutionTime = Task->GetTask().GetExecutionTime();
 *       delete Task;  // Manual cleanup required
 *   }
 */
class GAMEAI_PROJECT_API FMCTSAsyncTask : public FNonAbandonableTask
{
    friend class FAsyncTask<FMCTSAsyncTask>;

public:
    /**
     * Constructor
     *
     * @param InMCTS - MCTS instance to run
     * @param InTeamObs - Team observation snapshot
     * @param InFollowers - List of follower actors
     * @param InObjectiveManager - Objective manager for creating objectives
     */
    FMCTSAsyncTask(
        UMCTS* InMCTS,
        const FTeamObservation& InTeamObs,
        const TArray<AActor*>& InFollowers,
        UObjectiveManager* InObjectiveManager
    )
        : MCTS(InMCTS)
        , TeamObservation(InTeamObs)
        , Followers(InFollowers)
        , ObjectiveManager(InObjectiveManager)
        , bCompleted(false)
        , ExecutionTime(0.0f)
    {
    }

    /**
     * Execute MCTS on background thread
     * Called automatically by FAsyncTask infrastructure
     */
    void DoWork();

    /**
     * Required by FNonAbandonableTask
     * Provides stat tracking for UE5 Insights profiler
     */
    FORCEINLINE TStatId GetStatId() const
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(FMCTSAsyncTask, STATGROUP_ThreadPoolAsyncTasks);
    }

    /**
     * Get results (call after IsComplete() returns true)
     * @return Map of follower to objective assignments
     */
    TMap<AActor*, UObjective*> GetResults() const { return Results; }

    /**
     * Get execution time in milliseconds
     * @return MCTS execution time (ms)
     */
    float GetExecutionTime() const { return ExecutionTime; }

    /**
     * Check if task completed
     * Thread-safe check using FThreadSafeBool
     * @return true if MCTS completed, false if still running
     */
    bool IsComplete() const { return bCompleted; }

private:
    /** MCTS instance (safe to access from background thread) */
    UMCTS* MCTS;

    /** Team observation snapshot (copied, not referenced) */
    FTeamObservation TeamObservation;

    /** Follower actors (copied array) */
    TArray<AActor*> Followers;

    /** Objective manager (safe to access from background thread for objective creation) */
    UObjectiveManager* ObjectiveManager;

    /** Results from MCTS execution */
    TMap<AActor*, UObjective*> Results;

    /** Thread-safe completion flag */
    FThreadSafeBool bCompleted;

    /** Execution time in milliseconds */
    float ExecutionTime;
};
