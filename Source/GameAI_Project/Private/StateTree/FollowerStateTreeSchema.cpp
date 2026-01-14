// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/FollowerStateTreeSchema.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTreeExecutionContext.h"
#include "Components/StateTreeComponent.h"
#include "AI/AIController/FollowerAIController.h"
#include "Actor/FollowerCharacter.h"
#include "Team/FollowerAgentComponent.h"
#include "Team/TeamLeaderComponent.h"
#include "RL/RLPolicyNetwork.h"
#include "StateTree/Conditions/STCondition_IsAlive.h"
#include "StateTree/Tasks/STTask_ExecuteTacticalMovement_v8.h"
#include "StateTree/Tasks/STTask_ExecuteAiming.h"
#include "StateTree/Tasks/STTask_ExecuteFire.h"
#include "StateTree/Tasks/STTask_Dead.h"
#include "StateTree/Tasks/STTask_Idle.h"
#include "StateTree/Evaluators/STEvaluator_UpdateObservation.h"


UFollowerStateTreeSchema::UFollowerStateTreeSchema()
{
	AIControllerClass = AFollowerAIController::StaticClass();
	PawnClass = AFollowerCharacter::StaticClass();

	ContextDataDescs.Reset();

	// 1. BASE CONTEXT (Required by parent schema and StateTree framework)

	// Pawn (REQUIRED for all StateTree operations)
	{
		FStateTreeExternalDataDesc PawnDesc(
			FName("Pawn"),
			APawn::StaticClass(),
			FGuid(0x2E11DB00, 0xC4084FDB, 0xB164E824, 0x347C7BB6)
		);
		PawnDesc.Requirement = EStateTreeExternalDataRequirement::Required;
		ContextDataDescs.Add(PawnDesc);
	}

	// AIController (OPTIONAL - Schola training uses FollowerAgentTrainer which may replace AIController)
	{
		FStateTreeExternalDataDesc AIDesc(
			FName("AIController"),
			AAIController::StaticClass(),
			FGuid(0x1D291B00, 0x29994FDE, 0xC6546702, 0x47895FD6)
		);
		AIDesc.Requirement = EStateTreeExternalDataRequirement::Optional;
		ContextDataDescs.Add(AIDesc);
	}

	// Actor (base context)
	{
		FStateTreeExternalDataDesc ActorDesc(
			FName("Actor"),
			AActor::StaticClass(),
			FGuid(0x1D971B00, 0x28884FDE, 0xB5436802, 0x36984FD5)
		);
		ActorDesc.Requirement = EStateTreeExternalDataRequirement::Required;
		ContextDataDescs.Add(ActorDesc);
	}

	// 2. CUSTOM FOLLOWER CONTEXT


	// 1. FollowerContext
	{
		FStateTreeExternalDataDesc Desc(FName(TEXT("FollowerContext")), FFollowerStateTreeContext::StaticStruct(), FGuid(0x4F111111, 0x11112222, 0x33334444, 0x00000001));
		Desc.Requirement = EStateTreeExternalDataRequirement::Required;
		ContextDataDescs.Add(Desc);
	}

	// 2. FollowerComponent
	{
		FStateTreeExternalDataDesc Desc(FName(TEXT("FollowerComponent")), UFollowerAgentComponent::StaticClass(), FGuid(0x4F111111, 0x11112222, 0x33334444, 0x00000002));
		Desc.Requirement = EStateTreeExternalDataRequirement::Required;
		ContextDataDescs.Add(Desc);
	}

	// 3. FollowerStateTreeComponent
	{
		FStateTreeExternalDataDesc Desc(FName(TEXT("FollowerStateTreeComponent")), UFollowerStateTreeComponent::StaticClass(), FGuid(0x4F111111, 0x11112222, 0x33334444, 0x00000003));
		Desc.Requirement = EStateTreeExternalDataRequirement::Required;
		ContextDataDescs.Add(Desc);
	}

	// 4. TeamLeader
	{
		FStateTreeExternalDataDesc Desc(FName(TEXT("TeamLeader")), UTeamLeaderComponent::StaticClass(), FGuid(0x4F111111, 0x11112222, 0x33334444, 0x00000004));
		Desc.Requirement = EStateTreeExternalDataRequirement::Optional;
		ContextDataDescs.Add(Desc);
	}

	// 5. TacticalPolicy
	{
		FStateTreeExternalDataDesc Desc(FName(TEXT("TacticalPolicy")), URLPolicyNetwork::StaticClass(), FGuid(0x4F111111, 0x11112222, 0x33334444, 0x00000005));
		Desc.Requirement = EStateTreeExternalDataRequirement::Optional;
		ContextDataDescs.Add(Desc);
	}
}

