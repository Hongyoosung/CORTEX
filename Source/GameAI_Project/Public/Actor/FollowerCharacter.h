// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/CombatStatsInterface.h"
#include "FollowerCharacter.generated.h"

class UFollowerAgentComponent;
class UFollowerStateTreeComponent;
class UTeamCommsComponent;
class UContextBridgeComponent;
class UVisualLoggerComponent;
class URLPolicyNetwork;
// v9.0 Phase 4: Forward declarations for sub-components (for wrapper API)
class UTacticalStateComponent;
class UObservationBuilderComponent;
class URLAgentComponent;
class UCombatExecutorComponent;
class URewardCalculator;
class UHealthComponent;
class UWeaponComponent;
class UAgentPerceptionComponent;
class UTeamLeaderComponent;
class AObjectiveActor;
struct FStrategyAssignment;
struct FTacticalParameters;
struct FCombatParameters;
struct FMacroAction;
struct FObservationElement;
struct FTeamObservation;


/**
 * Follower Character - State Tree Based (v9.0)
 *
 * This character integrates the hierarchical multi-agent system using State Tree.
 *
 * Architecture (v9.0 - Decomposed):
 * - FollowerAgentComponent: Coordinator, manages 4 v8.0 sub-components (Tactical, Observation, RL, Combat)
 * - FollowerStateTreeComponent: Executes tactical states (Assault, Defend, Support)
 * - TeamCommsComponent: Handles communication with team leader (auto-registration)
 * - ContextBridgeComponent: Decouples StateTree dependencies (dependency inversion pattern)
 * - VisualLoggerComponent: Debug visualization (optional)
 * - Combat stats: Health, stamina, weapon system (via HealthComponent/WeaponComponent)
 *
 * Usage:
 * 1. Spawn this character with FollowerAIController
 * 2. TeamCommsComponent auto-registers with leader (finds by "TeamLeader" tag)
 * 3. Assign State Tree asset in FollowerStateTreeComponent
 * 4. System auto-starts on BeginPlay
 *
 * v9.0 Changes:
 * - Added TeamCommsComponent for decoupled leader communication
 * - Added ContextBridgeComponent for StateTree data sharing
 * - FollowerAgentComponent writes TO ContextBridge, StateTree reads FROM it
 * - Debug visualization separated into VisualLoggerComponent
 */
UCLASS()
class GAMEAI_PROJECT_API AFollowerCharacter : public ACharacter, public ICombatStatsInterface
{
	GENERATED_BODY()

public:
	AFollowerCharacter();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	//--------------------------------------------------------------------------
	// COMBAT STATS (ICombatStatsInterface Implementation)
	// Delegates to HealthComponent and WeaponComponent
	//--------------------------------------------------------------------------
	virtual float		GetHealthPercentage_Implementation	() const override;
	virtual bool		IsAlive_Implementation				() const override;
	virtual float		GetWeaponCooldown_Implementation	() const override;
	virtual bool		CanFireWeapon_Implementation		() const override;

	//==========================================================================
	// v9.0 PHASE 4: CHARACTER WRAPPER API
	// Character-as-Central-Hub Pattern - All component access goes through character
	//==========================================================================

	//--------------------------------------------------------------------------
	// TACTICAL STATE (wraps TacticalStateComponent)
	//--------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "AI|Tactical")
	void SetStrategyAssignment(const FStrategyAssignment& Assignment);

	UFUNCTION(BlueprintPure, Category = "AI|Tactical")
	EStrategyType GetAssignedStrategy() const;

	UFUNCTION(BlueprintPure, Category = "AI|Tactical")
	FStrategyAssignment GetStrategyAssignment() const;

	UFUNCTION(BlueprintCallable, Category = "AI|Tactical")
	void SetTacticalParameters(const FTacticalParameters& Params);

	UFUNCTION(BlueprintPure, Category = "AI|Tactical")
	FTacticalParameters GetTacticalParameters() const;

	UFUNCTION(BlueprintCallable, Category = "AI|Tactical")
	void SetCombatParameters(const FCombatParameters& Params);

	UFUNCTION(BlueprintPure, Category = "AI|Tactical")
	FCombatParameters GetCombatParameters() const;

	UFUNCTION(BlueprintPure, Category = "AI|Tactical")
	FMacroAction GetCurrentMacroAction() const;

	UFUNCTION(BlueprintPure, Category = "AI|Tactical")
	FAllyContext GetAllyContext() const;

