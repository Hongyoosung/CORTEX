// TeamCommsComponent.h
// Phase 3: Team communication (extracted from FollowerAgentComponent)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Team/TeamTypes.h"
#include "TeamCommsComponent.generated.h"

// Forward declarations
class UTeamLeaderComponent;

/**
 * Delegate fired when registration status changes.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCommsRegistrationChanged, bool, bIsRegistered);

/**
 * Delegate fired when an event is signaled to the leader.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnCommsEventSignaled,
	EStrategicEvent, Event,
	AActor*, Instigator,
	int32, Priority
);

/**
 * Team Communications Component
 *
 * Handles communication between followers and team leader.
 * Manages registration, unregistration, and event reporting.
 *
 * Responsibilities:
 * - Register/unregister with team leader
 * - Signal strategic events to leader
 * - Maintain team leader reference
 * - Handle auto-registration on BeginPlay
 *
 * Usage:
 * 1. Attach to follower actor
 * 2. Set TeamLeaderActor or TeamLeaderTag
 * 3. Component auto-registers on BeginPlay (if enabled)
 * 4. Use SignalEventToLeader() to report events
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UTeamCommsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTeamCommsComponent();

	// ========== Lifecycle ==========

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ========== Registration ==========

	/**
	 * Register this follower with the team leader.
	 * @return True if registration succeeded, false otherwise
	 */
	UFUNCTION(BlueprintCallable, Category = "Team Comms")
	bool RegisterWithTeamLeader();

	/**
	 * Unregister from the team leader.
	 */
	UFUNCTION(BlueprintCallable, Category = "Team Comms")
	void UnregisterFromTeamLeader();

	/**
	 * Check if currently registered with a team leader.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Team Comms")
	bool IsRegisteredWithLeader() const { return bIsRegistered; }

	// ========== Event Reporting ==========

	/**
	 * Signal a strategic event to the team leader.
	 * @param Event - Type of strategic event
	 * @param Instigator - Actor that caused the event (optional)
	 * @param Location - World location of the event
	 * @param Priority - Event priority (0-10, higher = more urgent)
	 */
	UFUNCTION(BlueprintCallable, Category = "Team Comms")
	void SignalEventToLeader(
		EStrategicEvent Event,
		AActor* Instigator = nullptr,
		FVector Location = FVector::ZeroVector,
		int32 Priority = 5
	);

	// ========== Queries ==========

	/**
	 * Get the team leader component reference.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Team Comms")
	UTeamLeaderComponent* GetTeamLeader() const { return TeamLeader; }

	/**
	 * Get the team leader actor reference.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Team Comms")
	AActor* GetTeamLeaderActor() const { return TeamLeaderActor; }

	/**
	 * Get team ID from the leader.
	 * @return Team ID, or -1 if not registered
	 */
	UFUNCTION(BlueprintCallable, Category = "Team Comms")
	int32 GetTeamID() const;

	// ========== Configuration ==========

	/** Team ID for this follower (used to find matching leader) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Comms")
	int32 TeamID = 0;

	/** Automatically register with team leader on BeginPlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Comms")
	bool bAutoRegisterWithLeader = true;

	/** Enable verbose logging for debugging */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Team Comms")
	bool bEnableVerboseLogging = false;

	// ========== Events ==========

	/** Fired when registration status changes */
	UPROPERTY(BlueprintAssignable, Category = "Team Comms")
	FOnCommsRegistrationChanged OnRegistrationChanged;

	/** Fired when an event is signaled to the leader */
	UPROPERTY(BlueprintAssignable, Category = "Team Comms")
	FOnCommsEventSignaled OnEventSignaled;

private:
	/** Team leader component reference (resolved by TeamID) */
	UPROPERTY()
	UTeamLeaderComponent* TeamLeader = nullptr;

	/** Team leader actor reference (cached) */
	UPROPERTY()
	AActor* TeamLeaderActor = nullptr;

	/** Is currently registered with team leader? */
	bool bIsRegistered = false;

	/**
	 * Find team leader by matching TeamID.
	 * @return Team leader actor, or nullptr if not found
	 */
	AActor* FindTeamLeaderByTeamID();

	/**
	 * Resolve TeamLeaderComponent by TeamID matching.
	 * @return True if resolution succeeded
	 */
	bool ResolveTeamLeaderComponent();
};
