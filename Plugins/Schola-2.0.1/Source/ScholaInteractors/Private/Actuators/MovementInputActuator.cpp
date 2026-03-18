// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Actuators/MovementInputActuator.h"
#include "LogScholaInteractors.h"
#include "GameFramework/Pawn.h"
#include "Points/DictPoint.h"

void UMovementInputActuator::GetActionSpace_Implementation(FInstancedStruct& OutActionSpace) const
{
	// Build a BoxSpace with dimensions for each enabled axis
	TArray<FBoxSpaceDimension> Dimensions;

	if (bHasXDimension)
	{
		Dimensions.Add(FBoxSpaceDimension(MinSpeed, MaxSpeed));
	}

	if (bHasYDimension)
	{
		Dimensions.Add(FBoxSpaceDimension(MinSpeed, MaxSpeed));
	}

	if (bHasZDimension)
	{
		Dimensions.Add(FBoxSpaceDimension(MinSpeed, MaxSpeed));
	}

	FBoxSpace ActionSpace(Dimensions);
	OutActionSpace.InitializeAs<FBoxSpace>(ActionSpace);
}

void UMovementInputActuator::TakeAction_Implementation(const FInstancedStruct& InAction)
{
	// Try to get BoxPoint directly
	if (const FBoxPoint* BoxAction = InAction.GetPtr<FBoxPoint>())
	{
		TakeAction(*BoxAction);
		return;
	}

	// Unknown point type
	const UScriptStruct* ReceivedStruct = InAction.GetScriptStruct();
	const FString TypeName = ReceivedStruct ? ReceivedStruct->GetName() : TEXT("null");
	UE_LOG(LogScholaInteractors, Warning, TEXT("MovementInputActuator %s: Received action is not a BoxPoint or DictPoint. Type: %s"), 
		*GenerateId(), *TypeName);
}

void UMovementInputActuator::TakeAction(const FBoxPoint& Action)
{
	// Convert the action to a movement vector
	FVector MovementInput = ConvertActionToFVector(Action);

	// Broadcast the delegate for debugging/logging
	OnMovementDelegate.Broadcast(MovementInput);

	// Get the owning pawn and apply movement
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn)
	{
		// Apply movement along each enabled axis
		if (bHasXDimension && MovementInput.X != 0.0f)
		{
			OwnerPawn->AddMovementInput(OwnerPawn->GetActorForwardVector(), MovementInput.X * ScaleValue, bForce);
		}

		if (bHasYDimension && MovementInput.Y != 0.0f)
		{
			OwnerPawn->AddMovementInput(OwnerPawn->GetActorRightVector(), MovementInput.Y * ScaleValue, bForce);
		}

		if (bHasZDimension && MovementInput.Z != 0.0f)
		{
			OwnerPawn->AddMovementInput(OwnerPawn->GetActorUpVector(), MovementInput.Z * ScaleValue, bForce);
		}
	}
	else
	{
		UE_LOG(LogScholaInteractors, Warning, TEXT("MovementInputActuator %s: Owner is not a Pawn, cannot apply movement input"), *GenerateId());
	}
}

FVector UMovementInputActuator::ConvertActionToFVector(const FBoxPoint& Action) const
{
	FVector Result = FVector::ZeroVector;
	int32 Index = 0;

	// Extract values from the BoxPoint based on enabled dimensions
	if (bHasXDimension && Index < Action.Values.Num())
	{
		Result.X = Action.Values[Index++];
	}

	if (bHasYDimension && Index < Action.Values.Num())
	{
		Result.Y = Action.Values[Index++];
	}

	if (bHasZDimension && Index < Action.Values.Num())
	{
		Result.Z = Action.Values[Index++];
	}

	return Result;
}

FString UMovementInputActuator::GenerateId() const
{
	return FString::Printf(TEXT("MovementInput_X_%s_Y_%s_Z_%s_Min_%.2f_Max_%.2f"),
		bHasXDimension ? TEXT("true") : TEXT("false"),
		bHasYDimension ? TEXT("true") : TEXT("false"),
		bHasZDimension ? TEXT("true") : TEXT("false"),
		MinSpeed,
		MaxSpeed);
}