	//--------------------------------------------------------------------------
	// OBSERVATION (wraps ObservationBuilderComponent)
	//--------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "AI|Observation")
	FObservationElement BuildLocalObservation();

	UFUNCTION(BlueprintPure, Category = "AI|Observation")
	FObservationElement GetLocalObservation() const;

	UFUNCTION(BlueprintPure, Category = "AI|Observation")
	FObservationElement GetPreviousObservation() const;

	UFUNCTION(BlueprintCallable, Category = "AI|Observation")
	void UpdateLocalObservation(const FObservationElement& NewObservation);

	UFUNCTION(BlueprintCallable, Category = "AI|Observation")
	void UpdateObjectiveContext(AObjectiveActor* Friendly, AObjectiveActor* Hostile);

	UFUNCTION(BlueprintCallable, Category = "AI|Observation")
	void UpdateTeamIntel(const FTeamObservation& TeamObs);

	UFUNCTION(BlueprintCallable, Category = "AI|Observation")
	bool FindNearestCover(FVector& OutCoverLocation, float& OutDistance, const TArray<AActor*>& Enemies);

	//--------------------------------------------------------------------------
	// REINFORCEMENT LEARNING (wraps RLAgentComponent)
	//--------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "AI|RL")
	void ProvideReward(float Reward, bool bTerminal = false);

	UFUNCTION(BlueprintCallable, Category = "AI|RL")
	void AccumulateReward(float Reward);

	UFUNCTION(BlueprintPure, Category = "AI|RL")
	float GetAccumulatedReward() const;

	UFUNCTION(BlueprintPure, Category = "AI|RL")
	URewardCalculator* GetRewardCalculator() const;

	UFUNCTION(BlueprintPure, Category = "AI|RL")
	URLPolicyNetwork* GetTacticalPolicy() const;

	UFUNCTION(BlueprintPure, Category = "AI|RL")
	bool IsTacticalPolicyReady() const;

	bool IsUsingRLPolicy() const;

	//--------------------------------------------------------------------------
	// COMBAT (wraps CombatExecutorComponent)
	//--------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "AI|Combat")
	void ExecuteCombat(const FCombatParameters& Params);

	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	AActor* GetClosestEnemy(const TArray<AActor*>& Enemies) const;

	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	AActor* GetLowestHPEnemy(const TArray<AActor*>& Enemies) const;

	//--------------------------------------------------------------------------
	// TEAM COMMUNICATION (wraps TeamCommsComponent)
	//--------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "AI|Team")
	void SignalEventToLeader(EStrategicEvent Event, AActor* InstigatorActor, FVector Location, int32 Priority);

	UFUNCTION(BlueprintPure, Category = "AI|Team")
	UTeamLeaderComponent* GetTeamLeader() const;

	UFUNCTION(BlueprintPure, Category = "AI|Team")
	int32 GetTeamID() const;

	UFUNCTION(BlueprintPure, Category = "AI|Team")
	bool IsRegisteredWithLeader() const;

	//--------------------------------------------------------------------------
	// CONTEXT BRIDGE (wraps ContextBridgeComponent)
	//--------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "AI|StateTree")
	void UpdateContextBridge();

	UFUNCTION(BlueprintPure, Category = "AI|StateTree")
	UContextBridgeComponent* GetContextBridge() const;

	//--------------------------------------------------------------------------
	// LIFECYCLE (Character coordinates all components)
	//--------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "AI|Lifecycle")
	void ResetEpisode();

	UFUNCTION(BlueprintCallable, Category = "AI|Lifecycle")
	void OnEpisodeEnded(float EpisodeReward);

	UFUNCTION(BlueprintCallable, Category = "AI|Lifecycle")
	void MarkAsDead();

	UFUNCTION(BlueprintCallable, Category = "AI|Lifecycle")
	void MarkAsAlive();

	UFUNCTION(BlueprintPure, Category = "AI|State")
	bool IsAliveState() const;

	//--------------------------------------------------------------------------
	// COMPONENT ACCESS (for external systems that need direct component access)
	//--------------------------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "AI|Components")
	UFollowerAgentComponent* GetFollowerAgentComponent() const { return FollowerAgentComponent; }

	UFUNCTION(BlueprintPure, Category = "AI|Components")
	UFollowerStateTreeComponent* GetStateTreeComponent() const { return StateTreeComponent; }

