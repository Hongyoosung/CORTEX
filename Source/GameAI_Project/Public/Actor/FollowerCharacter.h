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
class UObservationBuilderComponent;
class UCombatExecutorComponent;
class UScholaAgentComponent;
class URewardCalculator;
class UHealthComponent;
class UWeaponComponent;
class UAgentPerceptionComponent;
class ALeaderCharacter;
class AObjectiveActor;
struct FStrategyAssignment;
struct FMacroAction;
struct FObservationElement;


/** //==========================================================================
 * Follower Character - State Tree Based
 *
 * This character integrates the hierarchical multi-agent system using State Tree.
 *
 * Architecture (Decomposed):
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

	//============================================================
	// COMBAT STATS (ICombatStatsInterface Implementation)
	// Delegates to HealthComponent and WeaponComponent
	//============================================================
	virtual float		GetHealthPercentage_Implementation	() const override;
	virtual bool		IsAlive_Implementation				() const override;
	virtual float		GetWeaponCooldown_Implementation	() const override;
	virtual bool		CanFireWeapon_Implementation		() const override;



	//============================================================
	// TACTICAL STATE (wraps TacticalStateComponent)
	//============================================================
	UFUNCTION(BlueprintPure, Category = "AI|Tactical")
	FStrategyAssignment GetStrategyAssignment() const;

	UFUNCTION(BlueprintPure, Category = "AI|Tactical")
	FMacroAction		GetCurrentMacroAction() const;

	UFUNCTION(BlueprintPure, Category = "AI|Tactical")
	FAllyContext		GetAllyContext() const;

	void				SetStrategyAssignment(const FStrategyAssignment& Assignment);

	void				SetMacroAction(const FMacroAction& MacroAction);


	//============================================================
	// OBSERVATION (wraps ObservationBuilderComponent)
	//============================================================
	
	UFUNCTION(BlueprintPure, Category = "AI|Observation")
	FObservationElement GetLocalObservation() const;

	UFUNCTION(BlueprintPure, Category = "AI|Observation")
	FObservationElement GetPreviousObservation() const;

	UFUNCTION(BlueprintCallable, Category = "AI|Observation")
	void				UpdateLocalObservation(const FObservationElement& NewObservation);

	UFUNCTION(BlueprintCallable, Category = "AI|Observation")
	void				UpdateObjectiveContext(AObjectiveActor* Friendly, AObjectiveActor* Hostile);

	UFUNCTION(BlueprintCallable, Category = "AI|Observation")
	FObservationElement BuildLocalObservation();

	UFUNCTION(BlueprintCallable, Category = "AI|Observation")
	bool				FindNearestCover(FVector& OutCoverLocation, float& OutDistance, const TArray<AActor*>& Enemies);

	void				RegisterVisibleEnemy(AActor* Enemy);


	AObjectiveActor*	GetFriendlyObjective	() const;
	AObjectiveActor*	GetHostileObjective		() const;


	//============================================================
	// REINFORCEMENT LEARNING (wraps RLAgentComponent)
	//============================================================
	UFUNCTION(BlueprintCallable, Category = "AI|RL")
	void				ProvideReward(float Reward);

	UFUNCTION(BlueprintCallable, Category = "AI|RL")
	URewardCalculator*	GetRewardCalculator() const;

	UFUNCTION(BlueprintCallable, Category = "AI|RL")
	float				GetCurrentReward() const;

	UFUNCTION(BlueprintPure, Category = "AI|RL")
	UObservationBuilderComponent* GetObservationBuilder() const;

	UFUNCTION(BlueprintPure, Category = "AI|RL")
	URLPolicyNetwork*	GetTacticalPolicy() const;

	UFUNCTION(BlueprintPure, Category = "AI|RL")
	bool				IsTacticalPolicyReady() const;


	//============================================================
	// COMBAT (wraps CombatExecutorComponent)
	//============================================================
	UFUNCTION(BlueprintCallable, Category = "AI|Combat")
	void				ExecuteCombat(const FCombatParameters& Params);

	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	AActor*				GetClosestEnemy(const TArray<AActor*>& Enemies) const;

	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	AActor*				GetLowestHPEnemy(const TArray<AActor*>& Enemies) const;


	//============================================================
	// TEAM COMMUNICATION (wraps TeamCommsComponent)
	//============================================================
	UFUNCTION(BlueprintPure, Category = "AI|Team")
	ALeaderCharacter*	GetTeamLeader() const;

	UFUNCTION(BlueprintPure, Category = "AI|Team")
	int32				GetTeamID() const;


	//============================================================
	// LIFECYCLE (Character coordinates all components)
	//============================================================
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


	//============================================================
	// COMPONENT ACCESS (for external systems that need direct component access)
	//============================================================
	UFUNCTION(BlueprintPure, Category = "AI|Components")
	UFollowerStateTreeComponent*	GetStateTreeComponent()		const { return StateTreeComponent; }

	UFUNCTION(BlueprintPure, Category = "AI|Components")
	UVisualLoggerComponent*			GetVisualLoggerComponent()	const { return VisualLoggerComponent; }



private:
	//============================================================
	// TEAM COMMUNICATION HELPERS
	//============================================================
	void FindTeamByTeamID();


	//============================================================
	//  INITIALIZATION (Dependency Injection Pattern)
	//============================================================
	void InitializeComponents();
	void InjectDependencies();
	

	//============================================================
	// EVENT HANDLERS
	//============================================================
	UFUNCTION()
	void OnHealthComponentDeath(const struct FDeathEventData& DeathEvent);


public:
	//============= COMPONENTS =================

	/** State Tree component (tactical state management) */
	UPROPERTY(VisibleAnywhere,		BlueprintReadOnly, Category = "AI|Components|Core")
	UFollowerStateTreeComponent*	StateTreeComponent;

	/** Context bridge component (StateTree data bridge - dependency inversion) */
	UPROPERTY(VisibleAnywhere,		BlueprintReadOnly, Category = "AI|Components|StateTree")
	UContextBridgeComponent*		ContextBridgeComponent;

	/** Visual logger component (debug visualization - optional) */
	UPROPERTY(VisibleAnywhere,		BlueprintReadOnly, Category = "AI|Components|Debug")
	UVisualLoggerComponent*			VisualLoggerComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Components|Debug")
	UScholaAgentComponent*			ScholaAgentComponent;


