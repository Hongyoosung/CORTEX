#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Team/TeamTypes.h"
#include "Observation/ObservationElement.h"
#include "RL/RLTypes.h"
#include "Observation/TeamObservation.h"
#include "FollowerAgentComponent.generated.h"

// Forward declarations
class AObjectiveActor;
// v8.0 Refactored: New component references
class UTacticalStateComponent;
class UObservationBuilderComponent;
class URLAgentComponent;
class UCombatExecutorComponent;
class URLPolicyNetwork;
// v9.0 Phase 3: Specialized manager components
class UTeamCommsComponent;
class UContextBridgeComponent;
class UVisualLoggerComponent;

/**
 * Delegate for follower events (v8.0)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnStrategyAssignmentReceived,
	FStrategyAssignment, Assignment
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnEventSignaled,
	EStrategicEvent, Event,
	AActor*, Instigator,
	int32, Priority
);

/**
 * Follower Agent Component - Tactical Execution Coordinator (v9.0 Phase 3)
 *
 * ARCHITECTURE EVOLUTION:
 * v8.0: Refactored from 1,461-line monolith into tactical coordinator
 * v9.0 Phase 3: Further decomposed into specialized manager components
 *
 * v8.0 Sub-Components (Tactical Execution):
 * - TacticalStateComponent: Strategy assignment, tactical/combat parameters
 * - ObservationBuilderComponent: Observation building, cover detection
 * - RLAgentComponent: Reward tracking, episode management, policy network
 * - CombatExecutorComponent: Combat execution, target selection
 *
 * v9.0 Phase 3 Manager Components (Communication & Context):
 * - TeamCommsComponent: Team leader communication, registration, event signaling
 * - ContextBridgeComponent: StateTree dependency decoupling, shared data board
 * - VisualLoggerComponent: Centralized debug visualization
 *
 * Responsibilities (v9.0):
 * - Coordinate tactical execution components
 * - Coordinate manager components
 * - Strategy assignment reception and synchronization
 * - RL inference and parameter updates
 * - Combat execution coordination
 *
 * Usage:
 * 1. Attach to an AI-controlled Actor (e.g., AFollowerCharacter)
 * 2. Ensure all sub-components are attached (8 total components)
 * 3. TeamComms will automatically register with leader on BeginPlay
 * 4. Leader issues strategy assignments via SetStrategyAssignment()
 * 5. Component coordinates RL inference and tactical execution
 *
 * Backwards Compatibility:
 * All public methods remain the same, internally delegating to specialized components.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UFollowerAgentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFollowerAgentComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;


	void UpdateTacticalContext(AObjectiveActor* Friendly, AObjectiveActor* Hostile, const FTeamObservation& TeamObs);

	//--------------------------------------------------------------------------
	// TEAM LEADER COMMUNICATION
	//--------------------------------------------------------------------------

	/** Signal event to team leader */
	UFUNCTION(BlueprintCallable, Category = "Follower|Team")
	void SignalEventToLeader(
		EStrategicEvent Event,
		AActor* Instigator = nullptr,
		FVector Location = FVector::ZeroVector,
		int32 Priority = 5
	);



	//--------------------------------------------------------------------------
	// STRATEGY ASSIGNMENT (v8.0)
	//--------------------------------------------------------------------------

	/** Set strategy assignment from MCTS (delegates to TacticalStateComponent) */
	UFUNCTION(BlueprintCallable, Category = "Follower|Strategy")
	void SetStrategyAssignment(const FStrategyAssignment& Assignment);


	//--------------------------------------------------------------------------
	// INDIVIDUAL STRATEGY (v8.0: MCTS-Assigned, RL-Controlled Parameters)
	//--------------------------------------------------------------------------

	/** Get strategy assigned by MCTS (delegates to TacticalStateComponent) */
	UFUNCTION(BlueprintPure, Category = "Follower|Strategy")
	EStrategyType GetAssignedStrategy() const;

	/** v7.0 DEPRECATED: Get current strategy (delegates to TacticalStateComponent) */
	UFUNCTION(BlueprintPure, Category = "Follower|Strategy", meta = (DeprecatedFunction, DeprecationMessage = "v8.0: Strategy is now assigned by MCTS, not RL. Use GetAssignedStrategy() instead."))
	EStrategyType GetCurrentStrategy() const;

	/** Get current macro action (delegates to TacticalStateComponent) */
	UFUNCTION(BlueprintPure, Category = "Follower|Action")
	FMacroAction GetCurrentMacroAction() const;

	//--------------------------------------------------------------------------
	// v8.0: TACTICAL & COMBAT PARAMETER SETTERS (for Schola actuators)
	//--------------------------------------------------------------------------

	/** Set tactical parameters from RL actuator (delegates to TacticalStateComponent) */
	UFUNCTION(BlueprintCallable, Category = "Follower|Action")
	void SetTacticalParameters(const FTacticalParameters& Params);

	/** Set combat parameters from RL actuator (delegates to TacticalStateComponent) */
	UFUNCTION(BlueprintCallable, Category = "Follower|Action")
	void SetCombatParameters(const FCombatParameters& Params);

	/** Get current tactical parameters (delegates to TacticalStateComponent) */
	UFUNCTION(BlueprintPure, Category = "Follower|Action")
	FTacticalParameters GetTacticalParameters() const;

	/** Get current combat parameters (delegates to TacticalStateComponent) */
	UFUNCTION(BlueprintPure, Category = "Follower|Action")
	FCombatParameters GetCombatParameters() const;

	/** Get ally context for support strategy (delegates to ObservationBuilderComponent) */
	UFUNCTION(BlueprintPure, Category = "Follower|Strategy")
	FAllyContext GetAllyContext() const;

	//--------------------------------------------------------------------------
	// STATE MANAGEMENT
	//--------------------------------------------------------------------------

	/** Mark follower as dead (delegates to TacticalStateComponent & CombatExecutorComponent) */
	UFUNCTION(BlueprintCallable, Category = "Follower|State")
	void MarkAsDead();

	/** Mark follower as alive (delegates to TacticalStateComponent & CombatExecutorComponent) */
	UFUNCTION(BlueprintCallable, Category = "Follower|State")
	void MarkAsAlive();

	//--------------------------------------------------------------------------
	// OBSERVATION
	//--------------------------------------------------------------------------

	/** Update local observation (delegates to ObservationBuilderComponent) */
	UFUNCTION(BlueprintCallable, Category = "Follower|Observation")
	void UpdateLocalObservation(const FObservationElement& NewObservation);

	/** Get local observation (delegates to ObservationBuilderComponent) */
	UFUNCTION(BlueprintPure, Category = "Follower|Observation")
	FObservationElement GetLocalObservation() const;

	/** Build observation from current actor state (delegates to ObservationBuilderComponent) */
	UFUNCTION(BlueprintCallable, Category = "Follower|Observation")
	FObservationElement BuildLocalObservation();



	//--------------------------------------------------------------------------
	// REINFORCEMENT LEARNING
	//--------------------------------------------------------------------------

	/** Provide reward feedback to RL policy (delegates to RLAgentComponent) */
	UFUNCTION(BlueprintCallable, Category = "Follower|RL")
	void ProvideReward(float Reward, bool bTerminal = false);

	/** Accumulate reward (delegates to RLAgentComponent) */
	UFUNCTION(BlueprintCallable, Category = "Follower|RL")
	void AccumulateReward(float Reward);

	/** Get accumulated reward this episode (delegates to RLAgentComponent) */
	UFUNCTION(BlueprintPure, Category = "Follower|RL")
	float GetAccumulatedReward() const;

	/** Reset episode (delegates to all sub-components) */
	UFUNCTION(BlueprintCallable, Category = "Follower|RL")
	void ResetEpisode();

	/** Called when episode ends (delegates to RLAgentComponent) */
	UFUNCTION(BlueprintCallable, Category = "Follower|RL")
	void OnEpisodeEnded(float EpisodeReward);

	/** Is tactical policy ready for queries? (delegates to RLAgentComponent) */
	UFUNCTION(BlueprintPure, Category = "Follower|RL")
	bool IsTacticalPolicyReady() const;

	bool IsUsingRLPolicy() const;

	URLPolicyNetwork* GetTacticalPolicy() const;

	

	//--------------------------------------------------------------------------
	// UTILITY
	//--------------------------------------------------------------------------

	/** Get Team ID */
	UFUNCTION(BlueprintPure, Category = "Follower|Team")
	int32 GetTeamID() const;

	/** Is follower alive? (delegates to TacticalStateComponent) */
	UFUNCTION(BlueprintPure, Category = "Follower|State")
	bool GetIsAlive() const;

	/** Is registered with team leader? */
	UFUNCTION(BlueprintPure, Category = "Follower|Team")
	bool IsRegisteredWithLeader() const;

	/** Draw debug info */
	UFUNCTION(BlueprintCallable, Category = "Follower|Debug")
	void DrawDebugInfo();

	//--------------------------------------------------------------------------
	// COMBAT EXECUTION (v8.0)
	//--------------------------------------------------------------------------

	/** Execute combat actions (delegates to CombatExecutorComponent) */
	UFUNCTION(BlueprintCallable, Category = "Follower|Combat")
	void ExecuteCombat();


	/** * 리더에 의해 호출됨: 팀의 목표 및 관측 정보를 주입받음 (Push 방식)
	 * v10.0: 팔로워는 더 이상 리더의 IntelManager를 직접 참조하지 않음
	 */
	/** 리더로부터 전략 과제를 할당받음 */
	void AssignStrategy(FStrategyAssignment NewAssignment);

