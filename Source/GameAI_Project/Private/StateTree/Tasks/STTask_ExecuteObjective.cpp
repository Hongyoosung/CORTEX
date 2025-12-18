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
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryInstanceBlueprintWrapper.h"

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

	// Reset action throttle timer
	InstanceData.TimeSinceLastAction = 0.0f;

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


	// Check abort conditions
	if (!SharedContext.bIsAlive || !SharedContext.CurrentObjective)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// Check if objective completed or failed
	if (CheckObjectiveStatus(Context))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	SharedContext.TimeInTacticalAction += DeltaTime;

	// v4.0: Action throttling - decouple execution rate from FPS
	// Only execute actions at fixed rate (default 20 Hz) to match Schola's action frequency
	InstanceData = Context.GetInstanceData(*this);
	InstanceData.TimeSinceLastAction += DeltaTime;

	bool bShouldExecuteAction = (InstanceData.TimeSinceLastAction >= InstanceData.ActionApplicationInterval);

	if (bShouldExecuteAction)
	{
		// Reset timer for next interval
		InstanceData.TimeSinceLastAction = 0.0f;

		// v4.0: Execute macro action components directly
		// Action already set by TacticalActuator (Schola) or local RL policy
		const FTacticalAction& Action = SharedContext.CurrentAtomicAction;

		ExecuteMovement(Context, Action, DeltaTime);
		ExecuteAiming(Context, Action, DeltaTime);
		ExecuteFire(Context, Action);
		ExecuteCrouch(Context, Action);
		// Note: ExecuteAbility removed - not needed for v4.0
	}

	// Calculate and provide reward (every frame, not throttled)
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


void FSTTask_ExecuteObjective::ExecuteMovement(FStateTreeExecutionContext& Context, const FTacticalAction& Action, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[MOVE v4.0] No ControlledPawn available"));
		return;
	}

	if (!InstanceData.AIController)
	{
		UE_LOG(LogTemp, Error, TEXT("[MOVE v4.0] '%s': No AIController - cannot use NavMesh"), *Pawn->GetName());
		return;
	}

	// v4.0: Use macro action for high-level tactical movement
	const FMacroAction& Macro = Action.MacroAction;

	// Query EQS for tactical positions based on PositionChoice
	TArray<FVector> CandidatePositions = QueryEQSPositions(Context, Macro.PositionChoice);

	if (CandidatePositions.Num() > 0)
	{
		FVector TargetLocation = CandidatePositions[0]; // Best EQS result
		float AcceptanceRadius = 100.0f; // 1 meter acceptance radius

		InstanceData.AIController->MoveToLocation(TargetLocation, AcceptanceRadius);
		SharedContext.MovementDestination = TargetLocation;
		SharedContext.bIsMoving = true;

		#if !UE_BUILD_SHIPPING
		UE_LOG(LogTemp, Display, TEXT("[MOVE v4.0] '%s': NavMesh → %s (%.0f cm away)"),
			*Pawn->GetName(),
			*UEnum::GetValueAsString(Macro.PositionChoice),
			FVector::Dist(Pawn->GetActorLocation(), TargetLocation));
		#endif
	}
	else
	{
		// No valid position found - EQS query failed
		InstanceData.AIController->StopMovement();
		SharedContext.bIsMoving = false;

		if (Macro.PositionChoice != ETacticalPosition::Hold)
		{
			UE_LOG(LogTemp, Error, TEXT("[MOVE v4.0] ❌ '%s': EQS query failed for %s - agent stuck!"),
				*Pawn->GetName(), *UEnum::GetValueAsString(Macro.PositionChoice));
		}
	}
}

void FSTTask_ExecuteObjective::ExecuteAiming(FStateTreeExecutionContext& Context, const FTacticalAction& Action, float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		return;
	}

	// v4.0: Use macro action for engine-driven aiming
	const FMacroAction& Macro = Action.MacroAction;

	if (Macro.TargetIndex >= 0)
	{
		// Get enemy actor from observation system
		AActor* TargetEnemy = GetEnemyByIndex(Context, Macro.TargetIndex);

		if (TargetEnemy && InstanceData.AIController)
		{
			// Engine handles aiming automatically
			InstanceData.AIController->SetFocus(TargetEnemy);

			SharedContext.PrimaryTarget = TargetEnemy;

			#if !UE_BUILD_SHIPPING
			UE_LOG(LogTemp, Display, TEXT("[AIM v4.0] '%s': SetFocus → Enemy_%d ('%s')"),
				*Pawn->GetName(), Macro.TargetIndex, *TargetEnemy->GetName());
			#endif
		}
		else
		{
			// Target index valid but enemy not found - clear focus
			if (InstanceData.AIController)
			{
				InstanceData.AIController->ClearFocus(EAIFocusPriority::Gameplay);
			}
			SharedContext.PrimaryTarget = nullptr;

			#if !UE_BUILD_SHIPPING
			UE_LOG(LogTemp, Warning, TEXT("[AIM v4.0] '%s': Target_%d not found, clearing focus"),
				*Pawn->GetName(), Macro.TargetIndex);
			#endif
		}
	}
	else
	{
		// No target - clear focus
		if (InstanceData.AIController)
		{
			InstanceData.AIController->ClearFocus(EAIFocusPriority::Gameplay);
		}
		SharedContext.PrimaryTarget = nullptr;
	}
}

