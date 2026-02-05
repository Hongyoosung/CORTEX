// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "FollowerAIController.generated.h"

class UFollowerStateTreeComponent;

/**
 * Follower AI Controller - State Tree Based
 *
 * This controller manages follower agents using State Tree instead of FSM + Behavior Tree.
 *
 * Architecture:
 * - Team Leader issues strategic commands → FollowerAgentComponent
 * - FollowerAgentComponent updates context → State Tree
 * - State Tree manages tactical states (Assault, Defend, Support, etc.)
 * - RL policy selects actions within states
 *
 * Setup:
 * 1. Assign this controller to your follower pawn/character
 * 2. Ensure FollowerCharacter has FollowerAgentComponent + FollowerStateTreeComponent
 * 3. Set StateTreeAsset in the component (or via this controller)
 * 4. Controller auto-starts State Tree on possession
 */
UCLASS()
class GAMEAI_PROJECT_API AFollowerAIController : public AAIController
{
	GENERATED_BODY()

public:
	AFollowerAIController();


protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void Tick(float DeltaTime) override;


public:
	//--------------------------------------------------------------------------
	// COMPONENTS
	//--------------------------------------------------------------------------

	/** Get follower state tree component from controlled pawn */
	UFUNCTION(BlueprintPure, Category = "AI|State Tree")
	UFollowerStateTreeComponent* GetStateTreeComponent() const;

	//--------------------------------------------------------------------------
	// STATE TREE CONTROL
	//--------------------------------------------------------------------------
	/** Start the State Tree */
	UFUNCTION(BlueprintCallable, Category = "AI|State Tree")
	bool StartStateTree();

	/** Stop the State Tree */
	UFUNCTION(BlueprintCallable, Category = "AI|State Tree")
	void StopStateTree();

	/** Is State Tree running? */
	UFUNCTION(BlueprintPure, Category = "AI|State Tree")
	bool IsStateTreeRunning() const;

	/** Get current state name */
	UFUNCTION(BlueprintPure, Category = "AI|State Tree")
	FString GetCurrentStateName() const;

	//--------------------------------------------------------------------------
	// DEBUGGING
	//--------------------------------------------------------------------------
	/** Draw debug info for controlled follower */
	UFUNCTION(BlueprintCallable, Category = "AI|Debug")
	void DrawDebugInfo();


private:
	/** Initialize components on possession */
	void InitializeComponents();


public:
	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------
	/** Auto-start State Tree on possession */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|State Tree")
	bool bAutoStartStateTree = true;

	/** Enable debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Debug")
	bool bEnableDebugDrawing = false;


private:
	/** Cached reference to state tree component */
	UPROPERTY()
	UFollowerStateTreeComponent* CachedStateTreeComponent = nullptr;

	
};
