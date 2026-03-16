// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ActuatorInterface.h"      // IScholaActuator (ScholaInteractors)
#include "Spaces/BoxSpace.h"        // FBoxSpace (Schola)
#include "Points/BoxPoint.h"        // FBoxPoint (Schola)

#include "DynamicEQSActuatorBase.generated.h"

// Forward declarations
class UDynamicEQSExecutor;

/**
 * UDynamicEQSActuatorBase
 * Abstract base for DynamicEQS action applicators. Implements IScholaActuator so
 * that project subclasses integrate with the Schola 2.0.1 training pipeline.
 *
 * This is a UObject (not a UActorComponent). It is held as an Instanced subobject on
 * UDynamicEQSAgentComponent. Use GetOwnerActor() to reach the owning actor at runtime.
 *
 * Implement GetActionSpace() and TakeAction() in a concrete subclass to translate
 * the policy output into EQS weight updates.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Dynamic EQS Actuator"))
class DYNAMICEQS_API UDynamicEQSActuatorBase : public UObject, public IScholaActuator
{
	GENERATED_BODY()

public:
	// -------------------------------------------------------------------------
	// Typed interface (pure-virtual — implement in project subclass)
	// -------------------------------------------------------------------------

	/** Describe the shape of the action space expected by this actuator. */
	virtual FBoxSpace GetActionSpace() const
		PURE_VIRTUAL(UDynamicEQSActuatorBase::GetActionSpace, return FBoxSpace(););

	/**
	 * Translate policy output (FBoxPoint) into EQS weights and apply them.
	 * Retrieve the EQS Executor via GetOwner()->FindComponentByClass<UDynamicEQSExecutor>()
	 * or cache it in InitializeActuator().
	 */
	virtual void TakeAction(const FBoxPoint& Action)
		PURE_VIRTUAL(UDynamicEQSActuatorBase::TakeAction,);

	// -------------------------------------------------------------------------
	// IScholaActuator bridge (wraps the typed API for the Schola 2.0.1 pipeline)
	// -------------------------------------------------------------------------

	virtual void GetActionSpace_Implementation(FInstancedStruct& OutActionSpace) const override
	{
		OutActionSpace.InitializeAs<FBoxSpace>(GetActionSpace());
	}

	virtual void TakeAction_Implementation(const FInstancedStruct& InAction) override
	{
		if (const FBoxPoint* Point = InAction.GetPtr<FBoxPoint>())
		{
			TakeAction(*Point);
		}
	}

	// -------------------------------------------------------------------------
	// Lifecycle hooks (override in subclasses)
	// -------------------------------------------------------------------------

	/** Called once when the actuator is first set up. */
	virtual void InitializeActuator() {}

	/** Called at the start of each new episode. */
	virtual void ResetActuator() {}

	/** IScholaActuator init — forwards to InitializeActuator(). */
	virtual void InitActuator_Implementation() override { InitializeActuator(); }

	// -------------------------------------------------------------------------
	// Owner access
	// -------------------------------------------------------------------------

	/** Returns the actor that owns this actuator (set by UDynamicEQSAgentComponent in BeginPlay). */
	AActor* GetOwnerActor() const { return OwnerActor.Get(); }

	/** Called by UDynamicEQSAgentComponent to bind this subobject to its owning actor. */
	void SetOwnerActor(AActor* InActor) { OwnerActor = InActor; }

protected:
	/** Weak reference to the actor that owns the UDynamicEQSAgentComponent holding this actuator. */
	UPROPERTY()
	TWeakObjectPtr<AActor> OwnerActor;
};