void FSTTask_ExecuteObjective::ExecuteFire(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

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

	// v4.0: Use macro action fire mode
	const FMacroAction& Macro = Action.MacroAction;

	switch (Macro.FireMode)
	{
	case EFireMode::HoldFire:
		// Don't fire
		break;

	case EFireMode::Fire:
		// Fire at focused target (if within aim tolerance)
		if (InstanceData.AIController && InstanceData.AIController->GetFocusActor())
		{
			if (!WeaponComp->CanFire())
			{
				return; // On cooldown or out of ammo
			}

			// Fire in current facing direction (SetFocus already aims)
			FVector FireDirection = Pawn->GetActorForwardVector();
			bool bFired = WeaponComp->FireInDirection(FireDirection);

			#if !UE_BUILD_SHIPPING
			if (bFired)
			{
				UE_LOG(LogTemp, Display, TEXT("[FIRE v4.0] '%s': Fired at '%s'"),
					*Pawn->GetName(), *InstanceData.AIController->GetFocusActor()->GetName());
			}
			#endif
		}
		break;

	case EFireMode::Suppress:
		// Fire near enemy cover location (even if not visible)
		// TODO: Implement suppressive fire logic
		if (WeaponComp->CanFire())
		{
			FVector FireDirection = Pawn->GetActorForwardVector();
			WeaponComp->FireInDirection(FireDirection);

			#if !UE_BUILD_SHIPPING
			UE_LOG(LogTemp, Display, TEXT("[SUPPRESS v4.0] '%s': Suppressive fire"),
				*Pawn->GetName());
			#endif
		}
		break;
	}
}

void FSTTask_ExecuteObjective::ExecuteCrouch(FStateTreeExecutionContext& Context, const FTacticalAction& Action) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		return;
	}

	UCharacterMovementComponent* MovementComp = Pawn->FindComponentByClass<UCharacterMovementComponent>();
	if (!MovementComp)
	{
		static bool bWarningShown = false;
		if (!bWarningShown)
		{
			UE_LOG(LogTemp, Error, TEXT("[STANCE] '%s': No CharacterMovementComponent found!"), *Pawn->GetName());
			bWarningShown = true;
		}
		return;
	}

	// v4.0: Use macro action stance
	const FMacroAction& Macro = Action.MacroAction;

	switch (Macro.Stance)
	{
	case EStance::Stand:
		if (MovementComp->IsCrouching())
		{
			MovementComp->bWantsToCrouch = false;
			#if !UE_BUILD_SHIPPING
			UE_LOG(LogTemp, Display, TEXT("[STANCE v4.0] '%s': Standing"), *Pawn->GetName());
			#endif
		}
		// TODO: Exit prone if needed
		break;

	case EStance::Crouch:
		if (!MovementComp->IsCrouching())
		{
			MovementComp->bWantsToCrouch = true;
			#if !UE_BUILD_SHIPPING
			UE_LOG(LogTemp, Display, TEXT("[STANCE v4.0] '%s': Crouching"), *Pawn->GetName());
			#endif
		}
		break;

	case EStance::Prone:
		// TODO: Implement prone stance (custom movement mode)
		// UE5 CharacterMovement doesn't support prone natively
		#if !UE_BUILD_SHIPPING
		UE_LOG(LogTemp, Warning, TEXT("[STANCE v4.0] '%s': Prone not implemented yet"), *Pawn->GetName());
		#endif
		break;
	}
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

//------------------------------------------------------------------------------
// v4.0 MACRO ACTION HELPERS
//------------------------------------------------------------------------------

