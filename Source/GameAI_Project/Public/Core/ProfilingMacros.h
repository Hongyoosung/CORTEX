// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

/**
 * Performance Profiling Macros for CORTEX v6.0
 *
 * Usage:
 *   1. Declare stats in this file
 *   2. Add SCOPE_CYCLE_COUNTER(STAT_Name) in measured functions
 *   3. View in Unreal Insights or stat commands:
 *      - stat STATGROUP_AI
 *      - stat slow -ms=0.5 -depth=4
 *
 * Performance Targets (4v4 scenario):
 *   - MCTS Assignment: < 50ms (async, doesn't block gameplay)
 *   - RL Batched Inference: < 4ms (4 agents in single forward pass)
 *   - RL Single Inference: < 1.5ms (fallback if batching fails)
 *   - StateTree Execution: < 0.5ms per agent, < 2ms total (4 agents)
 *   - Observation Build: < 0.3ms per agent
 *   - Total AI Frame: < 10ms (RL + StateTree + Observation)
 */

// Stat group for AI systems
DECLARE_STATS_GROUP(TEXT("AI"), STATGROUP_CORTEXAI, STATCAT_Advanced);

// ========================================
// MCTS Performance Stats
// ========================================

/** MCTS: Complete Mission assignment process */
DECLARE_CYCLE_STAT_EXTERN(TEXT("MCTS: Mission Assignment"), STAT_MCTSAssignment, STATGROUP_AI, );

/** MCTS: Selection phase (traverse tree to leaf) */
DECLARE_CYCLE_STAT_EXTERN(TEXT("MCTS: Selection"), STAT_MCTSSelection, STATGROUP_AI, );

/** MCTS: Expansion phase (add new child node) */
DECLARE_CYCLE_STAT_EXTERN(TEXT("MCTS: Expansion"), STAT_MCTSExpansion, STATGROUP_AI, );

/** MCTS: Simulation phase (evaluate leaf node) */
DECLARE_CYCLE_STAT_EXTERN(TEXT("MCTS: Simulation"), STAT_MCTSSimulation, STATGROUP_AI, );

/** MCTS: Backpropagation phase (update ancestors) */
DECLARE_CYCLE_STAT_EXTERN(TEXT("MCTS: Backpropagation"), STAT_MCTSBackpropagation, STATGROUP_AI, );

/** MCTS: Assignment evaluation (RL value + heuristics) */
DECLARE_CYCLE_STAT_EXTERN(TEXT("MCTS: Evaluate Assignment"), STAT_MCTSEvaluate, STATGROUP_AI, );

// ========================================
// RL Policy Network Performance Stats
// ========================================

/** RL: Batched inference for multiple agents (v6.0 - Performance Critical) */
DECLARE_CYCLE_STAT_EXTERN(TEXT("RL: Batched Inference"), STAT_RLBatchedInference, STATGROUP_AI, );

/** RL: Single agent inference (fallback) */
DECLARE_CYCLE_STAT_EXTERN(TEXT("RL: Single Inference"), STAT_RLSingleInference, STATGROUP_AI, );

/** RL: Build network input (observation + Mission context) */
DECLARE_CYCLE_STAT_EXTERN(TEXT("RL: Build Network Input"), STAT_RLBuildInput, STATGROUP_AI, );

/** RL: Forward pass through network */
DECLARE_CYCLE_STAT_EXTERN(TEXT("RL: Forward Pass"), STAT_RLForwardPass, STATGROUP_AI, );

/** RL: Sample strategy from logits */
DECLARE_CYCLE_STAT_EXTERN(TEXT("RL: Sample Strategy"), STAT_RLSampleStrategy, STATGROUP_AI, );

/** RL: Get state value (for MCTS leaf evaluation) */
DECLARE_CYCLE_STAT_EXTERN(TEXT("RL: Get State Value"), STAT_RLGetStateValue, STATGROUP_AI, );

// ========================================
// StateTree Performance Stats
// ========================================

/** StateTree: Complete execution per agent */
DECLARE_CYCLE_STAT_EXTERN(TEXT("StateTree: Execution"), STAT_StateTreeExecution, STATGROUP_AI, );

/** StateTree: Execute Movement Task */
DECLARE_CYCLE_STAT_EXTERN(TEXT("StateTree: Execute Movement"), STAT_STExecuteMovement, STATGROUP_AI, );

/** StateTree: Execute Fire Task */
DECLARE_CYCLE_STAT_EXTERN(TEXT("StateTree: Execute Fire"), STAT_STExecuteFire, STATGROUP_AI, );

/** StateTree: Execute Aiming Task */
DECLARE_CYCLE_STAT_EXTERN(TEXT("StateTree: Execute Aiming"), STAT_STExecuteAiming, STATGROUP_AI, );

/** StateTree: Update Observation Evaluator */
DECLARE_CYCLE_STAT_EXTERN(TEXT("StateTree: Update Observation"), STAT_STUpdateObservation, STATGROUP_AI, );

// ========================================
// Observation System Performance Stats
// ========================================

/** Observation: Complete build process */
DECLARE_CYCLE_STAT_EXTERN(TEXT("Observation: Build"), STAT_ObservationBuild, STATGROUP_AI, );

/** Observation: Raycast perception queries */
DECLARE_CYCLE_STAT_EXTERN(TEXT("Observation: Raycasts"), STAT_ObservationRaycasts, STATGROUP_AI, );

