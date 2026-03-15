// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Observers/AbstractObservers.h"   // UBoxObserver, FBoxSpace, FBoxPoint

#include "DynamicEQSObserverBase.generated.h"

/**
 * UDynamicEQSObserverBase
 * Abstract base for DynamicEQS observation collectors. Extends UBoxObserver so
 * that project subclasses integrate automatically with the Schola training pipeline.
 *
 * Implement GetObservationSpace() and CollectObservations() in a concrete subclass
 * to describe and fill the observation vector sent to the policy network.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, meta = (DisplayName = "Dynamic EQS Observer"))
class DYNAMICEQS_API UDynamicEQSObserverBase : public UBoxObserver
{
	GENERATED_BODY()

public:
	// -------------------------------------------------------------------------
	// UBoxObserver interface (pure-virtual — implement in project subclass)
	// -------------------------------------------------------------------------

	/** Describe the shape and bounds of the observation space. */
	virtual FBoxSpace GetObservationSpace() const override
		PURE_VIRTUAL(UDynamicEQSObserverBase::GetObservationSpace, return FBoxSpace(););

	/** Fill OutObservations with the current environment state. */
	virtual void CollectObservations(FBoxPoint& OutObservations) override
		PURE_VIRTUAL(UDynamicEQSObserverBase::CollectObservations,);

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
};
