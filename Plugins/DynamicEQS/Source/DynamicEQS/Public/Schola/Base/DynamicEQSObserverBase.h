// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SensorInterface.h"        // IScholaSensor (ScholaInteractors)
#include "Spaces/BoxSpace.h"        // FBoxSpace (Schola)
#include "Points/BoxPoint.h"        // FBoxPoint (Schola)

#include "DynamicEQSObserverBase.generated.h"

/**
 * UDynamicEQSObserverBase
 * Abstract base for DynamicEQS observation collectors. Implements IScholaSensor so
 * that project subclasses integrate with the Schola 2.0.1 training pipeline.
 *
 * Implement GetObservationSpace() and CollectObservations() in a concrete subclass
 * to describe and fill the observation vector sent to the policy network.
 */
/**
 * UDynamicEQSObserverBase
 * Abstract base for DynamicEQS observation collectors. Implements IScholaSensor so
 * that project subclasses integrate with the Schola 2.0.1 training pipeline.
 *
 * This is a UObject (not a UActorComponent). It is held as an Instanced subobject on
 * UDynamicEQSAgentComponent. Use GetOwnerActor() to reach the owning actor at runtime.
 * Observations are typed as FBoxPoint, using the FPoint hierarchy from Schola/Public/Points.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Dynamic EQS Observer"))
class DYNAMICEQS_API UDynamicEQSObserverBase : public UObject, public IScholaSensor
{
	GENERATED_BODY()

public:
	// -------------------------------------------------------------------------
	// Typed interface (pure-virtual — implement in project subclass)
	// -------------------------------------------------------------------------

	/** Describe the shape and bounds of the observation space. */
	virtual FBoxSpace GetObservationSpace() const
		PURE_VIRTUAL(UDynamicEQSObserverBase::GetObservationSpace, return FBoxSpace(););

	/** Fill OutObservations with the current environment state. */
	virtual void CollectObservations(FBoxPoint& OutObservations)
		PURE_VIRTUAL(UDynamicEQSObserverBase::CollectObservations,);

	// -------------------------------------------------------------------------
	// IScholaSensor bridge (wraps the typed API for the Schola 2.0.1 pipeline)
	// -------------------------------------------------------------------------

	virtual void GetObservationSpace_Implementation(FInstancedStruct& OutObservationSpace) const override
	{
		OutObservationSpace.InitializeAs<FBoxSpace>(GetObservationSpace());
	}

	virtual void CollectObservations_Implementation(FInstancedStruct& OutObservations) override
	{
		FBoxPoint& Point = OutObservations.InitializeAs<FBoxPoint>();
		CollectObservations(Point);
	}

	// -------------------------------------------------------------------------
	// Lifecycle hooks (override in subclasses)
	// -------------------------------------------------------------------------

	/** Called once when the sensor is first set up. */
	virtual void InitializeObserver() {}

	/** Called at the start of each new episode. */
	virtual void ResetObserver() {}

	/** IScholaSensor init — forwards to InitializeObserver(). */
	virtual void InitSensor_Implementation() override { InitializeObserver(); }

	// -------------------------------------------------------------------------
	// Optional override hooks
	// -------------------------------------------------------------------------

	/**
	 * Called after observations have been collected each step.
	 * Useful for debug visualisation hooks in Blueprint subclasses.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DynamicEQS|Observer")
	void OnObservationCollected();
	virtual void OnObservationCollected_Implementation() {}

	// -------------------------------------------------------------------------
	// Owner access
	// -------------------------------------------------------------------------

	/** Returns the actor that owns this observer (set by UDynamicEQSAgentComponent in BeginPlay). */
	AActor* GetOwnerActor() const { return OwnerActor.Get(); }

	/** Called by UDynamicEQSAgentComponent to bind this subobject to its owning actor. */
	void SetOwnerActor(AActor* InActor) { OwnerActor = InActor; }

protected:
	/** Weak reference to the actor that owns the UDynamicEQSAgentComponent holding this observer. */
	UPROPERTY()
	TWeakObjectPtr<AActor> OwnerActor;
};
