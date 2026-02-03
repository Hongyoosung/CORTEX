// IntelManagerComponent.h
// Phase 3: Intel tracking and observation (extracted from TeamLeaderComponent)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Observation/TeamObservation.h"
#include "TeamManagerComponent.generated.h"

// Forward declarations
class AObjectiveActor;

/**
 * Delegate fired when objectives are discovered and ready for use.
 * v9.0: Allows followers to defer observation initialization until objectives exist.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnObjectivesDiscovered);

/**
 * Manages enemy tracking, objective discovery, and team observation building.
 * Handles all intelligence gathering for the team leader.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UTeamManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTeamManagerComponent();


	//=====================================================
	// Initialization
	//=====================================================

	virtual void BeginPlay() override;



	//=====================================================
	// Team Tracking 
	//=====================================================

	UFUNCTION(BlueprintCallable, Category = "Team Manager")
	bool RegisterFollower(AActor* Follower);

	UFUNCTION(BlueprintCallable, Category = "Team Manager")
	bool UnregisterFollower(AActor* Follower);

	UFUNCTION(BlueprintCallable, Category = "Team Manager")
	TArray<AActor*> GetFollowers() const;

	UFUNCTION(BlueprintCallable, Category = "Team Manager")
	int32 GetFollowerCount() const { return RegisteredFollowers.Num(); }

	UFUNCTION(BlueprintCallable, Category = "Team Manager")
	bool IsFollowerRegistered(AActor* Follower) const;



	//=====================================================
	// ObjectiveActor Tracking
	//=====================================================

	/**
	 * Discover objectives in the world by searching for actors with specific tags.
	 * Call this after a delay (e.g., 0.3s) to ensure objectives have spawned.
	 */
	UFUNCTION(BlueprintCallable, Category = "Team Manager")
	void DiscoverWorldObjectives();

	void PushContextToFollower(AObjectiveActor* Friendly, AObjectiveActor* Hostile);

	/**
	 * Get the friendly objective (for Defend strategy).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Team Manager")
	AObjectiveActor* GetFriendlyObjective() const { return FriendlyObjective; }

	/**
	 * Get the hostile objective (for Assault/Support strategies).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Team Manager")
	AObjectiveActor* GetHostileObjective() const { return HostileObjective; }

	/**
	 * Check if objectives have been discovered.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Team Manager")
	bool AreObjectivesDiscovered() const { return FriendlyObjective != nullptr && HostileObjective != nullptr; }



	//=====================================================
	// Enemy Tracking
	//=====================================================

	/**
	 * Register an enemy actor for tracking.
	 */
	UFUNCTION(BlueprintCallable, Category = "Team Manager")
	void RegisterEnemy(AActor* Enemy);

	/**
	 * Unregister an enemy actor.
	 */
	UFUNCTION(BlueprintCallable, Category = "Team Manager")
	void UnregisterEnemy(AActor* Enemy);

	/**
	 * Get all known enemies.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Team Manager")
	TArray<AActor*> GetVisibleEnemies() const;

	/**
	 * Clear all known enemies (useful for episode reset).
	 */
	UFUNCTION(BlueprintCallable, Category = "Team Manager")
	void ClearVisibleEnemies();

	/**
	 * Get number of known enemies.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Team Manager")
	int32 GetVisibleEnemyCount() const { return VisibleEnemies.Num(); }



	//=====================================================
	// Observation Building
	//=====================================================

	/**
	 * Build a team observation from provided followers.
	 * @param Followers List of follower actors to include in observation
	 * @return Team observation structure with aggregated intel
	 */
	UFUNCTION(BlueprintCallable, Category = "Team Manager")
	FTeamObservation BuildTeamObservation(const TArray<AActor*>& Followers);

	// ========== Events ==========

	/**
	 * Event broadcast when objectives are discovered.
	 * v9.0: Subscribers can safely initialize observations after this event.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Team Manager")
	FOnObjectivesDiscovered OnObjectivesDiscovered;



public:
	//========== Configuration ==========

	/** Team ID for this leader */
	UPROPERTY(EditAnywhere, Category = "AI|Team", meta = (AllowPrivateAccess = "true"))
	int32 TeamID = 0;

	/**Maximum number of followers allowed */
	UPROPERTY(EditAnywhere, Category = "AI|Team", meta = (AllowPrivateAccess = "true"))
	int32 MaxFollowers = 4;



private:
	/** Registered followers */
	UPROPERTY()
	TArray<AActor*> RegisteredFollowers;

	/** Known enemy actors */
	UPROPERTY()
	TSet<AActor*> VisibleEnemies;

	/** Friendly objective (defend) */
	UPROPERTY()
	TObjectPtr<AObjectiveActor> FriendlyObjective;

	/** Hostile objective (assault) */
	UPROPERTY()
	TObjectPtr<AObjectiveActor> HostileObjective;

	/** Current team observation (cached) */
	UPROPERTY()
	FTeamObservation CurrentTeamObservation;
};
