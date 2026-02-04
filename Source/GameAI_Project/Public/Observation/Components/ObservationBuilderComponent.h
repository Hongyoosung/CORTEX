#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Observation/ObservationElement.h"
#include "ObservationBuilderComponent.generated.h"

// Forward declarations
class UAgentPerceptionComponent;
class UHealthComponent;
class AObjectiveActor;

/**
 * Observation Builder Component
 *
 * Responsibilities:
 * - Build local observations (71 features)
 * - Cover detection and caching
 * - Raycast calculations
 * - Ally context building
 *
 * This component was extracted from FollowerAgentComponent to improve separation of concerns.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UObservationBuilderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UObservationBuilderComponent();

	virtual void BeginPlay() override;

	//--------------------------------------------------------------------------
	// OBSERVATION BUILDING
	//--------------------------------------------------------------------------

	/** Build observation from current actor state */
	UFUNCTION(BlueprintCallable, Category = "Observation|Building")
	FObservationElement BuildLocalObservation();

	/** Update local observation */
	UFUNCTION(BlueprintCallable, Category = "Observation|Building")
	void UpdateLocalObservation(const FObservationElement& NewObservation);

	/** Get local observation */
	UFUNCTION(BlueprintPure, Category = "Observation|Building")
	FObservationElement GetLocalObservation() const { return LocalObservation; }

	/** Get previous observation (for RL experience collection) */
	UFUNCTION(BlueprintPure, Category = "Observation|Building")
	FObservationElement GetPreviousObservation() const { return PreviousObservation; }

	//--------------------------------------------------------------------------
	// COVER DETECTION
	//--------------------------------------------------------------------------

	/** Find nearest cover position relative to enemies */
	UFUNCTION(BlueprintCallable, Category = "Observation|Cover")
	bool FindNearestCover(FVector& OutCoverLocation, float& OutDistance, const TArray<AActor*>& Enemies);

	/** Get cached cover location */
	UFUNCTION(BlueprintPure, Category = "Observation|Cover")
	FVector GetCachedCoverLocation() const { return CachedCoverLocation; }

	/** Get cached cover distance */
	UFUNCTION(BlueprintPure, Category = "Observation|Cover")
	float GetCachedCoverDistance() const { return CachedCoverDistance; }

	/** Has cached cover? */
	UFUNCTION(BlueprintPure, Category = "Observation|Cover")
	bool HasCachedCover() const { return bHasCachedCover; }

	/** Reset episode (clear observations) */
	UFUNCTION(BlueprintCallable, Category = "Observation|Building")
	void ResetEpisode();

	//--------------------------------------------------------------------------
	// OBJECTIVE CONTEXT (v9.0)
	//--------------------------------------------------------------------------

	/** Populate objective context fields in observation (friendly and hostile objectives) */
	void PopulateObjectiveContext(FObservationElement& Observation);

	void SetObjectives(AObjectiveActor* Friendly, AObjectiveActor* Hostile);

	FORCEINLINE void EnemySpotted(int32 EnemyCount = 1) { LocalObservation.VisibleEnemyCount += EnemyCount; }

	//--------------------------------------------------------------------------
	// v9.0 PHASE 3: DEPENDENCY INJECTION
	//--------------------------------------------------------------------------
	/** Set health component (injected by character) */
	void SetHealthComponent(UHealthComponent* Health);

	/** Set perception component (injected by character) */
	void SetPerceptionComponent(UAgentPerceptionComponent* Perception);



	//=========================================================================
	// OBSERVATION GETTER
	//=========================================================================
	FORCEINLINE AObjectiveActor* GetFriendlyObjective	()	const {		return CachedFriendlyObjective;		}
	FORCEINLINE AObjectiveActor* GetHostileObjective	()	const {		return CachedHostileObjective;		}



public:
	//--------------------------------------------------------------------------
	// COVER DETECTION CONFIG
	//--------------------------------------------------------------------------

	/** Cover search radius (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Cover")
	float CoverSearchRadius = 1000.0f;

	/** Cover search angular samples (rays around agent) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Cover")
	int32 CoverSearchSamples = 8;

	/** Minimum obstacle size to qualify as cover (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Cover")
	float MinCoverHeight = 100.0f;

	/** Cover query update interval (seconds) - cache results to avoid per-tick raycasts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Cover")
	float CoverQueryInterval = 0.5f;

	//--------------------------------------------------------------------------
	// STATE
	//--------------------------------------------------------------------------

	/** Local observation (56 features) */
	UPROPERTY(BlueprintReadOnly, Category = "Observation|State")
	FObservationElement LocalObservation;

	/** Previous observation (for experience collection) */
	UPROPERTY(BlueprintReadOnly, Category = "Observation|State")
	FObservationElement PreviousObservation;

private:
	//--------------------------------------------------------------------------
	// COVER CACHE
	//--------------------------------------------------------------------------

	/** Cached cover location */
	FVector CachedCoverLocation = FVector::ZeroVector;

	/** Cached cover distance */
	float CachedCoverDistance = 9999.0f;

	/** Is cached cover valid? */
	bool bHasCachedCover = false;

	/** Last cover query time */
	float LastCoverQueryTime = 0.0f;

	//--------------------------------------------------------------------------
	// CACHED COMPONENTS
	//--------------------------------------------------------------------------

	/** Cached health component */
	UPROPERTY()
	UHealthComponent* CachedHealthComponent = nullptr;

	/** Cached perception component */
	UPROPERTY()
	UAgentPerceptionComponent* CachedPerceptionComponent = nullptr;

	UPROPERTY()
	AObjectiveActor* CachedFriendlyObjective = nullptr;

	UPROPERTY()
	AObjectiveActor* CachedHostileObjective = nullptr;
};
