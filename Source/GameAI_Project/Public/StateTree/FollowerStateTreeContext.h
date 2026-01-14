// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeExecutionContext.h"
#include "Observation/ObservationElement.h"
#include "RL/RLTypes.h"
#include "Team/TeamTypes.h"
#include "FollowerStateTreeContext.generated.h"

class UFollowerAgentComponent;
class URLPolicyNetwork;
class AAIController;
class APawn;
class UTeamLeaderComponent;
class AObjectiveActor;

/**
 * State Tree Shared Context for Follower Agents (v8.0)
 *
 * Contains all data shared between State Tree states, tasks, evaluators, and conditions.
 * Replaces both Blackboard and component polling for better performance.
 *
 * This schema is optimized for Strategy-driven tactical execution:
 * 1. MCTS assigns Strategy + TargetObjective to follower
 * 2. RL policy outputs Tactical Parameters (Aggression, Cover, Spread, Risk)
 * 3. State Tree executes tactical movement via EQS (parameter-modulated)
 * 4. Rules execute combat actions (auto-targeting + auto-firing)
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FFollowerStateTreeContext
{
	GENERATED_BODY()

	//--------------------------------------------------------------------------
	// CORE COMPONENTS (Set during initialization, read-only during execution)
	//--------------------------------------------------------------------------

	/** Follower agent component reference */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UFollowerAgentComponent> FollowerComponent = nullptr;

	/** AI Controller controlling this follower */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<APawn> ControlledPawn = nullptr;

	/** Team leader component reference */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTeamLeaderComponent> TeamLeader = nullptr;

	/** RL policy network for tactical decisions */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<URLPolicyNetwork> TacticalPolicy = nullptr;

	//--------------------------------------------------------------------------
	// TACTICAL STATE (Updated by RL policy and execution tasks) - v6.0
	//--------------------------------------------------------------------------

	/** Current strategy from RL policy (v6.0 - simplified action space) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical")
	FMacroAction CurrentAction;

	/** Time in current strategy (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical")
	float TimeInStrategy = 0.0f;

	/** Progress of current action (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical")
	float ActionProgress = 0.0f;

	//--------------------------------------------------------------------------
	// STRATEGY ASSIGNMENT (v8.0 - MCTS-assigned strategy + target)
	//--------------------------------------------------------------------------

	/** Assigned strategy from MCTS */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	EStrategyType AssignedStrategy = EStrategyType::Assault;

	/** Target objective for this strategy */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	TObjectPtr<AObjectiveActor> TargetObjective = nullptr;

	/** Strategy assignment priority [0-10] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy")
	int32 StrategyPriority = 5;


	//--------------------------------------------------------------------------
	// OBSERVATION DATA (Updated every tick by evaluator)
	//--------------------------------------------------------------------------

	/** Current environmental observation (71 features) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation")
	FObservationElement CurrentObservation;

	/** Previous observation (for experience collection) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation")
	FObservationElement PreviousObservation;

	//--------------------------------------------------------------------------
	// TARGET TRACKING (Updated by perception/combat systems)
	//--------------------------------------------------------------------------

	/** Visible enemy actors */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	TArray<TObjectPtr<AActor>> VisibleEnemies;

	/** Primary target for combat */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	TObjectPtr<AActor> PrimaryTarget = nullptr;

	/** Distance to primary target (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	float DistanceToPrimaryTarget = 0.0f;

	//--------------------------------------------------------------------------
	// COVER SYSTEM (Updated by tactical tasks)
	//--------------------------------------------------------------------------

	/** Current cover actor being used */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover")
	TObjectPtr<AActor> CurrentCover = nullptr;

	/** Is agent currently in cover? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover")
	bool bInCover = false;

	/** Nearest available cover position */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover")
	FVector NearestCoverLocation = FVector::ZeroVector;

	/** Distance to nearest cover (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover")
	float DistanceToNearestCover = 0.0f;

	//--------------------------------------------------------------------------
	// MOVEMENT (Updated by movement tasks)
	//--------------------------------------------------------------------------

	/** Current movement destination */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	FVector MovementDestination = FVector::ZeroVector;

	/** Is agent currently moving? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	bool bIsMoving = false;

	/** Movement speed multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float MovementSpeedMultiplier = 1.0f;

	//--------------------------------------------------------------------------
	// REINFORCEMENT LEARNING (Updated during tactical execution)
	//--------------------------------------------------------------------------

	/** Accumulated reward this episode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	float AccumulatedReward = 0.0f;

	/** Reward from last action */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	float LastReward = 0.0f;

	/** Use RL policy vs rule-based fallback */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	bool bUseRLPolicy = true;

	/** Action received from Schola (real-time training mode) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RL")
	bool bScholaActionReceived = false;

	//--------------------------------------------------------------------------
	// STATE FLAGS (Updated by state transitions)
	//--------------------------------------------------------------------------

	/** Is follower alive? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	bool bIsAlive = true;

	/** Is follower under fire? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	bool bUnderFire = false;

	/** Has line of sight to primary target? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	bool bHasLOS = false;

	/** Is weapon ready to fire? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	bool bWeaponReady = true;

	//--------------------------------------------------------------------------
	// DEBUG
	//--------------------------------------------------------------------------

	/** Enable debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDrawDebugInfo = false;

	/** Enable verbose logging */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableDebugLog = false;
};
