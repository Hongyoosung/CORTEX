// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/StateTreeComponent.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "FollowerStateTreeComponent.generated.h"

class AFollowerCharacter;
class UHealthComponent;
class UTeamCommsComponent;



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
	/** Find components on actor */
	AFollowerCharacter* FindFollowerCharacter();

	UHealthComponent* FindHealthComponent();

	/** Bind to follower component events */
	void BindToFollowerEvents();

	/** Handle health component death event (v9.0) */
	UFUNCTION()
	void HandleOnHealthComponentDeath(const FDeathEventData& DeathEvent);

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
	TObjectPtr<AFollowerCharacter> Follower;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Tree")
	TObjectPtr<UHealthComponent> HealthComponent;

	// v9.0 PHASE 4: TeamCommsComponent merged into FollowerCharacter (no longer a separate component)

	/** Auto-find components on same actor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Tree")
	uint8 bAutoFindFollowerComponent : 1;

	//--------------------------------------------------------------------------
	// STATE TREE CONTEXT
	//--------------------------------------------------------------------------

	/** Shared context for State Tree (accessed by all tasks/evaluators/conditions) */
	UPROPERTY(BlueprintReadOnly, Category = "State Tree")
	FFollowerStateTreeContext Context;

	int32 TickLogCounter;


private:
	//============= StateTree Event =================

	/** Event tag: Follower died */
	static const FGameplayTag Event_FollowerDied;

	/** Event tag: Follower respawned */
	static const FGameplayTag Event_FollowerRespawned;
};
