#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Team/TeamTypes.h"
#include "RL/RLTypes.h"
#include "TacticalStateComponent.generated.h"

// Forward declarations
class AObjectiveActor;

/**
 * Tactical State Component
 *
 * Responsibilities (v8.0):
 * - Store tactical and combat parameters (from RL)
 * - Store strategy assignment (from MCTS)
 * - Manage agent state (alive/dead)
 * - Provide state queries
 *
 * This component was extracted from FollowerAgentComponent to improve separation of concerns.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UTacticalStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTacticalStateComponent();

	virtual void BeginPlay() override;

	//--------------------------------------------------------------------------
	// v8.0: STRATEGY ASSIGNMENT (from MCTS)
	//--------------------------------------------------------------------------

	/** Set strategy assignment from MCTS */
	UFUNCTION(BlueprintCallable, Category = "TacticalState|Strategy")
	void SetStrategyAssignment(const FStrategyAssignment& Assignment);

	/** Get current strategy assignment */
	UFUNCTION(BlueprintPure, Category = "TacticalState|Strategy")
	FStrategyAssignment GetStrategyAssignment() const { return CurrentAssignment; }

	/** Get assigned strategy */
	UFUNCTION(BlueprintPure, Category = "TacticalState|Strategy")
	EStrategyType GetAssignedStrategy() const { return CurrentAssignment.Strategy; }


	/** Get previous assignment (for change detection) */
	UFUNCTION(BlueprintPure, Category = "TacticalState|Strategy")
	FStrategyAssignment GetLastAssignment() const { return LastAssignment; }

	//--------------------------------------------------------------------------
	// v8.0: TACTICAL & COMBAT PARAMETERS (from RL)
	//--------------------------------------------------------------------------

	/** Set tactical parameters from RL actuator */
	UFUNCTION(BlueprintCallable, Category = "TacticalState|Parameters")
	void SetTacticalParameters(const FTacticalParameters& Params);

	/** Set combat parameters from RL actuator */
	UFUNCTION(BlueprintCallable, Category = "TacticalState|Parameters")
	void SetCombatParameters(const FCombatParameters& Params);

	/** Get current macro action (tactical + combat parameters) */
	UFUNCTION(BlueprintPure, Category = "TacticalState|Parameters")
	FMacroAction GetCurrentMacroAction() const { return CurrentMacroAction; }

	/** Get current tactical parameters */
	UFUNCTION(BlueprintPure, Category = "TacticalState|Parameters")
	FTacticalParameters GetTacticalParameters() const { return CurrentMacroAction.TacticalParams; }

	/** Get current combat parameters */
	UFUNCTION(BlueprintPure, Category = "TacticalState|Parameters")
	FCombatParameters GetCombatParameters() const { return CurrentMacroAction.CombatParams; }

	//--------------------------------------------------------------------------
	// STATE MANAGEMENT
	//--------------------------------------------------------------------------

	/** Mark agent as dead */
	UFUNCTION(BlueprintCallable, Category = "TacticalState|State")
	void MarkAsDead();

	/** Mark agent as alive (e.g., respawn) */
	UFUNCTION(BlueprintCallable, Category = "TacticalState|State")
	void MarkAsAlive();

	/** Is agent alive? */
	UFUNCTION(BlueprintPure, Category = "TacticalState|State")
	bool IsAlive() const { return bIsAlive; }

	/** Reset state for new episode */
	UFUNCTION(BlueprintCallable, Category = "TacticalState|State")
	void ResetState();

public:
	//--------------------------------------------------------------------------
	// STATE
	//--------------------------------------------------------------------------

	/** v8.0: Current strategy assignment from MCTS */
	UPROPERTY(BlueprintReadOnly, Category = "TacticalState|State")
	FStrategyAssignment CurrentAssignment;

	/** v8.0: Previous assignment (for change detection) */
	UPROPERTY(BlueprintReadOnly, Category = "TacticalState|State")
	FStrategyAssignment LastAssignment;

	/** v8.0: Current macro action (tactical + combat parameters from RL) */
	UPROPERTY(BlueprintReadOnly, Category = "TacticalState|State")
	FMacroAction CurrentMacroAction;

	/** Is agent alive? */
	UPROPERTY(BlueprintReadOnly, Category = "TacticalState|State")
	bool bIsAlive = true;

};