	UFUNCTION(BlueprintPure, Category = "AI|Components")
	UVisualLoggerComponent* GetVisualLoggerComponent() const { return VisualLoggerComponent; }



public:
	//--------------------------------------------------------------------------
	// COMPONENTS (v9.0 - Decomposed Architecture)
	//--------------------------------------------------------------------------
	/** Follower agent component (coordinator, manages 4 v8.0 sub-components) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Core")
	UFollowerAgentComponent* FollowerAgentComponent;

	/** State Tree component (tactical state management) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Core")
	UFollowerStateTreeComponent* StateTreeComponent;

	/** Context bridge component (StateTree data bridge - dependency inversion) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|StateTree")
	UContextBridgeComponent* ContextBridgeComponent;

	/** Visual logger component (debug visualization - optional) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Debug")
	UVisualLoggerComponent* VisualLoggerComponent;

private:
	//--------------------------------------------------------------------------
	// COMPONENT COORDINATION (v8.0)
	//--------------------------------------------------------------------------
	/** Handle death event from HealthComponent and coordinate with FollowerAgentComponent */
	UFUNCTION()
	void OnHealthComponentDeath(const struct FDeathEventData& DeathEvent);

	//--------------------------------------------------------------------------
	// v9.0 PHASE 4: CACHED SUB-COMPONENT REFERENCES (Dependency Injection)
	//--------------------------------------------------------------------------
	/** Cached sub-component references (initialized once in BeginPlay) */
	UPROPERTY()
	UTacticalStateComponent* CachedTacticalState = nullptr;

	UPROPERTY()
	UObservationBuilderComponent* CachedObservationBuilder = nullptr;

	UPROPERTY()
	URLAgentComponent* CachedRLAgent = nullptr;

	UPROPERTY()
	UCombatExecutorComponent* CachedCombatExecutor = nullptr;

	UPROPERTY()
	UHealthComponent* CachedHealthComponent = nullptr;

	UPROPERTY()
	UWeaponComponent* CachedWeaponComponent = nullptr;

	UPROPERTY()
	UAgentPerceptionComponent* CachedPerceptionComponent = nullptr;

	//--------------------------------------------------------------------------
	// v9.0 PHASE 4: TEAM COMMUNICATION DATA (merged from TeamCommsComponent)
	//--------------------------------------------------------------------------
	/** Cached team leader component reference */
	UPROPERTY()
	UTeamLeaderComponent* CachedTeamLeader = nullptr;

	/** Cached team leader actor reference */
	UPROPERTY()
	AActor* CachedTeamLeaderActor = nullptr;

	/** Is currently registered with team leader? */
	bool bIsRegisteredWithLeader = false;

	/** Team ID for this follower (used to find matching leader) */
	UPROPERTY(EditAnywhere, Category = "AI|Team", meta = (AllowPrivateAccess = "true"))
	int32 TeamID = 0;

	/** Automatically register with team leader on BeginPlay */
	UPROPERTY(EditAnywhere, Category = "AI|Team", meta = (AllowPrivateAccess = "true"))
	bool bAutoRegisterWithLeader = true;

	/** Enable verbose logging for team communication debugging */
	UPROPERTY(EditAnywhere, Category = "AI|Team", meta = (AllowPrivateAccess = "true"))
	bool bEnableTeamCommsLogging = false;

	/** Find team leader actor by matching TeamID */
	AActor* FindTeamLeaderByTeamID();

	/** Resolve TeamLeaderComponent by TeamID matching */
	bool ResolveTeamLeaderComponent();

	/** Register with team leader (internal implementation) */
	bool RegisterWithLeader();

	/** Unregister from team leader (internal implementation) */
	void UnregisterFromLeader();

	//--------------------------------------------------------------------------
	// v9.0 PHASE 5: DECISION LOOP (merged from FollowerAgentComponent)
	//--------------------------------------------------------------------------
	/** Last time strategy was updated (for rate-limiting RL inference) */
	double LastStrategyUpdateTime = 0.0;

	/** Ticks since last RL update (for timeout fallback) */
	int32 TicksSinceLastUpdate = 0;

	/** Minimum interval between RL policy updates (seconds) */
	float MinStrategyUpdateInterval = 0.05f;  // 50ms = 20 Hz max

	/** Should we update strategy/tactical params this tick? (rate-limiting) */
	bool ShouldUpdateStrategy() const;

	/** Execute combat using current tactical parameters */
	void ExecuteCombatInternal();

	//--------------------------------------------------------------------------
	// v9.0 PHASE 4: INITIALIZATION (Dependency Injection Pattern)
	//--------------------------------------------------------------------------
	/** Initialize all component references (called once in BeginPlay) */
	void InitializeComponents();

	/** Inject dependencies between components (called after InitializeComponents) */
	void InjectDependencies();
};
