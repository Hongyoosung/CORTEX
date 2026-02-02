// StrategicPlannerComponent.h
// Phase 3: MCTS planning with RAII async task (extracted from TeamLeaderComponent)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Team/TeamTypes.h"
#include "Async/AsyncWork.h"
#include "AI/MCTS/MCTSAsyncTask.h"  // Required for TUniquePtr<FAsyncTask<FMCTSAsyncTask>>
#include "StrategicPlannerComponent.generated.h"

// Forward declarations
class UMCTS;
class AObjectiveActor;

/**
 * Delegate fired when MCTS planning completes.
 * @param Assignments - Strategy assignments for agents
 * @param ExecutionTimeMs - Time taken by MCTS (milliseconds)
 * @param BatchKey - Selected batch key
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnPlanReady,
	const TArray<FStrategyAssignment>&, Assignments,
	float, ExecutionTimeMs,
	FString, BatchKey
);

/**
 * Strategic Planner Component
 *
 * Manages MCTS-based strategic planning with async execution.
 * Uses RAII (TUniquePtr) for automatic async task cleanup.
 *
 * Key Features:
 * - Async MCTS execution (non-blocking)
 * - RAII task management (no manual delete required)
 * - Batch-based strategy assignment (v8.0)
 * - Thread-safe polling
 * - Automatic cleanup on EndPlay
 *
 * Usage:
 * 1. Call InitializeMCTS(simulations)
 * 2. Call RunStrategyAssignmentAsync(agents, objectives)
 * 3. Poll with PollAsyncTask() in Tick
 * 4. Listen to OnPlanReady delegate for results
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UStrategicPlannerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStrategicPlannerComponent();

	// Custom destructor required for TUniquePtr with forward-declared type
	virtual ~UStrategicPlannerComponent();

	// ========== Lifecycle ==========

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ========== Configuration ==========

	/**
	 * Initialize MCTS with simulation count.
	 * Must be called before RunStrategyAssignmentAsync.
	 */
	UFUNCTION(BlueprintCallable, Category = "Strategic Planner")
	void InitializeMCTS(int32 Simulations = 500);

	/**
	 * Set MCTS instance directly (alternative to InitializeMCTS).
	 */
	UFUNCTION(BlueprintCallable, Category = "Strategic Planner")
	void SetMCTS(UMCTS* InMCTS);

	// ========== Planning Execution ==========

	/**
	 * Run strategy assignment asynchronously.
	 * Fires OnPlanReady when complete.
	 *
	 * @param Agents - Available agents for assignment
	 * @param Objectives - Available objectives
	 */
	UFUNCTION(BlueprintCallable, Category = "Strategic Planner")
	void RunStrategyAssignmentAsync(
		const TArray<AActor*>& Agents,
		const TArray<AObjectiveActor*>& Objectives
	);

	/**
	 * Poll async task for completion.
	 * Call this in TeamLeader's Tick.
	 * Automatically fires OnPlanReady when task completes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Strategic Planner")
	void PollAsyncTask();

	// ========== Queries ==========

	/**
	 * Check if MCTS is currently running.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Strategic Planner")
	bool IsMCTSRunning() const { return bMCTSRunning; }

	/**
	 * Get last MCTS execution time (milliseconds).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Strategic Planner")
	float GetLastExecutionTime() const { return LastExecutionTime; }

	/**
	 * Get last selected batch key.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Strategic Planner")
	FString GetLastBatchKey() const { return LastBatchKey; }

	// ========== Events ==========

	/**
	 * Fired when MCTS planning completes.
	 * Subscribe to this to receive strategy assignments.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Strategic Planner")
	FOnPlanReady OnPlanReady;

	// ========== Configuration Properties ==========

	/** Number of MCTS simulations per planning cycle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategic Planner")
	int32 MCTSSimulations = 500;

	/** Enable detailed logging */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategic Planner")
	bool bEnableVerboseLogging = false;

private:
	/** MCTS instance */
	UPROPERTY()
	UMCTS* StrategicMCTS = nullptr;

	/**
	 * Async MCTS task (RAII managed via TUniquePtr).
	 * Automatically cleaned up when reset or component destroyed.
	 */
	TUniquePtr<FAsyncTask<FMCTSAsyncTask>> AsyncMCTSTask;

	/** Is MCTS currently running? */
	bool bMCTSRunning = false;

	/** Last execution time (milliseconds) */
	float LastExecutionTime = 0.0f;

	/** Last selected batch key */
	FString LastBatchKey;

	/** Process completed async task */
	void ProcessCompletedTask();
};
