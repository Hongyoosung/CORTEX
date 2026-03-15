// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actuators/AbstractActuators.h"  // UBoxActuator, FBoxSpace, FBoxPoint

#include "DynamicEQSActuatorBase.generated.h"

// Forward declarations
class UDynamicEQSExecutor;

/**
 * UDynamicEQSActuatorBase
 * Abstract base for DynamicEQS action applicators. Extends UBoxActuator so that
 * project subclasses integrate automatically with the Schola training pipeline.
 *
 * Implement GetActionSpace() and TakeAction() in a concrete subclass to translate
 * the policy output into EQS weight updates.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, meta = (DisplayName = "Dynamic EQS Actuator"))
class DYNAMICEQS_API UDynamicEQSActuatorBase : public UBoxActuator
{
	GENERATED_BODY()

public:
	// -------------------------------------------------------------------------
	// UBoxActuator interface (pure-virtual — implement in project subclass)
	// -------------------------------------------------------------------------

	/** Describe the shape of the action space expected by this actuator. */
	virtual FBoxSpace GetActionSpace() override
		PURE_VIRTUAL(UDynamicEQSActuatorBase::GetActionSpace, return FBoxSpace(););

	/**
	 * Translate policy output (FBoxPoint) into EQS weights and apply them.
	 * Retrieve the EQS Executor via GetOwner()->FindComponentByClass<UDynamicEQSExecutor>()
	 * or cache it in InitializeActuator().
	 */
	virtual void TakeAction(const FBoxPoint& Action) override
		PURE_VIRTUAL(UDynamicEQSActuatorBase::TakeAction,);
};
