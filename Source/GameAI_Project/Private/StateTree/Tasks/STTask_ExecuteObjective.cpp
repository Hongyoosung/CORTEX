// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/Tasks/STTask_ExecuteObjective.h"
#include "StateTree/FollowerStateTreeContext.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Team/FollowerAgentComponent.h"
#include "Team/Objective.h"
#include "Combat/WeaponComponent.h"
#include "Combat/HealthComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RL/RLPolicyNetwork.h"
#include "DrawDebugHelpers.h"

EStateTreeRunStatus FSTTask_ExecuteObjective::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (!InstanceData.StateTreeComp)
	{
		UE_LOG(LogTemp, Error, TEXT("STTask_ExecuteObjective: StateTreeComp is null"));
		return EStateTreeRunStatus::Failed;
	}

	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// CRITICAL: Only require FollowerComponent (AIController is optional for Schola compatibility)
	if (!SharedContext.FollowerComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("STTask_ExecuteObjective: Missing FollowerComponent"));
		return EStateTreeRunStatus::Failed;
	}

	// Get Pawn from InstanceData (bound to FollowerContext.ControlledPawn)
	APawn* Pawn = InstanceData.ControlledPawn;

	if (!Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ STTask_ExecuteObjective: ControlledPawn not bound in StateTree asset!"));
		UE_LOG(LogTemp, Error, TEXT("   Bind 'ControlledPawn' to 'FollowerContext.ControlledPawn' in the StateTree asset."));
		return EStateTreeRunStatus::Failed;
	}

	FString PawnName = Pawn->GetName();
	FString ObjectiveName = SharedContext.CurrentObjective
		? UEnum::GetValueAsString(SharedContext.CurrentObjective->Type)
		: TEXT("None");

	UE_LOG(LogTemp, Warning, TEXT("🎯 [EXEC OBJ] '%s': ENTER - Objective: %s, Health: %.1f%%, Returning RUNNING"),
		*PawnName, *ObjectiveName, SharedContext.CurrentObservation.AgentHealth);

	SharedContext.TimeInTacticalAction = 0.0f;
	SharedContext.ActionProgress = 0.0f;

	UE_LOG(LogTemp, Warning, TEXT("🎯 [EXEC OBJ] '%s': EnterState returning Running (StateTree should call Tick next)"), *PawnName);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSTTask_ExecuteObjective::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// DIAGNOSTIC: Log EVERY tick (not throttled) to diagnose issue
	static int32 TickCounter = 0;
	TickCounter++;

	APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
	UE_LOG(LogTemp, Warning, TEXT("🔄 [EXEC OBJ TICK] '%s': Tick #%d (DeltaTime=%.3f), Alive=%d, Objective=%s, ScholaAction=%d"),
		*GetNameSafe(Pawn),
		TickCounter,
		DeltaTime,
		SharedContext.bIsAlive ? 1 : 0,
		SharedContext.CurrentObjective ? *UEnum::GetValueAsString(SharedContext.CurrentObjective->Type) : TEXT("NULL"),
		SharedContext.bScholaActionReceived ? 1 : 0);

	// Check abort conditions
	if (!SharedContext.bIsAlive || !SharedContext.CurrentObjective)
	{
		Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
		UE_LOG(LogTemp, Warning, TEXT("❌ [EXEC OBJ EXIT] '%s': Exiting - Alive=%d, Objective=%s"),
			*GetNameSafe(Pawn),
			SharedContext.bIsAlive ? 1 : 0,
			SharedContext.CurrentObjective ? TEXT("Valid") : TEXT("NULL"));
		return EStateTreeRunStatus::Succeeded;
	}

	// Check if objective completed or failed
	if (CheckObjectiveStatus(Context))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	SharedContext.TimeInTacticalAction += DeltaTime;

	// Execute atomic action from policy
	ExecuteAtomicAction(Context, DeltaTime);

	// Calculate and provide reward
	float Reward = CalculateObjectiveReward(Context, DeltaTime);
	if (Reward != 0.0f && SharedContext.FollowerComponent)
	{
		SharedContext.FollowerComponent->ProvideReward(Reward, false);
	}

	return EStateTreeRunStatus::Running;
}

void FSTTask_ExecuteObjective::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());

	// Log detailed exit information
	UE_LOG(LogTemp, Error, TEXT("❌ [EXEC OBJ EXIT] '%s': Exiting after %.1fs, Reason: %s"),
		*GetNameSafe(Pawn),
		SharedContext.TimeInTacticalAction,
		*UEnum::GetValueAsString(Transition.ChangeType));

	UE_LOG(LogTemp, Error, TEXT("   → Objective=%s, Alive=%d, Transition=%s"),
		SharedContext.CurrentObjective ? *UEnum::GetValueAsString(SharedContext.CurrentObjective->Type) : TEXT("NULL"),
		SharedContext.bIsAlive ? 1 : 0,
		Transition.NextActiveFrames.Num() > 0 ? TEXT("To another state") : TEXT("Tree stopped"));
}

