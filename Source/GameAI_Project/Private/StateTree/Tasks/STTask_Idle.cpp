// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/STTask_Idle.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Team/FollowerAgentComponent.h"
#include "GameFramework/Pawn.h"

EStateTreeRunStatus FSTTask_Idle::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.StateTreeComp)
	{
		UE_LOG(LogTemp, Error, TEXT("STTask_Idle: StateTreeComp is null"));
		return EStateTreeRunStatus::Failed;
	}

	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
	FString PawnName = Pawn ? Pawn->GetName() : TEXT("Unknown");

	UE_LOG(LogTemp, Warning, TEXT("⏸️ [IDLE] '%s': ENTER - Waiting for Mission (StateTree will keep running)"), *PawnName);

	// CRITICAL: Return Running to keep StateTree alive
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_Idle::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Log periodically (every 2 seconds) to confirm idle state
	static TMap<const FSTTask_Idle*, float> LastLogTimes;
	float& LastLogTime = LastLogTimes.FindOrAdd(this, 0.0f);
	float CurrentTime = Context.GetWorld()->GetTimeSeconds();

	if (CurrentTime - LastLogTime > 2.0f)
	{
		APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
		FString MissionStr = SharedContext.CurrentMission
			? UEnum::GetValueAsString(SharedContext.CurrentMission->Type)
			: TEXT("None");

		UE_LOG(LogTemp, Display, TEXT("⏸️ [IDLE TICK] '%s': Still idle - Mission=%s, Alive=%d"),
			*GetNameSafe(Pawn),
			*MissionStr,
			SharedContext.bIsAlive ? 1 : 0);

		LastLogTime = CurrentTime;
	}

	// Check if agent died (should transition to Dead state via conditions)
	if (!SharedContext.bIsAlive)
	{
		APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
		UE_LOG(LogTemp, Warning, TEXT("❌ [IDLE EXIT] '%s': Agent died while idle"), *GetNameSafe(Pawn));
		return EStateTreeRunStatus::Succeeded; // Allow transition to Dead state
	}

	// Check if Mission received (should transition to ExecuteMission via conditions)
	if (SharedContext.bHasActiveMission && SharedContext.CurrentMission)
	{
		APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
		FString MissionStr = UEnum::GetValueAsString(SharedContext.CurrentMission->Type);
		UE_LOG(LogTemp, Warning, TEXT("✅ [IDLE EXIT] '%s': Mission received (%s), transitioning to execution"),
			*GetNameSafe(Pawn), *MissionStr);
		return EStateTreeRunStatus::Succeeded; // Allow transition to ExecuteMission
	}

	// CRITICAL: Keep returning Running to prevent StateTree termination
	return EStateTreeRunStatus::Running;
}

void FSTTask_Idle::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
	FString MissionStr = SharedContext.CurrentMission
		? UEnum::GetValueAsString(SharedContext.CurrentMission->Type)
		: TEXT("None");

	UE_LOG(LogTemp, Warning, TEXT("⏸️ [IDLE EXIT] '%s': Exiting idle - Mission=%s, Alive=%d, Transition=%s"),
		*GetNameSafe(Pawn),
		*MissionStr,
		SharedContext.bIsAlive ? 1 : 0,
		*UEnum::GetValueAsString(Transition.ChangeType));
}
