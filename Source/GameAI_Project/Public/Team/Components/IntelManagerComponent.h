// IntelManagerComponent.h
// Phase 3: Intel tracking and observation (extracted from TeamLeaderComponent)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Observation/TeamObservation.h"
#include "IntelManagerComponent.generated.h"

// Forward declarations
class AObjectiveActor;

/**
 * Manages enemy tracking, objective discovery, and team observation building.
 * Handles all intelligence gathering for the team leader.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UIntelManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIntelManagerComponent();

	// ========== Initialization ==========

	virtual void BeginPlay() override;

	/**
	 * Discover objectives in the world by searching for actors with specific tags.
	 * Call this after a delay (e.g., 0.3s) to ensure objectives have spawned.
	 */
	UFUNCTION(BlueprintCallable, Category = "Intel Manager")
	void DiscoverWorldObjectives();

	// ========== Enemy Tracking ==========

	/**
	 * Register an enemy actor for tracking.
	 */
	UFUNCTION(BlueprintCallable, Category = "Intel Manager")
	void RegisterEnemy(AActor* Enemy);

	/**
	 * Unregister an enemy actor.
	 */
	UFUNCTION(BlueprintCallable, Category = "Intel Manager")
	void UnregisterEnemy(AActor* Enemy);

	/**
	 * Get all known enemies.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Intel Manager")
	TArray<AActor*> GetKnownEnemies() const;

	/**
	 * Clear all known enemies (useful for episode reset).
	 */
	UFUNCTION(BlueprintCallable, Category = "Intel Manager")
	void ClearKnownEnemies();

	/**
	 * Get number of known enemies.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Intel Manager")
	int32 GetKnownEnemyCount() const { return KnownEnemies.Num(); }

	// ========== Objective Queries ==========

	/**
	 * Get the friendly objective (for Defend strategy).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Intel Manager")
	AObjectiveActor* GetFriendlyObjective() const { return FriendlyObjective; }

	/**
	 * Get the hostile objective (for Assault/Support strategies).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Intel Manager")
	AObjectiveActor* GetHostileObjective() const { return HostileObjective; }

	/**
	 * Check if objectives have been discovered.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Intel Manager")
	bool AreObjectivesDiscovered() const { return FriendlyObjective != nullptr && HostileObjective != nullptr; }

	// ========== Observation Building ==========

	/**
	 * Build a team observation from provided followers.
	 * @param Followers List of follower actors to include in observation
	 * @return Team observation structure with aggregated intel
	 */
	UFUNCTION(BlueprintCallable, Category = "Intel Manager")
	FTeamObservation BuildTeamObservation(const TArray<AActor*>& Followers);

	// ========== Configuration ==========

	/** Team ID for this intel manager (used to identify friendly/hostile objectives) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intel Manager")
	int32 TeamID = 0;


private:
	/** Known enemy actors */
	UPROPERTY()
	TSet<AActor*> KnownEnemies;

	/** Friendly objective (defend) */
	UPROPERTY()
	AObjectiveActor* FriendlyObjective = nullptr;

	/** Hostile objective (assault) */
	UPROPERTY()
	AObjectiveActor* HostileObjective = nullptr;

	/** Current team observation (cached) */
	UPROPERTY()
	FTeamObservation CurrentTeamObservation;
};