void FSTTask_ExecuteObjective::ExecuteAtomicAction(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	FTacticalAction RawAction;

	// Priority 1: Use action from Schola (real-time training)
	if (SharedContext.bScholaActionReceived)
	{
		// Action already set by TacticalActuator
		RawAction = SharedContext.CurrentAtomicAction;
		SharedContext.bScholaActionReceived = false; // Reset flag

		APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
		UE_LOG(LogTemp, Display, TEXT("🔗 [SCHOLA ACTION] '%s': Move=(%.2f,%.2f) Speed=%.2f, Look=(%.2f,%.2f), Fire=%d"),
			*GetNameSafe(Pawn),
			RawAction.MoveDirection.X, RawAction.MoveDirection.Y, RawAction.MoveSpeed,
			RawAction.LookDirection.X, RawAction.LookDirection.Y,
			RawAction.bFire ? 1 : 0);
	}
	// Priority 2: Query local RL policy (inference mode)
	else if (SharedContext.TacticalPolicy && SharedContext.CurrentObjective)
	{
		// Diagnostic: Log why Schola action wasn't used
		APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
		UE_LOG(LogTemp, Display, TEXT("📊 [POLICY MODE] '%s': bScholaActionReceived=%d → Using local RL policy"),
			*GetNameSafe(Pawn), SharedContext.bScholaActionReceived ? 1 : 0);
		// Get action with objective context and mask
		RawAction = SharedContext.TacticalPolicy->GetActionWithMask(
			SharedContext.CurrentObservation,
			SharedContext.CurrentObjective,
			SharedContext.ActionMask);
	}
	// Priority 3: Fallback to default (zero) action
	else
	{
		APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
		UE_LOG(LogTemp, Warning, TEXT("❌ [NO ACTION] '%s': Policy=%s, Objective=%s - Using default (ZERO) action"),
			*GetNameSafe(Pawn),
			SharedContext.TacticalPolicy ? TEXT("Valid") : TEXT("NULL"),
			SharedContext.CurrentObjective ? TEXT("Valid") : TEXT("NULL"));
		RawAction = FTacticalAction(); // Default action
	}

	// Apply spatial constraints
	FTacticalAction MaskedAction = ApplyMask(RawAction, SharedContext.ActionMask);

	// LOG: If mask changed action significantly
	bool bMaskModified = (MaskedAction.MoveDirection - RawAction.MoveDirection).SizeSquared() > 0.01f ||
	                     MaskedAction.bFire != RawAction.bFire;
	if (bMaskModified)
	{
		APawn* Pawn = Cast<APawn>(InstanceData.StateTreeComp->GetOwner());
		UE_LOG(LogTemp, Display, TEXT("[ACTION MASK] '%s': Constraints modified action"), *GetNameSafe(Pawn));
	}

	// Store in context for experience collection
	SharedContext.CurrentAtomicAction = MaskedAction;

	// Execute components of the action
	ExecuteMovement(Context, MaskedAction, DeltaTime);
	ExecuteAiming(Context, MaskedAction, DeltaTime);
	ExecuteFire(Context, MaskedAction);
	ExecuteCrouch(Context, MaskedAction);
	ExecuteAbility(Context, MaskedAction);
}

