// StrategicPlannerComponent.cpp
// Phase 3: MCTS planning implementation with RAII async task

#include "Team/Components/StrategicPlannerComponent.h"
#include "AI/MCTS/MCTS.h"
#include "AI/MCTS/MCTSAsyncTask.h"
#include "Team/ObjectiveActor.h"
#include "Engine/World.h"


UStrategicPlannerComponent::UStrategicPlannerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UStrategicPlannerComponent::~UStrategicPlannerComponent()
{
	// Destructor defined in .cpp where FMCTSAsyncTask is fully defined
	// This allows TUniquePtr to correctly delete the async task
}

void UStrategicPlannerComponent::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("[StrategicPlanner] Initialized (Simulations: %d, Async: %s)"),
		MCTSSimulations, bAsyncMCTS ? TEXT("YES") : TEXT("NO"));
}

void UStrategicPlannerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// RAII: Ensure async task is completed before destruction
	if (AsyncMCTSTask)
	{
		UE_LOG(LogTemp, Log, TEXT("[StrategicPlanner] EndPlay: Ensuring async task completion"));

		AsyncMCTSTask->EnsureCompletion();  // Block until task finishes
		AsyncMCTSTask.Reset();              // TUniquePtr automatically deletes

		bMCTSRunning = false;
	}

	Super::EndPlay(EndPlayReason);
}

void UStrategicPlannerComponent::InitializeMCTS(int32 Simulations)
{
	MCTSSimulations = Simulations;

	// Create MCTS instance
	StrategicMCTS = NewObject<UMCTS>(this, TEXT("StrategicMCTS"));

	if (StrategicMCTS)
	{
		UE_LOG(LogTemp, Log, TEXT("[StrategicPlanner] MCTS initialized with %d simulations"), Simulations);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[StrategicPlanner] Failed to create MCTS instance"));
	}

	if (!bAsyncMCTS)
	{
		UE_LOG(LogTemp, Warning, TEXT("[StrategicPlanner] Synchronous MCTS enabled - may cause frame hitches"));
	}
}



void UStrategicPlannerComponent::RunStrategyAssignmentAsync(
	const TArray<AActor*>& Agents,
	const TArray<AObjectiveActor*>& Objectives)
{
	// Validation
	if (!StrategicMCTS)
	{
		UE_LOG(LogTemp, Error, TEXT("[StrategicPlanner] RunStrategyAssignmentAsync: MCTS not initialized. Call InitializeMCTS first."));
		return;
	}

	if (bMCTSRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("[StrategicPlanner] RunStrategyAssignmentAsync: MCTS already running, skipping request"));
		return;
	}

	if (Agents.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[StrategicPlanner] RunStrategyAssignmentAsync: No agents provided"));
		return;
	}

	if (Objectives.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[StrategicPlanner] RunStrategyAssignmentAsync: No objectives provided"));
		return;
	}

	// Create async task (RAII managed)
	AsyncMCTSTask = MakeUnique<FAsyncTask<FMCTSAsyncTask>>(
		StrategicMCTS,
		Agents,
		Objectives,
		MCTSSimulations,
		TMap<AActor*, FObservationElement>()  // Empty cached observations (v9.0)
	);

	// Start background execution
	AsyncMCTSTask->StartBackgroundTask();
	bMCTSRunning = true;

	if (bEnableVerboseLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[StrategicPlanner] Async MCTS started (Agents: %d, Objectives: %d, Simulations: %d)"),
			Agents.Num(), Objectives.Num(), MCTSSimulations);
	}
}

void UStrategicPlannerComponent::PollAsyncTask()
{
	if (!bMCTSRunning || !AsyncMCTSTask)
	{
		return;
	}

	// Check if task completed
	if (AsyncMCTSTask->IsDone())
	{
		ProcessCompletedTask();
	}
}

void UStrategicPlannerComponent::ProcessCompletedTask()
{
	if (!AsyncMCTSTask)
	{
		return;
	}

	// Extract results
	TMap<AActor*, FStrategyAssignment> AssignmentMap = AsyncMCTSTask->GetTask().GetResults();
	LastExecutionTime = AsyncMCTSTask->GetTask().GetExecutionTime();

	// Convert map to array
	TArray<FStrategyAssignment> Assignments;
	for (const auto& Pair : AssignmentMap)
	{
		Assignments.Add(Pair.Value);
	}

	// Extract batch key (assumes first assignment has batch key, or derive from assignments)
	// For now, generate a simple key from strategy composition
	if (Assignments.Num() > 0)
	{
		FString StrategyComposition;
		for (const auto& Assignment : Assignments)
		{
			StrategyComposition += UEnum::GetValueAsString(Assignment.Strategy).Right(1);  // First letter of strategy
		}
		LastBatchKey = StrategyComposition;
	}
	else
	{
		LastBatchKey = TEXT("Empty");
	}

	if (bEnableVerboseLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[StrategicPlanner] MCTS completed - Time: %.2fms, Assignments: %d, BatchKey: %s"),
			LastExecutionTime, Assignments.Num(), *LastBatchKey);
	}

	// Fire delegate
	OnPlanReady.Broadcast(Assignments, LastExecutionTime, LastBatchKey);

	// Cleanup (RAII)
	AsyncMCTSTask.Reset();
	bMCTSRunning = false;
}

void UStrategicPlannerComponent::RunStrategyAssignment(
	const TArray<AActor*>& Agents,
	const TArray<AObjectiveActor*>& Objectives)
{
	if (bAsyncMCTS)
	{
		RunStrategyAssignmentAsync(Agents, Objectives);
	}
	else
	{
		// Synchronous execution (blocking)
		if (!StrategicMCTS)
		{
			UE_LOG(LogTemp, Error, TEXT("[StrategicPlanner] RunStrategyAssignment: MCTS not initialized"));
			return;
		}

		UE_LOG(LogTemp, Log, TEXT("[StrategicPlanner] Running MCTS synchronously (Agents: %d, Objectives: %d, Simulations: %d)"),
			Agents.Num(), Objectives.Num(), MCTSSimulations);

		// Execute MCTS directly on game thread
		//TMap<AActor*, FStrategyAssignment> AssignmentMap = StrategicMCTS->RunStrategyAssignment(
		//	Agents,
		//	Objectives,
		//	MCTSSimulations,

		//);

		//// Convert to array and fire delegate
		//TArray<FStrategyAssignment> Assignments;
		//for (const auto& Pair : AssignmentMap)
		//{
		//	Assignments.Add(Pair.Value);
		//}

		//OnPlanReady.Broadcast(Assignments, 0.0f, TEXT("SYNC"));
	}
}