/** Observation: Enemy info gathering */
DECLARE_CYCLE_STAT_EXTERN(TEXT("Observation: Enemy Info"), STAT_ObservationEnemyInfo, STATGROUP_AI, );

/** Observation: Tactical analysis (cover, threats) */
DECLARE_CYCLE_STAT_EXTERN(TEXT("Observation: Tactical Analysis"), STAT_ObservationTactical, STATGROUP_AI, );

/** Observation: Mission context building (v6.0) */
DECLARE_CYCLE_STAT_EXTERN(TEXT("Observation: Mission Context"), STAT_ObservationMissionContext, STATGROUP_AI, );

// ========================================
// Team Coordination Performance Stats
// ========================================

/** Team Leader: Complete coordination cycle */
DECLARE_CYCLE_STAT_EXTERN(TEXT("Team Leader: Coordination"), STAT_TeamLeaderCoordination, STATGROUP_AI, );

/** Team Leader: Broadcast assignments to followers */
DECLARE_CYCLE_STAT_EXTERN(TEXT("Team Leader: Broadcast Assignments"), STAT_TeamLeaderBroadcast, STATGROUP_AI, );

/** Follower: Update strategy (event-driven, v6.0) */
DECLARE_CYCLE_STAT_EXTERN(TEXT("Follower: Update Strategy"), STAT_FollowerUpdateStrategy, STATGROUP_AI, );

/** Follower: Execute current strategy */
DECLARE_CYCLE_STAT_EXTERN(TEXT("Follower: Execute Strategy"), STAT_FollowerExecuteStrategy, STATGROUP_AI, );

// ========================================
// Reward Calculation Performance Stats
// ========================================

/** Reward: Complete reward calculation */
DECLARE_CYCLE_STAT_EXTERN(TEXT("Reward: Calculate"), STAT_RewardCalculate, STATGROUP_AI, );

/** Reward: Mission progress reward (v6.0) */
DECLARE_CYCLE_STAT_EXTERN(TEXT("Reward: Mission Progress"), STAT_RewardMissionProgress, STATGROUP_AI, );

/** Reward: Strategy-specific reward */
DECLARE_CYCLE_STAT_EXTERN(TEXT("Reward: Strategy Reward"), STAT_RewardStrategy, STATGROUP_AI, );

// ========================================
// Memory Tracking Stats
// ========================================

/** Memory: MCTS tree size */
DECLARE_MEMORY_STAT_EXTERN(TEXT("MCTS: Tree Memory"), STAT_MCTSTreeMemory, STATGROUP_AI, );

/** Memory: RL network weights */
DECLARE_MEMORY_STAT_EXTERN(TEXT("RL: Network Memory"), STAT_RLNetworkMemory, STATGROUP_AI, );

/** Memory: Observation buffers */
DECLARE_MEMORY_STAT_EXTERN(TEXT("Observation: Buffer Memory"), STAT_ObservationBufferMemory, STATGROUP_AI, );

// ========================================
// Custom Performance Counters
// ========================================

/** Counter: MCTS simulation count per assignment */
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("MCTS: Simulation Count"), STAT_MCTSSimulationCount, STATGROUP_AI, );

/** Counter: RL inference calls per frame */
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("RL: Inference Calls"), STAT_RLInferenceCalls, STATGROUP_AI, );

/** Counter: StateTree evaluations per frame */
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("StateTree: Evaluations"), STAT_StateTreeEvaluations, STATGROUP_AI, );

/** Counter: Active agents */
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Agents: Active Count"), STAT_ActiveAgentCount, STATGROUP_AI, );

// ========================================
// Profiling Helper Macros
// ========================================

/**
 * Scoped profiling macro with automatic logging
 * Usage:
 *   CORTEX_PROFILE_SCOPE(TEXT("MyFunction"))
 *   {
 *       // Code to profile
 *   }
 */
#define CORTEX_PROFILE_SCOPE(ScopeName) \
	SCOPE_LOG_TIME_IN_SECONDS(ScopeName, nullptr)

/**
 * Conditional profiling based on verbosity level
 * Usage:
 *   CORTEX_PROFILE_VERBOSE(STAT_RLInference, bEnableVerboseProfiling)
 *   {
 *       // Code to profile only if verbose profiling enabled
 *   }
 */
#define CORTEX_PROFILE_VERBOSE(StatName, bEnabled) \
	SCOPE_CYCLE_COUNTER_IF(StatName, bEnabled)

/**
 * Profile with automatic threshold warning
 * Usage:
 *   CORTEX_PROFILE_WITH_THRESHOLD(TEXT("SlowFunction"), 5.0f)  // Warn if >5ms
 */
#define CORTEX_PROFILE_WITH_THRESHOLD(FunctionName, ThresholdMs) \
	FScopedDurationTimer ScopedTimer([](double ElapsedSeconds) { \
		float ElapsedMs = static_cast<float>(ElapsedSeconds * 1000.0); \
		if (ElapsedMs > ThresholdMs) { \
			UE_LOG(LogTemp, Warning, TEXT("%s took %.2fms (threshold: %.2fms)"), \
				FunctionName, ElapsedMs, ThresholdMs); \
		} \
	});

/**
 * Log performance summary for a frame
 * Usage:
 *   CORTEX_LOG_FRAME_PERF()  // Call once per frame
 */
#define CORTEX_LOG_FRAME_PERF() \
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FramePerformance);