void FSTTask_ExecuteObjective::ExecuteMovement(FStateTreeExecutionContext& Context, const FTacticalAction& Action, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Get Pawn from InstanceData (bound to FollowerContext.ControlledPawn)
	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("ExecuteMovement: No ControlledPawn available"));
		return;
	}

	// Apply movement direction and speed
	FVector2D MoveDir = Action.MoveDirection;
	float MoveSpeed = Action.MoveSpeed;


	if (MoveDir.SizeSquared() > 0.01f) // Non-zero movement
	{
		// Convert 2D direction to world direction (relative to current rotation)
		FRotator CurrentRotation = Pawn->GetActorRotation();
		FVector ForwardDir = FRotationMatrix(CurrentRotation).GetUnitAxis(EAxis::X);
		FVector RightDir = FRotationMatrix(CurrentRotation).GetUnitAxis(EAxis::Y);

		FVector WorldMoveDir = (ForwardDir * MoveDir.X + RightDir * MoveDir.Y).GetSafeNormal();

		// Set movement destination
		FVector CurrentLocation = Pawn->GetActorLocation();
		FVector TargetLocation = CurrentLocation + WorldMoveDir * 500.0f; // 5m ahead

		// Apply speed to movement component
		if (UCharacterMovementComponent* MovementComp = Pawn->FindComponentByClass<UCharacterMovementComponent>())
		{
			float BaseSpeed = 600.0f;
			MovementComp->MaxWalkSpeed = BaseSpeed * MoveSpeed * InstanceData.MovementSpeedMultiplier;
		}

		// Move using AI controller (normal AI) or direct input (Schola)
		if (InstanceData.AIController)
		{
			// Normal AI mode: Use pathfinding
			InstanceData.AIController->MoveToLocation(TargetLocation, 50.0f);
			SharedContext.MovementDestination = TargetLocation;
			SharedContext.bIsMoving = true;

			UE_LOG(LogTemp, Display, TEXT("[MOVE EXEC AI] '%s': MoveToLocation(%.1f, %.1f, %.1f), Speed=%.1f"),
				*Pawn->GetName(),
				TargetLocation.X, TargetLocation.Y, TargetLocation.Z,
				Pawn->FindComponentByClass<UCharacterMovementComponent>()->MaxWalkSpeed);
		}
		else
		{
			// Schola mode: Use direct movement input (no pathfinding)
			Pawn->AddMovementInput(WorldMoveDir, MoveSpeed);
			SharedContext.MovementDestination = TargetLocation;
			SharedContext.bIsMoving = true;

			UE_LOG(LogTemp, Display, TEXT("[MOVE EXEC DIRECT] '%s': AddMovementInput(%.2f, %.2f, %.2f), Speed=%.1f"),
				*Pawn->GetName(),
				WorldMoveDir.X, WorldMoveDir.Y, WorldMoveDir.Z,
				Pawn->FindComponentByClass<UCharacterMovementComponent>()->MaxWalkSpeed);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MOVE EXEC STOP] No movement input"));

		// Stop movement
		if (InstanceData.AIController)
		{
			InstanceData.AIController->StopMovement();
		}
		// For Schola: Movement stops naturally when AddMovementInput isn't called
		SharedContext.bIsMoving = false;
	}
}

void FSTTask_ExecuteObjective::ExecuteAiming(FStateTreeExecutionContext& Context, const FTacticalAction& Action, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Get Pawn from InstanceData (bound to FollowerContext.ControlledPawn)
	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		return;
	}

	// Convert 2D look direction to rotation
	FVector2D LookDir = Action.LookDirection;

	// RL-ONLY AIMING: No fallback to auto-targeting
	// Agent must learn to aim via LookDirection output
	if (LookDir.SizeSquared() > 0.01f)
	{
		// FIXED: Agent-relative aiming (not world-based)
		// LookDir.X = Yaw [-1,1], LookDir.Y = Pitch [-1,1]
		float DeltaYaw = LookDir.X * 180.0f; // Relative yaw change (degrees)
		float DeltaPitch = LookDir.Y * 45.0f; // Relative pitch change (limit range)

		FRotator CurrentRotation = Pawn->GetActorRotation();
		FRotator TargetRotation = CurrentRotation + FRotator(DeltaPitch, DeltaYaw, 0.0f);

		// Interpolate rotation
		float RotSpeed = InstanceData.RotationSpeed;
		FRotator NewRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, RotSpeed / 180.0f);

		Pawn->SetActorRotation(NewRotation);
	}
	// REMOVED: Auto-aiming at PrimaryTarget when LookDir is zero
	// This was rule-based assistance that prevented true RL learning
}

void FSTTask_ExecuteObjective::ExecuteFire(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	if (!Action.bFire)
	{
		return;
	}

	// Get Pawn from InstanceData (bound to FollowerContext.ControlledPawn)
	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[EXEC FIRE] No ControlledPawn available"));
		return;
	}

	UWeaponComponent* WeaponComp = Pawn->FindComponentByClass<UWeaponComponent>();
	if (!WeaponComp)
	{
		UE_LOG(LogTemp, Error, TEXT("[EXEC FIRE] '%s': No WeaponComponent found"), *Pawn->GetName());
		return;
	}

	if (!WeaponComp->CanFire())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXEC FIRE] '%s': Weapon cannot fire (cooldown/ammo)"), *Pawn->GetName());
		return;
	}

	// RL-ONLY FIRING: Fire in current facing direction
	// No target validation, no LOS checks, no auto-aiming
	// Agent must learn to aim before firing for effectiveness
	FVector FireDirection = Pawn->GetActorForwardVector();
	bool bFired = WeaponComp->FireInDirection(FireDirection);

	if (bFired)
	{
		UE_LOG(LogTemp, Display, TEXT("[EXEC FIRE] ✅ '%s': FIRING in direction (%.2f, %.2f, %.2f)"),
			*Pawn->GetName(), FireDirection.X, FireDirection.Y, FireDirection.Z);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXEC FIRE] ❌ '%s': Fire failed (weapon state)"),
			*Pawn->GetName());
	}
	
	// REMOVED: PrimaryTarget and bHasLOS validation
	// REMOVED: FireAtTarget() with predictive aiming
	// Agent must learn targeting through trial and error
}