TArray<FVector> FSTTask_ExecuteObjective::QueryEQSPositions(FStateTreeExecutionContext& Context, ETacticalPosition PositionType) const
{
	TArray<FVector> Results;

	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	APawn* Pawn = InstanceData.ControlledPawn;
	if (!Pawn)
	{
		return Results;
	}

	FVector AgentLocation = Pawn->GetActorLocation();

	switch (PositionType)
	{
	case ETacticalPosition::Hold:
		// Stay at current location (no EQS needed)
		Results.Add(AgentLocation);
		break;

	case ETacticalPosition::ForwardCover:
		// Query EQS for cover points closer to objective
		Results = RunEQSQuery(Pawn, FName("EQS_ForwardCover"));
		break;

	case ETacticalPosition::Retreat:
		// Query EQS for cover points away from enemies
		Results = RunEQSQuery(Pawn, FName("EQS_RetreatCover"));
		break;

	case ETacticalPosition::Advance:
		// Move toward objective without cover requirement
		Results = RunEQSQuery(Pawn, FName("EQS_Advance"));
		break;
	}

	return Results;
}

TArray<FVector> FSTTask_ExecuteObjective::RunEQSQuery(APawn* Pawn, FName QueryName) const
{
	TArray<FVector> Results;

	if (!Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[EQS v4.0] ❌ RunEQSQuery failed: Invalid Pawn"));
		return Results;
	}

	// Load EQS query asset from Content/AI/EQS/ directory
	FString AssetPath = FString::Printf(TEXT("/Game/AI/EQS/%s.%s"), *QueryName.ToString(), *QueryName.ToString());
	UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *AssetPath);

	if (!Query)
	{
		UE_LOG(LogTemp, Error, TEXT("[EQS v4.0] ❌ '%s': Failed to load EQS query '%s' at path '%s'"),
			*Pawn->GetName(), *QueryName.ToString(), *AssetPath);
		UE_LOG(LogTemp, Error, TEXT("[EQS v4.0]    Create the query asset in UE Editor: Content/AI/EQS/%s.uasset"),
			*QueryName.ToString());
		return Results;
	}

	UWorld* World = Pawn->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[EQS v4.0] ❌ '%s': No valid World for EQS query"), *Pawn->GetName());
		return Results;
	}

	UEnvQueryManager* QueryManager = UEnvQueryManager::GetCurrent(World);
	if (!QueryManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[EQS v4.0] ❌ '%s': EnvQueryManager not available"), *Pawn->GetName());
		return Results;
	}

	// Run instant EQS query (synchronous execution)
	FEnvQueryRequest QueryRequest(Query, Pawn);
	TSharedPtr<FEnvQueryResult> QueryResult = QueryManager->RunInstantQuery(QueryRequest, EEnvQueryRunMode::AllMatching);

	if (!QueryResult.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[EQS v4.0] ❌ '%s': EQS query '%s' returned invalid result"),
			*Pawn->GetName(), *QueryName.ToString());
		return Results;
	}

	if (!QueryResult->IsSuccessful())
	{
		UE_LOG(LogTemp, Error, TEXT("[EQS v4.0] ❌ '%s': EQS query '%s' failed to execute"),
			*Pawn->GetName(), *QueryName.ToString());
		return Results;
	}

	// Extract location results
	const int32 ItemCount = QueryResult->Items.Num();
	if (ItemCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EQS v4.0] ⚠️ '%s': EQS query '%s' returned no valid positions"),
			*Pawn->GetName(), *QueryName.ToString());
		return Results;
	}

	Results.Reserve(ItemCount);

	for (int i = 0; i < QueryResult->Items.Num(); ++i)
	{
		FVector ItemLocation = QueryResult->GetItemAsLocation(i);

		if (!ItemLocation.IsZero())
		{
			Results.Add(ItemLocation);
		}
	}

	#if !UE_BUILD_SHIPPING
	UE_LOG(LogTemp, Display, TEXT("[EQS v4.0] ✅ '%s': Query '%s' returned %d positions (best score: %.1f)"),
		*Pawn->GetName(), *QueryName.ToString(), Results.Num(),
		QueryResult->Items.Num() > 0 ? QueryResult->Items[0].Score : 0.0f);
	#endif

	return Results;
}

AActor* FSTTask_ExecuteObjective::GetEnemyByIndex(FStateTreeExecutionContext& Context, int32 EnemyIndex) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	FFollowerStateTreeContext& SharedContext = InstanceData.StateTreeComp->GetSharedContext();

	// Get visible enemies from context
	if (EnemyIndex < 0 || EnemyIndex >= SharedContext.VisibleEnemies.Num())
	{
		return nullptr;
	}

	AActor* Enemy = SharedContext.VisibleEnemies[EnemyIndex];

	// Validate enemy is still alive
	if (Enemy && Enemy->IsValidLowLevel())
	{
		return Enemy;
	}

	return nullptr;
}
