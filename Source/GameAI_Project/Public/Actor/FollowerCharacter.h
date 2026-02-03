// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/CombatStatsInterface.h"
#include "FollowerCharacter.generated.h"


class UFollowerStateTreeComponent;
class UTeamCommsComponent;
class UContextBridgeComponent;
class UVisualLoggerComponent;
class URLPolicyNetwork;
class UTacticalStateComponent;
class UObservationBuilderComponent;
class URLAgentComponent;
class UCombatExecutorComponent;
class URewardCalculator;
class UHealthComponent;
class UWeaponComponent;
class UAgentPerceptionComponent;
class ALeaderCharacter;
class AObjectiveActor;
struct FStrategyAssignment;
struct FTacticalParameters;
struct FCombatParameters;
struct FMacroAction;
struct FObservationElement;
struct FTeamObservation;


/** //==========================================================================
 * Follower Character - State Tree Based (v9.0)
 *
 * This character integrates the hierarchical multi-agent system using State Tree.
 *
 * Architecture (v9.0 - Decomposed):
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
 */ //==========================================================================
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

	//==========================================================================
	// COMBAT STATS (ICombatStatsInterface Implementation)
	// Delegates to HealthComponent and WeaponComponent
	//==========================================================================
	virtual float		GetHealthPercentage_Implementation	() const override;
	virtual bool		IsAlive_Implementation				() const override;
	virtual float		GetWeaponCooldown_Implementation	() const override;
	virtual bool		CanFireWeapon_Implementation		() const override;



	//==========================================================================
	// TACTICAL STATE (wraps TacticalStateComponent)
	//==========================================================================
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

	//==========================================================================
	// OBSERVATION (wraps ObservationBuilderComponent)
	//==========================================================================
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

	//==========================================================================
	// REINFORCEMENT LEARNING (wraps RLAgentComponent)
	//==========================================================================
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

	//==========================================================================
	// COMBAT (wraps CombatExecutorComponent)
	//==========================================================================
	UFUNCTION(BlueprintCallable, Category = "AI|Combat")
	void ExecuteCombat(const FCombatParameters& Params);

	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	AActor* GetClosestEnemy(const TArray<AActor*>& Enemies) const;

	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	AActor* GetLowestHPEnemy(const TArray<AActor*>& Enemies) const;

	//==========================================================================
	// TEAM COMMUNICATION (wraps TeamCommsComponent)
	//==========================================================================
	UFUNCTION(BlueprintPure, Category = "AI|Team")
	ALeaderCharacter* GetTeamLeader() const;

	UFUNCTION(BlueprintPure, Category = "AI|Team")
	int32 GetTeamID() const;


	//==========================================================================
	// LIFECYCLE (Character coordinates all components)
	//==========================================================================
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

	//==========================================================================
	// COMPONENT ACCESS (for external systems that need direct component access)
	//==========================================================================
	UFUNCTION(BlueprintPure, Category = "AI|Components")
	UFollowerStateTreeComponent* GetStateTreeComponent() const { return StateTreeComponent; }

	UFUNCTION(BlueprintPure, Category = "AI|Components")
	UVisualLoggerComponent* GetVisualLoggerComponent() const { return VisualLoggerComponent; }



private:
	//==========================================================================
	// TEAM COMMUNICATION HELPERS
	//==========================================================================
	/** Find team leader actor by matching TeamID */
	void FindTeamByTeamID();


	//==========================================================================
	// DECISION LOOP HELPERS
	//==========================================================================
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
	

	//==========================================================================
	// EVENT HANDLERS
	//==========================================================================
	/** Handle death event from HealthComponent and coordinate with FollowerAgentComponent */
	UFUNCTION()
	void OnHealthComponentDeath(const struct FDeathEventData& DeathEvent);


public:
	//============= COMPONENTS =================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Core")
	ALeaderCharacter*				TeamLeader;

	/** State Tree component (tactical state management) */
	UPROPERTY(VisibleAnywhere,		BlueprintReadOnly, Category = "AI|Components|Core")
	UFollowerStateTreeComponent*	StateTreeComponent;

	/** Context bridge component (StateTree data bridge - dependency inversion) */
	UPROPERTY(VisibleAnywhere,		BlueprintReadOnly, Category = "AI|Components|StateTree")
	UContextBridgeComponent*		ContextBridgeComponent;

	/** Visual logger component (debug visualization - optional) */
	UPROPERTY(VisibleAnywhere,		BlueprintReadOnly, Category = "AI|Components|Debug")
	UVisualLoggerComponent*			VisualLoggerComponent;


private:
	//============= sub-component references  =================
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


	//============= TEAM COMMUNICATION DATA   =================


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


	//============= DECISION LOOP   =================
	/** Last time strategy was updated (for rate-limiting RL inference) */
	double LastStrategyUpdateTime = 0.0;

	/** Ticks since last RL update (for timeout fallback) */
	int32 TicksSinceLastUpdate = 0;

	/** Minimum interval between RL policy updates (seconds) */
	float MinStrategyUpdateInterval = 0.05f;  // 50ms = 20 Hz max
};
