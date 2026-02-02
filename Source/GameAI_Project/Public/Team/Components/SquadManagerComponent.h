// SquadManagerComponent.h
// Phase 3: Follower roster management (extracted from TeamLeaderComponent)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SquadManagerComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSquadFollowerRegistered, AActor*, Follower);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSquadFollowerUnregistered, AActor*, Follower);

/**
 * Manages follower roster for a team leader.
 * Handles registration, unregistration, and roster queries.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API USquadManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USquadManagerComponent();

	// ========== Configuration ==========

	/** Maximum number of followers allowed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad Manager")
	int32 MaxFollowers = 4;

	// ========== Registration ==========

	/**
	 * Register a follower with this squad.
	 * @param Follower The actor to register
	 * @return True if registration succeeded, false if squad is full or follower is invalid
	 */
	UFUNCTION(BlueprintCallable, Category = "Squad Manager")
	bool RegisterFollower(AActor* Follower);

	/**
	 * Unregister a follower from this squad.
	 * @param Follower The actor to unregister
	 * @return True if unregistration succeeded
	 */
	UFUNCTION(BlueprintCallable, Category = "Squad Manager")
	bool UnregisterFollower(AActor* Follower);

	/**
	 * Queue a follower for registration (deferred until next tick).
	 * Used when registration happens during component initialization.
	 */
	UFUNCTION(BlueprintCallable, Category = "Squad Manager")
	void QueueFollowerRegistration(AActor* Follower);

	// ========== Queries ==========

	/**
	 * Get all registered followers.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Squad Manager")
	TArray<AActor*> GetFollowers() const { return Followers; }

	/**
	 * Get only alive followers (Health > 0).
	 */
	UFUNCTION(BlueprintCallable, Category = "Squad Manager")
	TArray<AActor*> GetAliveFollowers() const;

	/**
	 * Get current follower count.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Squad Manager")
	int32 GetFollowerCount() const { return Followers.Num(); }

	/**
	 * Check if squad is at max capacity.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Squad Manager")
	bool IsSquadFull() const { return Followers.Num() >= MaxFollowers; }

	/**
	 * Check if an actor is registered as a follower.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Squad Manager")
	bool IsFollowerRegistered(AActor* Follower) const { return Followers.Contains(Follower); }

	// ========== Events ==========

	/** Fired when a follower is successfully registered */
	UPROPERTY(BlueprintAssignable, Category = "Squad Manager")
	FOnSquadFollowerRegistered OnFollowerRegistered;

	/** Fired when a follower is unregistered */
	UPROPERTY(BlueprintAssignable, Category = "Squad Manager")
	FOnSquadFollowerUnregistered OnFollowerUnregistered;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Active followers */
	UPROPERTY()
	TArray<AActor*> Followers;

	/** Pending followers (registered during BeginPlay, processed on first tick) */
	UPROPERTY()
	TArray<AActor*> PendingFollowerRegistration;

	/** Process pending registrations */
	void ProcessPendingRegistrations();
};
