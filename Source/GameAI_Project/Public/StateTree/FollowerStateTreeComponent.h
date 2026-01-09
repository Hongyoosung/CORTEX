// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/StateTreeComponent.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "FollowerStateTreeComponent.generated.h"

class UFollowerAgentComponent;

/**
 * Follower State Tree Component
 *
 * Manages State Tree execution for follower agents.
 * Replaces both StateMachine + BehaviorTree with a unified State Tree system.
 *
 * Architecture:
 * - Team Leader issues strategic commands
 * - FollowerAgentComponent receives commands
 * - FollowerStateTreeComponent executes commands via State Tree
 * - RL policy selects tactical actions within states
 *
 * Setup:
 * 1. Add this component to your AI pawn/character
 * 2. Assign StateTreeAsset (create in editor: Right Click > Gameplay > State Tree)
 * 3. Set FollowerComponent reference (auto-found if on same actor)
 * 4. Component auto-starts State Tree on BeginPlay
 *
 * State Tree Asset Structure (Sprint 3 - Mission-Driven):
 * Root (Selector)
 * ├─ [IsAlive == false] DeadState
 * ├─ [CurrentMission != null] ExecuteMissionState
 * │  └─ ExecuteMission (universal task, handles all Mission types)
 * └─ IdleState
 *
 * Evaluators (Global):
 * - UpdateObservation (runs every tick, gathers 71-feature observation)
 * - SpatialContext (computes FActionSpaceMask based on environment, 5Hz)
 * - SyncCommand (syncs command from FollowerAgentComponent)
 */
UCLASS(ClassGroup = "StateTree", meta = (BlueprintSpawnableComponent, DisplayName = "Follower StateTree Component"))
class GAMEAI_PROJECT_API UFollowerStateTreeComponent : public UStateTreeComponent
{
	GENERATED_BODY()

public:
	UFollowerStateTreeComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual TValueOrError<void, FString> HasValidStateTreeReference() const override;
	virtual void ValidateStateTreeReference() override;

	UFUNCTION(BlueprintPure, Category = "State Tree")
    virtual TSubclassOf<UStateTreeSchema> GetSchema() const override;

	// Override to provide external data to StateTree
	virtual bool SetContextRequirements(FStateTreeExecutionContext& Context, bool bLogErrors = false) override;

	virtual bool CollectExternalData(
		const FStateTreeExecutionContext& InContext,
		const UStateTree* StateTree,
		TArrayView<const FStateTreeExternalDataDesc> Descs,
		TArrayView<FStateTreeDataView> OutDataViews
	) const override;

	//--------------------------------------------------------------------------
	// API
	//--------------------------------------------------------------------------

	/** Initialize State Tree context */
	UFUNCTION(BlueprintCallable, Category = "State Tree")
	void InitializeContext();

	/** Get State Tree context (by value - Blueprint safe) */
	UFUNCTION(BlueprintPure, Category = "State Tree")
	FFollowerStateTreeContext GetContext() const { return Context; }

	/** Get SHARED State Tree context reference (C++ only - for tasks to share data) */
	FFollowerStateTreeContext& GetSharedContext() { return Context; }
	const FFollowerStateTreeContext& GetSharedContext() const { return Context; }

	/** Update context from follower component */
	UFUNCTION(BlueprintCallable, Category = "State Tree")
	void UpdateContextFromFollower();

	/** Is State Tree currently running? */
	UFUNCTION(BlueprintPure, Category = "State Tree")
	bool IsStateTreeRunning() const;

	/** Get current state name (for debugging) */
	UFUNCTION(BlueprintPure, Category = "State Tree")
	FString GetCurrentStateName() const;


	/** Handle follower death */
	void OnFollowerDied();

	/** Handle follower respawn (episode reset) */
	void OnFollowerRespawned();

	
protected:
	/** Find FollowerAgentComponent on actor */
	UFollowerAgentComponent* FindFollowerComponent();

	/** Bind to follower component events */
	void BindToFollowerEvents();

	/** Handle Mission received from leader (v3.0) */
	UFUNCTION()
	void OnMissionReceived(UMission* Mission);

	/** Handle controller changed (for Schola Trainer possession) */
	UFUNCTION()
	void OnControllerChanged(APawn* InPawn, AController* OldController, AController* NewController);

	bool CheckRequirementsAndStart();


private:
	/** Handle StateTree run status changes */
	UFUNCTION()
	void HandleOnStateTreeRunStatusChanged(const EStateTreeRunStatus Status);

	/** Send StateTree event (helper) */
	void SendStateTreeEvent(const FGameplayTag& EventTag, FConstStructView Payload = FConstStructView());

public:
	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------
	// Note: StateTree asset is set on the base UStateTreeComponent's "StateTree" property
	// in the editor. It will automatically use FollowerStateTreeSchema due to GetRequiredStateTreeSchema()

	/** Follower agent component reference (auto-found if nullptr) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Tree")
	UFollowerAgentComponent* FollowerComponent;

	/** Auto-find FollowerAgentComponent on same actor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Tree")
	uint8 bAutoFindFollowerComponent : 1;

	//--------------------------------------------------------------------------
	// STATE TREE CONTEXT
	//--------------------------------------------------------------------------

	/** Shared context for State Tree (accessed by all tasks/evaluators/conditions) */
	UPROPERTY(BlueprintReadOnly, Category = "State Tree")
	FFollowerStateTreeContext Context;

	//--------------------------------------------------------------------------
	// STATE TREE EVENTS (for event-driven transitions)
	//--------------------------------------------------------------------------

	/** Event tag: Mission received */
	static const FGameplayTag Event_MissionReceived;

	/** Event tag: Follower died */
	static const FGameplayTag Event_FollowerDied;

	/** Event tag: Follower respawned */
	static const FGameplayTag Event_FollowerRespawned;

	int32 TickLogCounter;
};
