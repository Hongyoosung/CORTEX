#pragma once

#include "CoreMinimal.h"
#include "Perception/AIPerceptionComponent.h"
#include "Observation/ObservationTypes.h"
#include "AgentPerceptionComponent.generated.h"


class AFollowerCharacter;
class UAISenseConfig_Sight;


/**
 * Perception result for a single detected actor
 */
USTRUCT(BlueprintType)
struct FPerceptionResult
{
	GENERATED_BODY()

	/** Detected actor */
	UPROPERTY(BlueprintReadWrite)
	AActor* DetectedActor = nullptr;

	/** Is this actor an enemy? */
	UPROPERTY(BlueprintReadWrite)
	bool bIsEnemy = false;

	/** Distance to detected actor */
	UPROPERTY(BlueprintReadWrite)
	float Distance = 0.0f;

	/** Relative angle to detected actor */
	UPROPERTY(BlueprintReadWrite)
	float RelativeAngle = 0.0f;

	/** Successfully sensed (visible/audible) */
	UPROPERTY(BlueprintReadWrite)
	bool bSuccessfullySensed = false;

	/** Last sense time */
	UPROPERTY(BlueprintReadWrite)
	float LastSenseTime = 0.0f;
};

/**
 * Agent Perception Component
 *
 * Handles enemy detection and observation updates for AI agents.
 * Uses UE5's AI Perception System to detect actors, then filters
 * for enemies using SimulationManagerGameMode's team system.
 *
 * Features:
 * - Sight-based enemy detection
 * - Team-based enemy filtering
 * - Automatic observation updates
 * - Enemy tracking (up to 5 nearest)
 * - Integration with RL observation system
 *
 * Usage:
 * 1. Attach to AI agent actor (alongside FollowerAgentComponent)
 * 2. Configure sight radius and angle
 * 3. Component auto-detects enemies and updates observations
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UAgentPerceptionComponent : public UAIPerceptionComponent
{
	GENERATED_BODY()

public:
	UAgentPerceptionComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	//============================================================
	// PERCEPTION QUERIES
	//============================================================

	/**
	 * Get nearest N enemies
	 * @param MaxCount - Maximum number of enemies to return
	 * @return Array of nearest enemy actors
	 */
	UFUNCTION(BlueprintCallable, Category = "Perception|Enemies")
	TArray<AActor*> GetNearestEnemies(int32 MaxCount = 5) const;

	/**
	 * Get enemy observation data (for RL observations)
	 * @param MaxCount - Maximum number of enemies to track
	 * @return Array of enemy observations
	 */
	UFUNCTION(BlueprintCallable, Category = "Perception|Enemies")
	TArray<FEnemyObservation> GetEnemyObservations(int32 MaxCount = 5) const;


	/**
	 * Get number of visible enemies
	 */
	UFUNCTION(BlueprintPure, Category = "Perception|Enemies")
	int32 GetVisibleEnemyCount() const;

	/**
	 * Get all perception results (enemies and non-enemies)
	 */
	UFUNCTION(BlueprintCallable, Category = "Perception|Debug")
	TArray<FPerceptionResult> GetAllPerceptionResults() const;

	//============================================================
	// OBSERVATION INTEGRATION
	//============================================================

	/**
	 * Update observation element with enemy data
	 * @param OutObservation - Observation to update
	 */
	UFUNCTION(BlueprintCallable, Category = "Perception|Observation")
	void UpdateObservationWithEnemies(FObservationElement& OutObservation);

	/**
	 * Build raycast hit types based on perceived actors
	 * @param NumRays - Number of rays
	 * @param RayLength - Length of each ray
	 * @return Array of raycast hit types
	 */
	UFUNCTION(BlueprintCallable, Category = "Perception|Observation")
	TArray<ERaycastHitType> BuildRaycastHitTypes(int32 NumRays = 16, float RayLength = 5000.0f);

	//============================================================
	// TEAM LEADER INTEGRATION
	//============================================================
	/**
	 * Signal enemy spotted event to team leader
	 * @param Enemy - Detected enemy actor
	 */
	UFUNCTION(BlueprintCallable, Category = "Perception|Team")
	void SignalEnemySpotted(AActor* Enemy);



private:
	/** Perception update callback */
	UFUNCTION()
	void OnPerceptionUpdatedCallback(const TArray<AActor*>& UpdatedActors);

	/** Target perceived callback */
	UFUNCTION()
	void OnTargetPerceivedCallback(AActor* Actor, FAIStimulus Stimulus);

	/** Initialize perception senses */
	void InitializePerception();

	/** Update tracked enemies */
	void UpdateTrackedEnemies();

	/** Calculate relative angle to target */
	float GetRelativeAngleToTarget(AActor* Target) const;



public:
	//============================================================
	// CONFIGURATION
	//============================================================

	/** Sight radius (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Config")
	float SightRadius = 5000.0f;

	/** Lose sight radius (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Config")
	float LoseSightRadius = 5500.0f;

	/** Peripheral vision angle (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Config")
	float PeripheralVisionAngle = 90.0f;

	/** Auto-report enemies to team leader when detected */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Config")
	bool bAutoReportToLeader = true;

	/** Auto-update follower observation on tick */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Config")
	bool bAutoUpdateObservation = true;

	/** Update observation interval (seconds, 0 = every tick) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Config")
	float ObservationUpdateInterval = 0.1f;

	/** Maximum enemies to track in observations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Config")
	int32 MaxTrackedEnemies = 5;

	/** Enable debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Debug")
	bool bDrawDebugInfo = false;


public:
	//=============  EVENTS  =================

	/** Event fired when new enemy is detected for the first time */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemySpottedSignature, AActor*, Enemy);

	UPROPERTY(BlueprintAssignable, Category = "Perception|Events")
	FOnEnemySpottedSignature OnEnemySpotted;

	/** Event fired when all enemies are lost from sight */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAllEnemiesLostSignature);

	UPROPERTY(BlueprintAssignable, Category = "Perception|Events")
	FOnAllEnemiesLostSignature OnAllEnemiesLost;


private:

	/** Cached follower component */
	UPROPERTY()
	AFollowerCharacter* FollowerAgent = nullptr;

	/** Tracked enemies (sorted by distance) */
	UPROPERTY()
	TArray<AActor*> TrackedEnemies;

	/** Last observation update time */
	float LastObservationUpdateTime = 0.0f;

	/** Previously reported enemies (to avoid spam) */
	UPROPERTY()
	TSet<AActor*> ReportedEnemies;
};
