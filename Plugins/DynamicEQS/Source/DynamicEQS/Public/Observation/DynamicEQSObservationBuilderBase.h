// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "DynamicEQSObservationBuilderBase.generated.h"

/**
 * UDynamicEQSObservationBuilderBase
 * Abstract helper for incrementally assembling an observation vector.
 * Useful when the full observation is composed of several sub-components
 * that each need their own builder (e.g., position, health, objective state).
 * Subclass and implement BuildObservation(), GetObservationDimension(),
 * and ValidateObservation().
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Dynamic EQS Observation Builder"))
class DYNAMICEQS_API UDynamicEQSObservationBuilderBase : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Append this builder's observations to OutObservation.
	 * Implementations should Add() exactly GetObservationDimension() elements.
	 */
	virtual void BuildObservation(TArray<float>& OutObservation)
		PURE_VIRTUAL(UDynamicEQSObservationBuilderBase::BuildObservation,);

	/**
	 * Return the number of floats this builder contributes.
	 * Must be constant for a given builder instance.
	 */
	virtual int32 GetObservationDimension() const
		PURE_VIRTUAL(UDynamicEQSObservationBuilderBase::GetObservationDimension, return 0;);

	/**
	 * Sanity-check a completed observation vector.
	 * Return true if the observation is valid (correct size, in-range values, etc.).
	 */
	virtual bool ValidateObservation(const TArray<float>& Observation) const
		PURE_VIRTUAL(UDynamicEQSObservationBuilderBase::ValidateObservation, return false;);
};