private:
	//============= sub-component references  =================
	UPROPERTY()
	UObservationBuilderComponent*	ObservationBuilder;

	UPROPERTY()
	UCombatExecutorComponent*		CombatExecutor;

	UPROPERTY()
	UHealthComponent*				HealthComponent;

	UPROPERTY()
	UWeaponComponent*				WeaponComponent;

	UPROPERTY()
	UAgentPerceptionComponent*		PerceptionComponent;

	UPROPERTY()
	URewardCalculator*				RewardCalculatorComponent;


	//============= TEAM COMMUNICATION DATA   =================
	UPROPERTY()
	ALeaderCharacter* TeamLeader;


	FStrategyAssignment CurrentAssigned;
	FMacroAction CurrentMacroAction;
	bool bIsAlive = true;




	/** Is currently registered with team leader? */
	bool bIsRegisteredWithLeader;

	/** Team ID for this follower (used to find matching leader) */
	UPROPERTY(EditAnywhere, Category = "AI|Team", meta = (AllowPrivateAccess = "true"))
	int32 TeamID;

	/** Automatically register with team leader on BeginPlay */
	UPROPERTY(EditAnywhere, Category = "AI|Team", meta = (AllowPrivateAccess = "true"))
	bool bAutoRegisterWithLeader;

	/** Enable verbose logging for team communication debugging */
	UPROPERTY(EditAnywhere, Category = "AI|Team", meta = (AllowPrivateAccess = "true"))
	bool bEnableTeamCommsLogging;


	//============= DECISION LOOP   =================
	/** Last time strategy was updated (for rate-limiting RL inference) */
	double LastStrategyUpdateTime;

	/** Ticks since last RL update (for timeout fallback) */
	int32 TicksSinceLastUpdate;

	/** Minimum interval between RL policy updates (seconds) */
	float MinStrategyUpdateInterval;  // 50ms = 20 Hz max
};
