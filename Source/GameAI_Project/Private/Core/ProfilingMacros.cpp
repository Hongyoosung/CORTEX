// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/ProfilingMacros.h"

// ========================================
// Define Stats (Implementation)
// ========================================

// MCTS Stats
DEFINE_STAT(STAT_MCTSAssignment);
DEFINE_STAT(STAT_MCTSSelection);
DEFINE_STAT(STAT_MCTSExpansion);
DEFINE_STAT(STAT_MCTSSimulation);
DEFINE_STAT(STAT_MCTSBackpropagation);
DEFINE_STAT(STAT_MCTSEvaluate);

// RL Policy Network Stats
DEFINE_STAT(STAT_RLBatchedInference);
DEFINE_STAT(STAT_RLSingleInference);
DEFINE_STAT(STAT_RLBuildInput);
DEFINE_STAT(STAT_RLForwardPass);
DEFINE_STAT(STAT_RLSampleStrategy);
DEFINE_STAT(STAT_RLGetStateValue);

// StateTree Stats
DEFINE_STAT(STAT_StateTreeExecution);
DEFINE_STAT(STAT_STExecuteMovement);
DEFINE_STAT(STAT_STExecuteFire);
DEFINE_STAT(STAT_STExecuteAiming);
DEFINE_STAT(STAT_STUpdateObservation);

// Observation Stats
DEFINE_STAT(STAT_ObservationBuild);
DEFINE_STAT(STAT_ObservationRaycasts);
DEFINE_STAT(STAT_ObservationEnemyInfo);
DEFINE_STAT(STAT_ObservationTactical);
DEFINE_STAT(STAT_ObservationMissionContext);

// Team Coordination Stats
DEFINE_STAT(STAT_TeamLeaderCoordination);
DEFINE_STAT(STAT_TeamLeaderBroadcast);
DEFINE_STAT(STAT_FollowerUpdateStrategy);
DEFINE_STAT(STAT_FollowerExecuteStrategy);

// Reward Stats
DEFINE_STAT(STAT_RewardCalculate);
DEFINE_STAT(STAT_RewardMissionProgress);
DEFINE_STAT(STAT_RewardStrategy);

// Memory Stats
DEFINE_STAT(STAT_MCTSTreeMemory);
DEFINE_STAT(STAT_RLNetworkMemory);
DEFINE_STAT(STAT_ObservationBufferMemory);

// Counter Stats
DEFINE_STAT(STAT_MCTSSimulationCount);
DEFINE_STAT(STAT_RLInferenceCalls);
DEFINE_STAT(STAT_StateTreeEvaluations);
DEFINE_STAT(STAT_ActiveAgentCount);