bool UFollowerStateTreeSchema::SetContextRequirements(UStateTreeComponent& InComponent, FStateTreeExecutionContext& Context, bool bLogErrors)
{

	// Call parent implementation first to set up base framework
	if (!Super::SetContextRequirements(InComponent, Context, bLogErrors))
	{
		if (bLogErrors)
		{
			//UE_LOG(LogTemp, Warning, TEXT("FollowerStateTreeSchema: Parent SetContextRequirements failed (may be expected for custom schema)"));
		}
	}

	// Get owner actor and pawn
	AActor* Owner = InComponent.GetOwner();
	if (!Owner)
	{
		if (bLogErrors)
		{
			UE_LOG(LogTemp, Error, TEXT("FollowerStateTreeSchema: Owner is null"));
		}
		return false;
	}

	APawn* OwnerPawn = Cast<APawn>(Owner);
	if (!OwnerPawn)
	{
		if (bLogErrors)
		{
			UE_LOG(LogTemp, Error, TEXT("FollowerStateTreeSchema: Owner '%s' is not a Pawn"), *Owner->GetName());
		}
		return false;
	}

	// REQUIRED: Pawn
	if (!Context.SetContextDataByName(TEXT("Pawn"), FStateTreeDataView(OwnerPawn)))
	{
		if (bLogErrors)
		{
			UE_LOG(LogTemp, Error, TEXT("FollowerStateTreeSchema: Failed to set Pawn context for '%s'"), *Owner->GetName());
		}
		return false;
	}

	// OPTIONAL: AIController (may be null during Schola training when FollowerAgentTrainer takes over)
	AAIController* AIController = Cast<AAIController>(OwnerPawn->GetController());
	if (AIController)
	{
		Context.SetContextDataByName(TEXT("AIController"), FStateTreeDataView(AIController));
	}
	else
	{
		// Set null context - StateTree will handle optional data gracefully
		Context.SetContextDataByName(TEXT("AIController"), FStateTreeDataView());
		if (bLogErrors)
		{
			UE_LOG(LogTemp, Log, TEXT("FollowerStateTreeSchema: No AIController for '%s' (Schola training mode)"), *Owner->GetName());
		}
	}

	// REQUIRED: Actor
	if (!Context.SetContextDataByName(TEXT("Actor"), FStateTreeDataView(Owner)))
	{
		if (bLogErrors)
		{
			UE_LOG(LogTemp, Error, TEXT("FollowerStateTreeSchema: Failed to set Actor context for '%s'"), *Owner->GetName());
		}
		return false;
	}

	return true;
}

bool UFollowerStateTreeSchema::IsStructAllowed(const UScriptStruct* InScriptStruct) const
{
	// Allow base State Tree structs
	if (Super::IsStructAllowed(InScriptStruct))
	{
		return true;
	}

	// Allow project-specific structs
	if (InScriptStruct)
	{
		if (InScriptStruct->IsChildOf(FSTEvaluator_UpdateObservation::StaticStruct()) ||
			InScriptStruct->IsChildOf(FSTCondition_IsAlive::StaticStruct()) ||
			InScriptStruct->IsChildOf(FSTTask_Dead::StaticStruct()) ||
			InScriptStruct->IsChildOf(FSTTask_Idle::StaticStruct()) ||
			InScriptStruct->IsChildOf(FSTTask_ExecuteTacticalMovement_v8::StaticStruct()) ||
			InScriptStruct->IsChildOf(FSTTask_ExecuteAiming::StaticStruct()) ||
			InScriptStruct->IsChildOf(FSTTask_ExecuteFire::StaticStruct())
			)
		{
			return true;
		}

		// Allow observation structs
		if (InScriptStruct->GetFName() == FName(TEXT("ObservationElement")))
		{
			return true;
		}

		// Allow RL types
		if (InScriptStruct->GetFName() == FName(TEXT("TacticalAction")) ||
			InScriptStruct->GetFName() == FName(TEXT("StrategicCommand")))
		{
			return true;
		}

		// Allow team types
		if (InScriptStruct->GetFName() == FName(TEXT("TeamID")) ||
			InScriptStruct->GetFName() == FName(TEXT("TeamMessage")))
		{
			return true;
		}

		// Allow context struct
		if (InScriptStruct == FFollowerStateTreeContext::StaticStruct())
		{
			return true;
		}
	}

	return false;
}

bool UFollowerStateTreeSchema::IsClassAllowed(const UClass* InClass) const
{
	// Allow base State Tree classes
	if (Super::IsClassAllowed(InClass))
	{
		return true;
	}

	// Allow AI-related classes
	if (InClass)
	{
		if (InClass->IsChildOf(AAIController::StaticClass()) ||
			InClass->IsChildOf(APawn::StaticClass()) ||
			InClass->IsChildOf(UFollowerAgentComponent::StaticClass()) ||
			InClass->IsChildOf(UTeamLeaderComponent::StaticClass()) ||
			InClass->IsChildOf(URLPolicyNetwork::StaticClass()) ||
			InClass->IsChildOf(UFollowerStateTreeComponent::StaticClass()))
		{
			return true;
		}

		// Allow Actor for target tracking
		if (InClass->IsChildOf(AActor::StaticClass()))
		{
			return true;
		}
	}

	return false;
}

bool UFollowerStateTreeSchema::IsExternalItemAllowed(const UStruct& InStruct) const
{
	// Allow base State Tree external items
	if (Super::IsExternalItemAllowed(InStruct))
	{
		return true;
	}

	// Allow our context struct
	if (&InStruct == FFollowerStateTreeContext::StaticStruct())
	{
		return true;
	}

	return false;
}