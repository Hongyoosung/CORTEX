#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TeamTypes.h"
#include "Observation/ObservationElement.h"
#include "RL/RLTypes.h"
#include "Observation/TeamObservation.h"
#include "FollowerAgentComponent.generated.h"

// Forward declarations
class UTeamLeaderComponent;
class AObjectiveActor;
// v8.0 Refactored: New component references
class UTacticalStateComponent;
class UObservationBuilderComponent;
class URLAgentComponent;
class UCombatExecutorComponent;

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
 * Follower Agent Component - Tactical Execution (v8.0 Refactored)
 *
 * ARCHITECTURE CHANGE (v8.0):
 * This component has been refactored from a 1,461-line monolithic class into a coordinator
 * that delegates to specialized sub-components:
 *
 * - TacticalStateComponent: Strategy assignment, tactical/combat parameters, state management
 * - ObservationBuilderComponent: Observation building, cover detection
 * - RLAgentComponent: Reward tracking, episode management, policy network interaction
 * - CombatExecutorComponent: Combat execution, target selection, event handling
 *
 * Responsibilities (Refactored):
 * - Team leader registration/communication
 * - Component lifecycle management
 * - Strategy assignment reception (delegates to TacticalStateComponent)
 * - Component coordination
 *
 * Usage:
 * 1. Attach to an AI-controlled Actor (e.g., AGameAICharacter)
 * 2. Ensure sub-components are also attached (TacticalState, ObservationBuilder, RLAgent, CombatExecutor)
 * 3. Set TeamLeader reference
 * 4. Component will automatically register with leader
 * 5. Leader will issue commands, component will coordinate execution
 *
 * Backwards Compatibility:
 * All public methods remain the same, but now delegate to sub-components internally.
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


	//--------------------------------------------------------------------------
	// TEAM LEADER COMMUNICATION
	//--------------------------------------------------------------------------

	/** Register with team leader */
	UFUNCTION(BlueprintCallable, Category = "Follower|Team")
	bool RegisterWithTeamLeader();

	/** Unregister from team leader */
	UFUNCTION(BlueprintCallable, Category = "Follower|Team")
	void UnregisterFromTeamLeader();

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

	/** Get target objective from current assignment (delegates to TacticalStateComponent) */
	UFUNCTION(BlueprintPure, Category = "Follower|Strategy")
	AObjectiveActor* GetTargetObjective() const;

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
	FAllyContext GetAllyContext() const { return CachedAllyContext; }

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

	/** Find nearest cover position (delegates to ObservationBuilderComponent) */
	UFUNCTION(BlueprintCallable, Category = "Follower|Tactical")
	bool FindNearestCover(FVector& OutCoverLocation, float& OutDistance, const TArray<AActor*>& Enemies);

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

	//--------------------------------------------------------------------------
	// UTILITY
	//--------------------------------------------------------------------------

	/** Get team leader */
	UFUNCTION(BlueprintPure, Category = "Follower|Team")
	UTeamLeaderComponent* GetTeamLeader() const { return TeamLeader; }

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

	/** Get closest enemy from perception (delegates to CombatExecutorComponent) */
	UFUNCTION(BlueprintPure, Category = "Follower|Combat")
	AActor* GetClosestEnemy(const TArray<AActor*>& Enemies) const;

	/** Get enemy with lowest HP (delegates to CombatExecutorComponent) */
	UFUNCTION(BlueprintPure, Category = "Follower|Combat")
	AActor* GetLowestHPEnemy(const TArray<AActor*>& Enemies) const;

public:
	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------

	/** Team leader actor (will find TeamLeaderComponent on this actor) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Config")
	AActor* TeamLeaderActor = nullptr;

	/** Team leader component reference (auto-set from TeamLeaderActor) */
	UPROPERTY(BlueprintReadOnly, Category = "Follower|Config")
	UTeamLeaderComponent* TeamLeader = nullptr;

	/** Automatically register with team leader on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Config")
	bool bAutoRegisterWithLeader = true;

	/** Auto-find team leader by tag (if TeamLeaderActor not set) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Config")
	FName TeamLeaderTag = TEXT("TeamLeader");

	/** Enable debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follower|Debug")
	bool bEnableDebugDrawing = false;

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
	// BACKWARDS COMPATIBILITY (v7.0 DEPRECATED)
	//--------------------------------------------------------------------------

	/** v7.0 DEPRECATED: Cached ally context (now in ObservationBuilderComponent) */
	FAllyContext CachedAllyContext;

	/** v7.0 DEPRECATED: Current strategy (now in TacticalStateComponent) */
	EStrategyType CurrentStrategy = EStrategyType::Assault;

	/** v7.0 DEPRECATED: Current macro action (now in TacticalStateComponent) */
	FMacroAction CurrentMacroAction;

	/** v7.0 DEPRECATED: Current assignment (now in TacticalStateComponent) */
	FStrategyAssignment CurrentAssignment;

	//--------------------------------------------------------------------------
	// EVENT-DRIVEN STRATEGY UPDATES
	//--------------------------------------------------------------------------

	/** Last enemy count at strategy update */
	int32 LastEnemyCount = 0;

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
