// ContextBridgeComponent.h
// Phase 3: StateTree context decoupling (dependency inversion for StateTree)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Team/TeamTypes.h"
#include "RL/RLTypes.h"
#include "ContextBridgeComponent.generated.h"

// Forward declarations
class AObjectiveActor;
class AActor;

/**
 * Delegate fired when context data changes.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnContextUpdated);

/**
 * Context Bridge Component
 *
 * Decouples FollowerAgentComponent from StateTree by providing a shared data board.
 * StateTree reads from this component instead of directly accessing FollowerAgent internals.
 *
 * Purpose:
 * - Inversion of control: FollowerAgent writes TO bridge, StateTree reads FROM bridge
 * - No direct dependency on StateTree schema from core logic
 * - Clean interface for StateTree tasks/evaluators
 * - Easier testing and mocking
 *
 * Usage:
 * 1. Attach to follower actor
 * 2. FollowerAgent updates context via SetXXX methods
 * 3. StateTree tasks query context via GetXXX methods
 * 4. Subscribe to OnContextUpdated for change notifications
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UContextBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UContextBridgeComponent();

	// ========== Context Setters (FollowerAgent writes here) ==========

	/**
	 * Set current strategy assignment.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Bridge")
	void SetStrategy(EStrategyType InStrategy);

	/**
	 * Set target objective actor.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Bridge")
	void SetTargetObjective(AObjectiveActor* InObjective);

	/**
	 * Set tactical parameters.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Bridge")
	void SetTacticalParameters(const FTacticalParameters& InParams);

	/**
	 * Set combat parameters.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Bridge")
	void SetCombatParameters(const FCombatParameters& InParams);

	/**
	 * Set current target actor (enemy).
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Bridge")
	void SetCurrentTarget(AActor* InTarget);

	/**
	 * Set alert level (0=idle, 1=cautious, 2=combat).
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Bridge")
	void SetAlertLevel(int32 InLevel);

	/**
	 * Set whether agent is alive.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Bridge")
	void SetIsAlive(bool bInAlive);

	/**
	 * Update all context at once.
	 * Useful for bulk updates to minimize change notifications.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Bridge")
	void UpdateContext(
		EStrategyType InStrategy,
		AObjectiveActor* InObjective,
		const FTacticalParameters& InTacticalParams,
		const FCombatParameters& InCombatParams,
		AActor* InTarget = nullptr,
		int32 InAlertLevel = 0
	);

	// ========== Context Getters (StateTree reads from here) ==========

	/**
	 * Get current strategy.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Bridge")
	EStrategyType GetStrategy() const { return CurrentStrategy; }

	/**
	 * Get target objective.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Bridge")
	AObjectiveActor* GetTargetObjective() const { return TargetObjective; }

	/**
	 * Get tactical parameters.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Bridge")
	FTacticalParameters GetTacticalParameters() const { return TacticalParameters; }

	/**
	 * Get combat parameters.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Bridge")
	FCombatParameters GetCombatParameters() const { return CombatParameters; }

	/**
	 * Get current target actor.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Bridge")
	AActor* GetCurrentTarget() const { return CurrentTarget; }

	/**
	 * Get alert level.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Bridge")
	int32 GetAlertLevel() const { return AlertLevel; }

	/**
	 * Get whether agent is alive.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Bridge")
	bool GetIsAlive() const { return bIsAlive; }

	// ========== Utility ==========

	/**
	 * Reset context to defaults.
	 * Useful for episode resets.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Bridge")
	void ResetContext();

	// ========== Events ==========

	/**
	 * Fired when any context data changes.
	 * StateTree can subscribe to this for reactive updates.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Context Bridge")
	FOnContextUpdated OnContextUpdated;

	// ========== Configuration ==========

	/** Enable verbose logging for debugging */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Bridge")
	bool bEnableVerboseLogging = false;

	/** Suppress change notifications (useful for bulk updates) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Bridge")
	bool bSuppressNotifications = false;

private:
	// ========== Context State ==========

	/** Current strategy assignment */
	UPROPERTY()
	EStrategyType CurrentStrategy = EStrategyType::Assault;

	/** Target objective actor */
	UPROPERTY()
	AObjectiveActor* TargetObjective = nullptr;

	/** Tactical parameters (from RL policy) */
	UPROPERTY()
	FTacticalParameters TacticalParameters;

	/** Combat parameters (from RL policy) */
	UPROPERTY()
	FCombatParameters CombatParameters;

	/** Current target actor (enemy) */
	UPROPERTY()
	AActor* CurrentTarget = nullptr;

	/** Alert level (0=idle, 1=cautious, 2=combat) */
	int32 AlertLevel = 0;

	/** Is agent alive? */
	bool bIsAlive = true;

	/**
	 * Notify listeners of context change.
	 */
	void NotifyContextChanged();
};
