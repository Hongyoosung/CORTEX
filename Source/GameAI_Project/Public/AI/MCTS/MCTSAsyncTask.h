// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/AsyncWork.h"
#include "Observation/TeamObservation.h"
#include "Observation/ObservationElement.h"

// Forward declarations
class UMCTS;
class UMission;
class UMissionManager;

/**
 * Async task for running MCTS Mission assignment in background thread (v6.0)
 *
 * Provides better lifecycle management than raw AsyncTask lambdas:
 * - Integrated UE5 stat tracking (Insights profiler)
 * - Thread-safe completion checking
 * - Built-in execution time tracking
 * - Manual cleanup control
 *
 * v6.0 Updates:
 * - Now runs RunMissionAssignment() instead of RunTeamMCTSWithMissions()
 * - Returns FMissionAssignment instead of TMap
 * - Takes agents and Missions directly (no TeamObservation or MissionManager)
 *
 * Usage:
 *   auto* Task = new FAsyncTask<FMCTSAsyncTask>(
 *       MCTS, Agents, Missions
 *   );
 *   Task->StartBackgroundTask();
 *
 *   // Later (e.g., in Tick):
 *   if (Task->IsDone()) {
 *       FMissionAssignment Assignment = Task->GetTask().GetResults();
 *       ExecutionTime = Task->GetTask().GetExecutionTime();
 *       delete Task;  // Manual cleanup required
 *   }
 */
class GAMEAI_PROJECT_API FMCTSAsyncTask : public FNonAbandonableTask
{
    friend class FAsyncTask<FMCTSAsyncTask>;

public:
    /**
     * Constructor (v6.0)
     *
     * @param InMCTS - MCTS instance to run
     * @param InAgents - Available agents for assignment
     * @param InMissions - Available Missions for assignment
     * @param InSimulations - Number of MCTS simulations (default: 500)
     * @param InCachedObservations - Pre-cached observations for thread safety
     */
    FMCTSAsyncTask(
        UMCTS* InMCTS,
        const TArray<AActor*>& InAgents,
        const TArray<UMission*>& InMissions,
        int32 InSimulations = 500,
        const TMap<AActor*, FObservationElement>& InCachedObservations = TMap<AActor*, FObservationElement>()
    )
        : MCTS(InMCTS)
        , Agents(InAgents)
        , Missions(InMissions)
        , Simulations(InSimulations)
        , CachedObservations(InCachedObservations)
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
     * Get results (call after IsComplete() returns true) - v6.0
     * @return Mission assignment with value estimates
     */
    FMissionAssignment GetResults() const { return ResultAssignment; }

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

    /** Agents available for assignment (v6.0) */
    TArray<AActor*> Agents;

    /** Missions available for assignment (v6.0) */
    TArray<UMission*> Missions;

    /** Number of MCTS simulations (v6.0) */
    int32 Simulations;

    /** Pre-cached observations for thread-safe async execution (v6.0 fix) */
    TMap<AActor*, FObservationElement> CachedObservations;

    /** Results from MCTS execution (v6.0) */
    FMissionAssignment ResultAssignment;

    /** Thread-safe completion flag */
    FThreadSafeBool bCompleted;

    /** Execution time in milliseconds */
    float ExecutionTime;
};
