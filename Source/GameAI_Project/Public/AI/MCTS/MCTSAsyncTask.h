// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/AsyncWork.h"
#include "Observation/TeamObservation.h"
#include "Observation/ObservationElement.h"

// Forward declarations
class UMCTS;
class AObjectiveActor;

/**
 * v8.0: Async task for running MCTS strategy assignment in background thread
 *
 * Provides better lifecycle management than raw AsyncTask lambdas:
 * - Integrated UE5 stat tracking (Insights profiler)
 * - Thread-safe completion checking
 * - Built-in execution time tracking
 * - Manual cleanup control
 *
 * v8.0 Updates:
 * - Now runs RunStrategyAssignment() instead of RunMissionAssignment()
 * - Returns TMap<AActor*, FStrategyAssignment> instead of FMissionAssignment
 * - Takes agents and objectives directly (no Mission objects)
 *
 * Usage:
 *   auto* Task = new FAsyncTask<FMCTSAsyncTask>(
 *       MCTS, Agents, Objectives
 *   );
 *   Task->StartBackgroundTask();
 *
 *   // Later (e.g., in Tick):
 *   if (Task->IsDone()) {
 *       TMap<AActor*, FStrategyAssignment> Assignments = Task->GetTask().GetResults();
 *       ExecutionTime = Task->GetTask().GetExecutionTime();
 *       delete Task;  // Manual cleanup required
 *   }
 */
class GAMEAI_PROJECT_API FMCTSAsyncTask : public FNonAbandonableTask
{
    friend class FAsyncTask<FMCTSAsyncTask>;

public:
    /**
     * Constructor (v8.0)
     *
     * @param InMCTS - MCTS instance to run
     * @param InAgents - Available agents for assignment
     * @param InObjectives - Available objectives for assignment
     * @param InSimulations - Number of MCTS simulations (default: 500)
     * @param InCachedObservations - Pre-cached observations for thread safety
     */
    FMCTSAsyncTask(
        UMCTS* InMCTS,
        const TArray<AActor*>& InAgents,
        const TArray<AObjectiveActor*>& InObjectives,
        int32 InSimulations = 500,
        const TMap<AActor*, FObservationElement>& InCachedObservations = TMap<AActor*, FObservationElement>()
    )
        : MCTS(InMCTS)
        , Agents(InAgents)
        , Objectives(InObjectives)
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
     * Get results (call after IsComplete() returns true) - v8.0
     * @return Strategy assignments (agent -> FStrategyAssignment map)
     */
    TMap<AActor*, FStrategyAssignment> GetResults() const { return ResultAssignments; }

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

    /** Agents available for assignment (v8.0) */
    TArray<AActor*> Agents;

    /** Objectives available for assignment (v8.0) */
    TArray<AObjectiveActor*> Objectives;

    /** Number of MCTS simulations (v8.0) */
    int32 Simulations;

    /** Pre-cached observations for thread-safe async execution (v8.0) */
    TMap<AActor*, FObservationElement> CachedObservations;

    /** Results from MCTS execution (v8.0) - agent -> strategy assignment map */
    TMap<AActor*, FStrategyAssignment> ResultAssignments;

    /** Thread-safe completion flag */
    FThreadSafeBool bCompleted;

    /** Execution time in milliseconds */
    float ExecutionTime;
};