void FSTTask_ExecuteObjective::ExecuteCrouch(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Get Pawn from InstanceData (bound to FollowerContext.ControlledPawn)
	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		return;
	}

	// Toggle crouch via movement component
	if (UCharacterMovementComponent* MovementComp = Pawn->FindComponentByClass<UCharacterMovementComponent>())
	{
		if (Action.bCrouch && !MovementComp->IsCrouching())
		{
			MovementComp->bWantsToCrouch = true;
		}
		else if (!Action.bCrouch && MovementComp->IsCrouching())
		{
			MovementComp->bWantsToCrouch = false;
		}
	}
}

void FSTTask_ExecuteObjective::ExecuteAbility(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const
{
	if (!Action.bUseAbility)
	{
		return;
	}

	// Placeholder for ability system integration
	//UE_LOG(LogTemp, Log, TEXT("STTask_ExecuteObjective: Ability %d requested (not implemented)"), Action.AbilityID);
}

FTacticalAction FSTTask_ExecuteObjective::ApplyMask(const FTacticalAction& RawAction, const FActionSpaceMask& Mask) const
{
	FTacticalAction MaskedAction = RawAction;

	// Apply movement constraints
	if (Mask.bLockMovementX)
	{
		MaskedAction.MoveDirection.X = 0.0f;
	}
	if (Mask.bLockMovementY)
	{
		MaskedAction.MoveDirection.Y = 0.0f;
	}

	// Clamp speed
	MaskedAction.MoveSpeed = FMath::Clamp(MaskedAction.MoveSpeed, 0.0f, Mask.MaxSpeed);

	// Apply aiming constraints (LookDirection is normalized [-1,1], convert to angles for clamping)
	float Yaw = MaskedAction.LookDirection.X * 180.0f;
	float Pitch = MaskedAction.LookDirection.Y * 90.0f;

	Yaw = FMath::Clamp(Yaw, Mask.MinYaw, Mask.MaxYaw);
	Pitch = FMath::Clamp(Pitch, Mask.MinPitch, Mask.MaxPitch);

	MaskedAction.LookDirection.X = Yaw / 180.0f;
	MaskedAction.LookDirection.Y = Pitch / 90.0f;

	// Force crouch if required
	if (Mask.bForceCrouch)
	{
		MaskedAction.bCrouch = true;
	}

	// Safety lock prevents firing
	if (Mask.bSafetyLock)
	{
		MaskedAction.bFire = false;
	}

	return MaskedAction;
}

float FSTTask_ExecuteObjective::CalculateObjectiveReward(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	if (!SharedContext.CurrentObjective)
	{
		return 0.0f;
	}

	float Reward = 0.0f;

	// Small progress reward (encourage forward progress)
	float ObjProgress = SharedContext.CurrentObjective->GetProgress();
	if (ObjProgress > SharedContext.ActionProgress)
	{
		float ProgressDelta = ObjProgress - SharedContext.ActionProgress;
		Reward += ProgressDelta * 10.0f; // +10 reward per 100% progress
		SharedContext.ActionProgress = ObjProgress;
	}

	// Penalty for time inefficiency (encourage fast completion)
	if (SharedContext.CurrentObjective->TimeLimit > 0.0f)
	{
		float TimeRatio = SharedContext.CurrentObjective->TimeRemaining / SharedContext.CurrentObjective->TimeLimit;
		if (TimeRatio < 0.3f) // Less than 30% time remaining
		{
			Reward -= 0.5f * DeltaTime; // Small time penalty
		}
	}

	return Reward;
}

bool FSTTask_ExecuteObjective::CheckObjectiveStatus(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	if (!SharedContext.CurrentObjective)
	{
		return true; // No objective = exit
	}

	UObjective* Obj = SharedContext.CurrentObjective;

	// Check completion
	if (Obj->IsCompleted())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXEC OBJ] Objective COMPLETED"));

		// Provide completion reward
		if (SharedContext.FollowerComponent)
		{
			SharedContext.FollowerComponent->ProvideReward(50.0f, false); // Major reward
		}

		return true;
	}

	// Check failure
	if (Obj->IsFailed())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EXEC OBJ] Objective FAILED"));

		// Provide failure penalty
		if (SharedContext.FollowerComponent)
		{
			SharedContext.FollowerComponent->ProvideReward(-30.0f, false); // Major penalty
		}

		return true;
	}

	return false; // Still active
}
