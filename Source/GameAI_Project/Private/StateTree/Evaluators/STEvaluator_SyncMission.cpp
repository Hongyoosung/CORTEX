// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Evaluators/STEvaluator_SyncMission.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Team/FollowerAgentComponent.h"

void FSTEvaluator_SyncMission::TreeStart(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp ? InstanceData.StateTreeComp->GetOwner() : nullptr);
	FString PawnName = Pawn ? Pawn->GetName() : TEXT("Unknown");

	if (!InstanceData.StateTreeComp)
	{
		UE_LOG(LogTemp, Error, TEXT("[SYNC Mission] '%s' TreeStart: ❌ StateTreeComp is null!"), *PawnName);
		return;
	}

	// Get SHARED context from component (not a local copy!)
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Initialize context outputs from follower component
	// CRITICAL: This must happen in TreeStart so conditions can evaluate during initial state selection
	if (SharedContext.FollowerComponent)
	{
		UMission* CurrentMission = SharedContext.FollowerComponent->GetCurrentMission();
		bool bHasMission = CurrentMission != nullptr && CurrentMission->IsActive();

		// Set context outputs (shared with conditions/tasks)
		SharedContext.CurrentMission = CurrentMission;
		SharedContext.bHasActiveMission = bHasMission;

		// Sync PrimaryTarget from Mission
		if (CurrentMission)
		{
			SharedContext.PrimaryTarget = CurrentMission->TargetActor;

			// Initialize tracking variables
			InstanceData.LastMissionType = CurrentMission->Type;
			InstanceData.LastMission = CurrentMission;

		}
		else
		{
			InstanceData.LastMission = nullptr;
			UE_LOG(LogTemp, Warning, TEXT("[SYNC Mission] '%s' TreeStart: ⚠️ No Mission assigned"), *PawnName);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[SYNC Mission] '%s' TreeStart: ❌ FollowerComponent is null!"), *PawnName);
	}
}

void FSTEvaluator_SyncMission::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp ? InstanceData.StateTreeComp->GetOwner() : nullptr);
	FString PawnName = Pawn ? Pawn->GetName() : TEXT("Unknown");

	if (!InstanceData.StateTreeComp)
	{
		UE_LOG(LogTemp, Error, TEXT("[SYNC Mission] '%s' Tick: ❌ StateTreeComp is null!"), *PawnName);
		return;
	}

	// Get SHARED context from component (not a local copy!)
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	if (!SharedContext.FollowerComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[SYNC Mission] '%s' Tick: ❌ FollowerComponent is null!"), *PawnName);
		return;
	}

	// Get current Mission from follower component
	UMission* NewMission = SharedContext.FollowerComponent->GetCurrentMission();
	bool bHasNewMission = NewMission != nullptr && NewMission->IsActive();

	// Update context outputs (shared with all tasks/evaluators)
	SharedContext.CurrentMission = NewMission;
	SharedContext.bHasActiveMission = bHasNewMission;

	// Update primary target from Mission
	if (NewMission)
	{
		SharedContext.PrimaryTarget = NewMission->TargetActor;
	}
	else
	{
		SharedContext.PrimaryTarget = nullptr;
	}

	// Log Mission changes (Warning level for visibility)
	if (NewMission != InstanceData.LastMission)
	{
		if (NewMission)
		{
			FString TargetInfo = NewMission->TargetActor ?
				FString::Printf(TEXT("Target: %s"), *NewMission->TargetActor->GetName()) :
				TEXT("Target: None");

			FString OldTypeStr = InstanceData.LastMission ?
				UEnum::GetValueAsString(InstanceData.LastMission->Type) :
				TEXT("None");

			/*UE_LOG(LogTemp, Warning, TEXT("[SYNC Mission] 📝 Mission changed: '%s' → '%s', Active=%d, %s"),
				*OldTypeStr,
				*UEnum::GetValueAsString(NewMission->Type),
				bHasNewMission,
				*TargetInfo);*/

			InstanceData.LastMissionType = NewMission->Type;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[SYNC Mission] 📝 Mission cleared (was: %s)"),
				InstanceData.LastMission ? *UEnum::GetValueAsString(InstanceData.LastMission->Type) : TEXT("None"));
		}

		InstanceData.LastMission = NewMission;
	}
}

void FSTEvaluator_SyncMission::TreeStop(FStateTreeExecutionContext& Context) const
{
	// No cleanup needed
}
