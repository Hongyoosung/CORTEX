// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/STTask_Dead.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Combat/Components/AgentPerceptionComponent.h"
#include "Animation/AnimInstance.h"
#include "TimerManager.h"
#include "Engine/World.h"

EStateTreeRunStatus FSTTask_Dead::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.StateTreeComp)
	{
		UE_LOG(LogTemp, Error, TEXT("STTask_Dead: StateTreeComp is null"));
		return EStateTreeRunStatus::Failed;
	}

	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Get Pawn from either AIController (normal AI) or directly from owner (Schola)
	APawn* Pawn = nullptr;
	if (SharedContext.AIController)
	{
		Pawn = SharedContext.AIController->GetPawn();
	}
	else
	{
		// Schola mode: Get pawn from component owner
		Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
	}

	if (!Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("STTask_Dead: Cannot get Pawn"));
		return EStateTreeRunStatus::Failed;
	}

	// Reset state
	InstanceData.TimeSinceDeath = 0.0f;
	InstanceData.bAnimationStarted = false;
	InstanceData.bMarkedForDestruction = false;

	// Stop all movement (only if AIController exists)
	if (SharedContext.AIController)
	{
		SharedContext.AIController->StopMovement();
		SharedContext.AIController->ClearFocus(EAIFocusPriority::Gameplay);
	}

	// NOTE: Do NOT call StopLogic() - StateTree IS the brain component, stopping it from within causes crashes
	// The Dead state itself keeps the agent disabled until respawn

	// Disable character movement component
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (UCharacterMovementComponent* MovementComp = Character->GetCharacterMovement())
		{
			MovementComp->DisableMovement();
			MovementComp->StopMovementImmediately();
		}
	}

	// Hide mesh (make invisible)
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (USkeletalMeshComponent* MeshComp = Character->GetMesh())
		{
			MeshComp->SetVisibility(false, true); // Hide mesh and propagate to children
			UE_LOG(LogTemp, Log, TEXT("STTask_Dead: Mesh hidden for %s"), *Pawn->GetName());
		}
	}
	else
	{
		// For non-character pawns, hide all components
		TArray<UActorComponent*> Components;
		Pawn->GetComponents(UPrimitiveComponent::StaticClass(), Components);
		for (UActorComponent* Comp : Components)
		{
			if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Comp))
			{
				PrimComp->SetVisibility(false, true);
			}
		}
	}

	// Disable perception (stop detecting enemies)
	UAgentPerceptionComponent* PerceptionComp = Pawn->FindComponentByClass<UAgentPerceptionComponent>();
	if (PerceptionComp)
	{
		PerceptionComp->SetActive(false);
		UE_LOG(LogTemp, Log, TEXT("STTask_Dead: Perception disabled for %s"), *Pawn->GetName());
	}

	// Disable collision if configured
	if (InstanceData.bDisableCollision)
	{
		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			// Disable capsule collision (character body)
			if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}

			// Disable mesh collision (prevents bullets from hitting dead bodies)
			if (USkeletalMeshComponent* MeshComp = Character->GetMesh())
			{
				MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
		else
		{
			Pawn->SetActorEnableCollision(false);
		}
	}

	// Play death animation (if visible - optional, since we're hiding)
	// PlayDeathAnimation(Context);

	// Enable ragdoll if configured (NOTE: Conflicts with hiding mesh, so skip if hiding)
	// if (InstanceData.bEnableRagdoll)
	// {
	// 	EnableRagdoll(Context);
	// }

	UE_LOG(LogTemp, Log, TEXT("STTask_Dead: Agent %s fully disabled (invisible, no collision, no perception)"),
		*Pawn->GetName());

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_Dead::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// Update timer
	InstanceData.TimeSinceDeath += DeltaTime;

	// EPISODIC RL TRAINING: Do NOT destroy actors - they need to respawn on episode reset
	// The SimulationManager will handle actor lifecycle during episodes
	// Actors are disabled visually (ragdoll/animation) but remain in world for respawn

	// Keep running indefinitely (actor stays in Dead state until episode reset)
	return EStateTreeRunStatus::Running;
}