public:
	//--------------------------------------------------------------------------
	// v8.0 REFACTORED: SUB-COMPONENT REFERENCES
	//--------------------------------------------------------------------------

	/** Tactical state component (strategy assignment, tactical/combat parameters) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|Components")
	UTacticalStateComponent* TacticalState = nullptr;

	/** Observation builder component (observation building, cover detection) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|Components")
	UObservationBuilderComponent* ObservationBuilder = nullptr;

	/** RL agent component (reward tracking, episode management) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|Components")
	URLAgentComponent* RLAgent = nullptr;

	/** Combat executor component (combat execution, target selection) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|Components")
	UCombatExecutorComponent* CombatExecutor = nullptr;

	//--------------------------------------------------------------------------
	// v9.0 PHASE 3: SPECIALIZED MANAGER COMPONENTS
	//--------------------------------------------------------------------------

	/** Team communications component (leader registration, event signaling) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|Components")
	UTeamCommsComponent* TeamComms = nullptr;

	/** Context bridge component (StateTree dependency decoupling) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|Components")
	UContextBridgeComponent* ContextBridge = nullptr;

	/** Visual logger component (debug visualization) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|Components")
	UVisualLoggerComponent* VisualLogger = nullptr;

	//--------------------------------------------------------------------------
	// EVENTS
	//--------------------------------------------------------------------------

	/** v8.0: Fired when strategy assignment is received from leader */
	UPROPERTY(BlueprintAssignable, Category = "Follower|Events")
	FOnStrategyAssignmentReceived OnStrategyAssignmentReceived;

	/** Fired when event is signaled to leader */
	UPROPERTY(BlueprintAssignable, Category = "Follower|Events")
	FOnEventSignaled OnEventSignaled;

private:
	//--------------------------------------------------------------------------
	// BACKWARDS COMPATIBILITY (v7.0 DEPRECATED - Temporary for observation building)
	//--------------------------------------------------------------------------

	/** v7.0 DEPRECATED: Current strategy - Used only for observation AssignedStrategyIndex */
	EStrategyType CurrentStrategy = EStrategyType::Assault;

	//--------------------------------------------------------------------------
	// v9.0: EVENT-BASED INITIALIZATION
	//--------------------------------------------------------------------------


	/** Flag to track if observation builder is initialized */
	bool bObservationBuilderInitialized = false;

	//--------------------------------------------------------------------------
	// EVENT-DRIVEN STRATEGY UPDATES
	//--------------------------------------------------------------------------

	/** Ticks since last strategy update */
	int32 TicksSinceLastUpdate = 0;

	/** Last time strategy was updated */
	double LastStrategyUpdateTime = 0.0;

	/** Minimum time between strategy updates */
	float MinStrategyUpdateInterval = 0.05f;


	/**
	 * Check if strategy should be recomputed (v6.0)
	 * Only updates on significant events to reduce inference cost
	 */
	bool ShouldUpdateStrategy() const;
};