void FSTTask_Dead::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	UE_LOG(LogTemp, Warning, TEXT("STTask_Dead: Exiting death state (time: %.1fs) - Re-enabling actor"),
		InstanceData.TimeSinceDeath);

	if (!InstanceData.StateTreeComp)
	{
		UE_LOG(LogTemp, Error, TEXT("STTask_Dead: StateTreeComp is null"));
		return;
	}

	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Get Pawn from either AIController (normal AI) or directly from owner (Schola)
	APawn* Pawn = nullptr;
	if (SharedContext.AIController)
	{
		Pawn = SharedContext.AIController->GetPawn();
	}
	else
	{
		// Schola mode: Get pawn from component owner
		Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
	}

	if (!Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("STTask_Dead: Cannot exit - Pawn is NULL"));
		return;
	}

	// Re-enable mesh visibility
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (USkeletalMeshComponent* MeshComp = Character->GetMesh())
		{
			MeshComp->SetVisibility(true, true); // Show mesh and propagate to children
			UE_LOG(LogTemp, Log, TEXT("STTask_Dead: Mesh shown for %s"), *Pawn->GetName());
		}
	}
	else
	{
		// For non-character pawns, show all components
		TArray<UActorComponent*> Components;
		Pawn->GetComponents(UPrimitiveComponent::StaticClass(), Components);
		for (UActorComponent* Comp : Components)
		{
			if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Comp))
			{
				PrimComp->SetVisibility(true, true);
			}
		}
	}

	// Re-enable perception
	UAgentPerceptionComponent* PerceptionComp = Pawn->FindComponentByClass<UAgentPerceptionComponent>();
	if (PerceptionComp)
	{
		PerceptionComp->SetActive(true);
		UE_LOG(LogTemp, Log, TEXT("STTask_Dead: Perception re-enabled for %s"), *Pawn->GetName());
	}

	// Re-enable collision
	if (InstanceData.bDisableCollision)
	{
		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			// Re-enable capsule collision (character body)
			if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}

			// Re-enable mesh collision (restore to QueryOnly to avoid physics simulation)
			if (USkeletalMeshComponent* MeshComp = Character->GetMesh())
			{
				// Disable ragdoll if it was enabled (restore normal physics)
				if (InstanceData.bEnableRagdoll)
				{
					MeshComp->SetSimulatePhysics(false);
				}
				MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			}

			// Re-enable character movement
			if (UCharacterMovementComponent* MovementComp = Character->GetCharacterMovement())
			{
				MovementComp->SetMovementMode(MOVE_Walking);
				MovementComp->Activate(); // Ensure movement component is active
			}
		}
		else
		{
			Pawn->SetActorEnableCollision(true);
		}
	}

	// NOTE: Do NOT call RestartLogic() - StateTree handles its own state transitions
	// Exiting this task means StateTree is already transitioning to a new state

	UE_LOG(LogTemp, Warning, TEXT("STTask_Dead: Actor %s fully re-enabled for respawn"), *Pawn->GetName());
}

void FSTTask_Dead::PlayDeathAnimation(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.DeathMontage)
	{
		UE_LOG(LogTemp, Warning, TEXT("STTask_Dead: No death montage assigned"));
		return;
	}

	if (!InstanceData.StateTreeComp)
	{
		return;
	}

	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Get Pawn from either AIController (normal AI) or directly from owner (Schola)
	APawn* Pawn = nullptr;
	if (SharedContext.AIController)
	{
		Pawn = SharedContext.AIController->GetPawn();
	}
	else
	{
		Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
	}

	if (!Pawn)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!Character)
	{
		UE_LOG(LogTemp, Warning, TEXT("STTask_Dead: Pawn is not a Character, cannot play montage"));
		return;
	}

	UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("STTask_Dead: No AnimInstance found"));
		return;
	}

	// Play the death montage
	float MontageLength = AnimInstance->Montage_Play(InstanceData.DeathMontage);
	if (MontageLength > 0.0f)
	{
		InstanceData.bAnimationStarted = true;
		UE_LOG(LogTemp, Log, TEXT("STTask_Dead: Playing death montage (length: %.1fs)"), MontageLength);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("STTask_Dead: Failed to play death montage"));
	}
}

void FSTTask_Dead::EnableRagdoll(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.StateTreeComp)
	{
		return;
	}

	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Get Pawn from either AIController (normal AI) or directly from owner (Schola)
	APawn* Pawn = nullptr;
	if (SharedContext.AIController)
	{
		Pawn = SharedContext.AIController->GetPawn();
	}
	else
	{
		Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
	}

	if (!Pawn)
	{
		return;
	}

	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!Character)
	{
		return;
	}

	// Get skeletal mesh
	USkeletalMeshComponent* MeshComp = Character->GetMesh();
	if (!MeshComp)
	{
		return;
	}

	// Disable character movement
	if (UCharacterMovementComponent* MovementComp = Character->GetCharacterMovement())
	{
		MovementComp->DisableMovement();
		MovementComp->StopMovementImmediately();
	}

	// Enable physics simulation on mesh
	MeshComp->SetSimulatePhysics(true);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionResponseToAllChannels(ECR_Block);

	UE_LOG(LogTemp, Log, TEXT("STTask_Dead: Ragdoll enabled"));
}

void FSTTask_Dead::DestroyActor(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.StateTreeComp)
	{
		return;
	}

	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Get Pawn from either AIController (normal AI) or directly from owner (Schola)
	APawn* Pawn = nullptr;
	if (SharedContext.AIController)
	{
		Pawn = SharedContext.AIController->GetPawn();
	}
	else
	{
		Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
	}

	if (!Pawn)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("STTask_Dead: Scheduling destruction for actor %s"), *Pawn->GetName());

	// IMPORTANT: Defer destruction to next frame to avoid modifying StateTree while it's ticking
	// This prevents the "Ensure condition failed: Exec.CurrentPhase == EStateTreeUpdatePhase::Unset" error
	UWorld* World = Pawn->GetWorld();
	if (World)
	{
		// Capture weak references to avoid issues if objects are already destroyed
		TWeakObjectPtr<AAIController> WeakController = SharedContext.AIController;
		TWeakObjectPtr<APawn> WeakPawn = Pawn;

		FTimerHandle TimerHandle;
		World->GetTimerManager().SetTimerForNextTick([WeakController, WeakPawn]()
		{
			// Unpossess first (this triggers StopStateTree safely outside of tick)
			if (WeakController.IsValid())
			{
				WeakController->UnPossess();
			}

			// Destroy the pawn
			if (WeakPawn.IsValid())
			{
				UE_LOG(LogTemp, Log, TEXT("STTask_Dead: Destroying actor %s"), *WeakPawn->GetName());
				WeakPawn->Destroy();
			}
		});
	}
}
