// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree/FollowerStateTreeComponent.h"
#include "StateTree/FollowerStateTreeSchema.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Team/Components/TeamCommsComponent.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "Combat/Components/HealthComponent.h"
#include "RL/RLPolicyNetwork.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "StateTreeModule\Public\StateTree.h"
#include "GameplayTagsManager.h"
#include "StateTreeEvents.h"

#if WITH_EDITOR
#include "StateTreeDelegates.h"
#endif

// Define StateTree event tags
const FGameplayTag UFollowerStateTreeComponent::Event_MissionReceived =
	FGameplayTag::RequestGameplayTag(FName("StateTree.Follower.MissionReceived"));
const FGameplayTag UFollowerStateTreeComponent::Event_FollowerDied =
	FGameplayTag::RequestGameplayTag(FName("StateTree.Follower.Died"));
const FGameplayTag UFollowerStateTreeComponent::Event_FollowerRespawned =
	FGameplayTag::RequestGameplayTag(FName("StateTree.Follower.Respawned"));

UFollowerStateTreeComponent::UFollowerStateTreeComponent()
	: Super()
	, FollowerComponent(nullptr)
	, HealthComponent(nullptr)
	, TeamCommsComponent(nullptr)
	, bAutoFindFollowerComponent(true)
	, TickLogCounter(0)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bAutoActivate = true;

	bStartLogicAutomatically = false;
}

void UFollowerStateTreeComponent::BeginPlay()
{
	UE_LOG(LogTemp, Warning, TEXT("🔵 UFollowerStateTreeComponent::BeginPlay CALLED for '%s'"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("NULL_OWNER"));

	// CRITICAL: Find components and initialize context BEFORE Super::BeginPlay()
	// Super::BeginPlay() may start the StateTree, and evaluators need valid context
	if (bAutoFindFollowerComponent)
	{
		if (!FollowerComponent)
		{
			FollowerComponent = FindFollowerComponent();
		}
		if (!HealthComponent)
		{
			HealthComponent = FindHealthComponent();
		}
		if (!TeamCommsComponent)
		{
			TeamCommsComponent = FindTeamCommsComponent();
		}
	}

	if (!FollowerComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("UFollowerStateTreeComponent: ❌ FollowerComponent not found!"));
		return;
	}

	if (!HealthComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("UFollowerStateTreeComponent: ⚠️ HealthComponent not found!"));
	}

	if (!TeamCommsComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("UFollowerStateTreeComponent: ⚠️ TeamCommsComponent not found!"));
	}

	// Initialize context BEFORE Super::BeginPlay() starts the tree
	InitializeContext();

	// Bind to follower events
	BindToFollowerEvents();

	// Bind status change delegate
	OnStateTreeRunStatusChanged.AddDynamic(this, &UFollowerStateTreeComponent::HandleOnStateTreeRunStatusChanged);

	// Bind to pawn controller change (to handle Schola Trainer possession)
	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		OwnerPawn->ReceiveControllerChangedDelegate.AddDynamic(this, &UFollowerStateTreeComponent::OnControllerChanged);
	}

	// NOW call parent class (may start StateTree with valid context)
	Super::BeginPlay();

	// [핵심] 초기화가 끝난 후 마지막에 시작 시도
	if (CheckRequirementsAndStart())
	{
		//UE_LOG(LogTemp, Warning, TEXT("UFollowerStateTreeComponent: StateTree started immediately in BeginPlay!"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("UFollowerStateTreeComponent: StateTree waiting for AIController..."));
	}

	// 상태 확인 로그
	EStateTreeRunStatus Status = GetStateTreeRunStatus();
	if (Status == EStateTreeRunStatus::Running)
	{
		UE_LOG(LogTemp, Warning, TEXT("UFollowerStateTreeComponent: ✅ StateTree successfully started and running!"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("UFollowerStateTreeComponent: ❌ StateTree not running after BeginPlay. Status=%s"),
			*UEnum::GetValueAsString(Status));
	}
}
void UFollowerStateTreeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// v9.0 FIX: Increment counter to prevent spam
	TickLogCounter++;

	// Deferred initialization if BeginPlay failed to find FollowerComponent
	if (!FollowerComponent && bAutoFindFollowerComponent)
	{
		FollowerComponent = FindFollowerComponent();
		if (FollowerComponent)
		{
			UE_LOG(LogTemp, Log, TEXT("UFollowerStateTreeComponent: FollowerComponent found on deferred initialization for '%s'"), *GetOwner()->GetName());
			InitializeContext();
			BindToFollowerEvents();
		}
		else
		{
			// Only log error once per 2 seconds to avoid spam
			static float LastErrorTime = 0.0f;
			if (GetWorld()->GetTimeSeconds() - LastErrorTime > 2.0f)
			{
				UE_LOG(LogTemp, Error, TEXT("UFollowerStateTreeComponent: FollowerComponent still not found on '%s'. Ensure UFollowerAgentComponent is added to the same actor!"), *GetOwner()->GetName());
				LastErrorTime = GetWorld()->GetTimeSeconds();
			}
		}
		return; // Skip update until initialized
	}

	// Update context from follower component every tick
	if (FollowerComponent)
	{
		UpdateContextFromFollower();
	}

	EStateTreeRunStatus CurrentStatus = GetStateTreeRunStatus();
	if (CurrentStatus != EStateTreeRunStatus::Running)
	{
		// v9.0 FIX: Don't restart StateTree when agent is dead
		// Dead agents should stay in Dead state until episode reset/respawn
		if (HealthComponent && !HealthComponent->IsAlive())
		{
			return; // Stay stopped while dead
		}

		// Schola 학습 중에는 컨트롤러가 늦게 붙을 수 있으므로
		// 컨트롤러가 유효해질 때까지 재시도를 반복하는 것은 괜찮으나,
		// 종료 이유(Succeeded/Failed)를 확인해야 함.

		// 1초에 한 번만 재시작 시도 로그 출력 (스팸 방지)
		if (TickLogCounter % 60 == 0)
		{
			// CheckRequirementsAndStart 내부 로그가 이미 있으므로 여기선 생략 가능
			CheckRequirementsAndStart();
		}
		else
		{
			// 매 틱마다 시도는 하되 로그는 남기지 않음 (반응성 유지)
			CheckRequirementsAndStart();
		}
	}
}

void UFollowerStateTreeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

TSubclassOf<UStateTreeSchema> UFollowerStateTreeComponent::GetSchema() const
{
	return UFollowerStateTreeSchema::StaticClass();
}

bool UFollowerStateTreeComponent::SetContextRequirements(FStateTreeExecutionContext& InContext, bool bLogErrors)
{
	InContext.SetLinkedStateTreeOverrides(LinkedStateTreeOverrides);
	InContext.SetCollectExternalDataCallback(FOnCollectStateTreeExternalData::CreateUObject(
		this, &UFollowerStateTreeComponent::CollectExternalData));

	// (A) Follower Context
	FStateTreeDataView ContextView(
		FFollowerStateTreeContext::StaticStruct(),
		reinterpret_cast<uint8*>(&Context)
	);
	if (!InContext.SetContextDataByName(FName(TEXT("FollowerContext")), ContextView))
	{
		if (bLogErrors) UE_LOG(LogTemp, Error, TEXT("  ❌ Failed to set FollowerContext"));
	}

	// (B) Follower Component
	if (!InContext.SetContextDataByName(FName(TEXT("FollowerComponent")), FStateTreeDataView(FollowerComponent)))
	{
		if (bLogErrors) UE_LOG(LogTemp, Error, TEXT("  ❌ Failed to set FollowerComponent"));
	}

	// (C) Follower State Tree Component
	if (!InContext.SetContextDataByName(FName(TEXT("FollowerStateTreeComponent")), FStateTreeDataView(this)))
	{
		if (bLogErrors) UE_LOG(LogTemp, Error, TEXT("  ❌ Failed to set FollowerStateTreeComponent"));
	}


	// (D) Team Leader (Optional)
	if (Context.TeamLeader)
	{
		InContext.SetContextDataByName(FName(TEXT("TeamLeader")), FStateTreeDataView(Context.TeamLeader));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("  ⚠️ TeamLeader is NULL (optional)"));
	}

	// (E) Tactical Policy (Optional)
	if (Context.TacticalPolicy)
	{
		InContext.SetContextDataByName(FName(TEXT("TacticalPolicy")), FStateTreeDataView(Context.TacticalPolicy));
	}
	// Note: TacticalPolicy is optional - no log needed for NULL case

	// Use our custom schema's SetContextRequirements which makes AIController optional for Schola
	const bool bResult = UFollowerStateTreeSchema::SetContextRequirements(*this, InContext, true);

	if (!bResult)
	{
		UE_LOG(LogTemp, Error, TEXT("🔵 UFollowerStateTreeComponent::SetContextRequirements FAILED"));
	}


	return bResult;
}

TValueOrError<void, FString> UFollowerStateTreeComponent::HasValidStateTreeReference() const
{
	if (!StateTreeRef.IsValid())
	{
		return MakeError(TEXT("The State Tree asset is not set."));
	}

	const UStateTree* StateTree = StateTreeRef.GetStateTree();
	if (!StateTree)
	{
		return MakeError(TEXT("The State Tree reference is invalid."));
	}

	const UStateTreeSchema* Schema = StateTree->GetSchema();
	if (!Schema)
	{
		return MakeError(TEXT("The State Tree schema is not set."));
	}

	if (!Schema->GetClass()->IsChildOf(UFollowerStateTreeSchema::StaticClass()))
	{
		return MakeError(FString::Printf(
			TEXT("The State Tree schema is not compatible. Expected FollowerStateTreeSchema or child class, but got %s."),
			*Schema->GetClass()->GetName()
		));
	}

	return MakeValue();

}

void UFollowerStateTreeComponent::ValidateStateTreeReference()
{
	const TValueOrError<void, FString> Result = HasValidStateTreeReference();
	if (Result.HasError())
	{
		UE_LOG(LogTemp, Error, TEXT("UFollowerStateTreeComponent::ValidateStateTreeReference: %s Cannot initialize."),
			*Result.GetError());
	}
}

void UFollowerStateTreeComponent::InitializeContext()
{
	if (!FollowerComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("UFollowerStateTreeComponent::InitializeContext: FollowerComponent not found on '%s'!"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"));
		return;
	}

	// Set context component reference
	Context.FollowerComponent = FollowerComponent;

	// Auto-find Pawn and AIController
	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		Context.ControlledPawn = OwnerPawn;

		if (!Context.AIController)
		{
			Context.AIController = Cast<AAIController>(OwnerPawn->GetController());
		}
	}

	// Set component references (v9.0: Use TeamCommsComponent)
	if (!Context.TeamLeader && TeamCommsComponent)
	{
		Context.TeamLeader = TeamCommsComponent->GetTeamLeader();
		UE_LOG(LogTemp, Log, TEXT("UFollowerStateTreeComponent: TeamLeader set to '%s'"),
			Context.TeamLeader ? *Context.TeamLeader->GetName() : TEXT("NULL"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("UFollowerStateTreeComponent: TeamLeader already set to '%s'"),
			Context.TeamLeader ? *Context.TeamLeader->GetName() : TEXT("NULL"));
	}

	if (!Context.TacticalPolicy && Context.FollowerComponent)
	{
		Context.TacticalPolicy = Context.FollowerComponent->GetTacticalPolicy();
	}

	// Initialize state flags
	Context.bIsAlive = HealthComponent.Get()->bIsAlive;
	Context.bUseRLPolicy = FollowerComponent->IsUsingRLPolicy();

	// Initialize observation
	Context.CurrentObservation = FollowerComponent->GetLocalObservation();

	UE_LOG(LogTemp, Log, TEXT("UFollowerStateTreeComponent: Initialized context for '%s' - ControlledPawn: %s, AIController: %s"),
		*GetOwner()->GetName(),
		Context.ControlledPawn ? *Context.ControlledPawn->GetName() : TEXT("NULL"),
		Context.AIController ? *Context.AIController->GetName() : TEXT("NULL"));
}

void UFollowerStateTreeComponent::UpdateContextFromFollower()
{
	if (!FollowerComponent)
	{
		return;
	}

	// Sync basic state from follower component (v3.0)
	// (Detailed observation updates are handled by STEvaluator_UpdateObservation)
	Context.bIsAlive = HealthComponent->bIsAlive;
	Context.AccumulatedReward = FollowerComponent->GetAccumulatedReward();

	// v9.0: Sync strategy assignment and compute implicit objective from team leader
	// Objectives are no longer explicitly assigned - they're implicit in the strategy:
	// - Assault/Support → Hostile objective
	// - Defend → Friendly objective
	// - Retreat → No specific objective
	Context.AssignedStrategy = FollowerComponent->GetAssignedStrategy();

	// v9.0: Compute implicit target objective based on strategy (use TeamCommsComponent)
	UTeamLeaderComponent* TeamLeader = TeamCommsComponent ? TeamCommsComponent->GetTeamLeader() : nullptr;
	if (TeamLeader)
	{
		if (Context.AssignedStrategy == EStrategyType::Assault ||
		    Context.AssignedStrategy == EStrategyType::Support)
		{
			Context.TargetObjective = TeamLeader->GetHostileObjective();
		}
		else if (Context.AssignedStrategy == EStrategyType::Defend)
		{
			Context.TargetObjective = TeamLeader->GetFriendlyObjective();
		}
		else // Retreat
		{
			Context.TargetObjective = nullptr;
		}
	}
	else
	{
		Context.TargetObjective = nullptr;
	}
}

bool UFollowerStateTreeComponent::IsStateTreeRunning() const
{
	return GetStateTreeRunStatus() == EStateTreeRunStatus::Running;
}

FString UFollowerStateTreeComponent::GetCurrentStateName() const
{
	// Get run status
	EStateTreeRunStatus Status = GetStateTreeRunStatus();

	switch (Status)
	{
		case EStateTreeRunStatus::Running:
			return TEXT("Running");
		case EStateTreeRunStatus::Succeeded:
			return TEXT("Succeeded");
		case EStateTreeRunStatus::Failed:
			return TEXT("Failed");
		case EStateTreeRunStatus::Stopped:
			return TEXT("Stopped");
		case EStateTreeRunStatus::Unset:
		default:
			return TEXT("Not Running");
	}

	// Note: In UE5.6, accessing individual state names requires accessing the execution context
	// during callbacks (EnterState, Tick, etc.). Direct access from component is not supported.
	// For detailed state information, use debug visualization or StateTree logging.
}

bool UFollowerStateTreeComponent::CollectExternalData(const FStateTreeExecutionContext& InContext,
	const UStateTree* StateTree, TArrayView<const FStateTreeExternalDataDesc> ExternalDataDescs,
	TArrayView<FStateTreeDataView> OutDataViews) const
{
	UE_LOG(LogTemp, Error, TEXT("🔥🔥🔥 CollectExternalData CALLED 🔥🔥🔥"));
    UE_LOG(LogTemp, Warning, TEXT("🔍 Collecting external data for %d descriptors"), 
        ExternalDataDescs.Num());
    
    // Get owner references (const-safe)
    const APawn* OwnerPawn = Cast<APawn>(GetOwner());
    const AAIController* AIController = OwnerPawn ? Cast<AAIController>(OwnerPawn->GetController()) : nullptr;

    int32 RequiredCount = 0;
    int32 ProvidedCount = 0;

    for (int32 Index = 0; Index < ExternalDataDescs.Num(); Index++)
    {
        const FStateTreeExternalDataDesc& Desc = ExternalDataDescs[Index];
        
        if (Desc.Requirement == EStateTreeExternalDataRequirement::Required)
        {
            RequiredCount++;
        }

        bool bProvided = false;

        // Handle base class descriptors
        if (Desc.Struct && Desc.Struct->IsChildOf(AAIController::StaticClass()))
        {
            if (AIController)
            {
                OutDataViews[Index] = FStateTreeDataView(const_cast<AAIController*>(AIController));
                bProvided = true;
                UE_LOG(LogTemp, Log, TEXT("  ✅ [%d] AIController provided"), Index);
            }
            else
            {
                // AIController is now optional (Schola training mode)
                OutDataViews[Index] = FStateTreeDataView();
                if (Desc.Requirement == EStateTreeExternalDataRequirement::Required)
                {
                    UE_LOG(LogTemp, Error, TEXT("  ❌ [%d] AIController REQUIRED but NULL"), Index);
                }
                else
                {
                    UE_LOG(LogTemp, Log, TEXT("  ⚠️ [%d] AIController optional and NULL (Schola mode)"), Index);
                    bProvided = true; // Count as provided for optional items
                }
            }
        }
        else if (Desc.Struct && Desc.Struct->IsChildOf(APawn::StaticClass()))
        {
            if (OwnerPawn)
            {
                OutDataViews[Index] = FStateTreeDataView(const_cast<APawn*>(OwnerPawn));
                bProvided = true;
                UE_LOG(LogTemp, Log, TEXT("  ✅ [%d] Pawn provided"), Index);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("  ❌ [%d] Pawn REQUIRED but NULL"), Index);
            }
        }
        else if (Desc.Struct && Desc.Struct->IsChildOf(UStateTreeComponent::StaticClass()))
        {
            // Provide this component itself
            OutDataViews[Index] = FStateTreeDataView(const_cast<UFollowerStateTreeComponent*>(this));
            bProvided = true;
            UE_LOG(LogTemp, Log, TEXT("  ✅ [%d] StateTreeComponent (self) provided"), Index);
        }
        else if (Desc.Name == FName(TEXT("FollowerComponent")))
        {
            if (FollowerComponent)
            {
                OutDataViews[Index] = FStateTreeDataView(FollowerComponent);
                bProvided = true;
                UE_LOG(LogTemp, Log, TEXT("  ✅ [%d] FollowerComponent provided"), Index);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("  ❌ [%d] FollowerComponent REQUIRED but NULL"), Index);
            }
        }
        else if (Desc.Name == FName(TEXT("FollowerContext")))
        {
            // Provide the context struct - const_cast is safe here as StateTree needs mutable access
            OutDataViews[Index] = FStateTreeDataView(
                FFollowerStateTreeContext::StaticStruct(),
                reinterpret_cast<uint8*>(const_cast<FFollowerStateTreeContext*>(&Context))
            );
            bProvided = true;
            UE_LOG(LogTemp, Log, TEXT("  ✅ [%d] FollowerContext struct provided"), Index);
        }
        else if (Desc.Name == FName(TEXT("TeamLeader")))
        {
            // v9.0: Get TeamLeader via TeamCommsComponent
            UTeamLeaderComponent* TeamLeader = nullptr;
            if (TeamCommsComponent)
            {
                TeamLeader = TeamCommsComponent->GetTeamLeader();
            }

            OutDataViews[Index] = FStateTreeDataView(TeamLeader);
            bProvided = true;
            UE_LOG(LogTemp, Log, TEXT("  ✅ [%d] TeamLeader: %s"), Index,
                TeamLeader ? TEXT("Valid") : TEXT("NULL (Optional)"));
        }
        else if (Desc.Name == FName(TEXT("TacticalPolicy")))
        {
            // Same issue - need to cache this separately
            URLPolicyNetwork* CachedPolicy = nullptr;
            if (FollowerComponent)
            {
                CachedPolicy = FollowerComponent->GetTacticalPolicy(); // Implement this getter
            }
            
            OutDataViews[Index] = FStateTreeDataView(CachedPolicy);
            bProvided = true;
            UE_LOG(LogTemp, Log, TEXT("  ✅ [%d] TacticalPolicy: %s"), Index,
                CachedPolicy ? TEXT("Valid") : TEXT("NULL (Optional)"));
        }

        if (!bProvided)
        {
            OutDataViews[Index] = FStateTreeDataView();
            
            if (Desc.Requirement == EStateTreeExternalDataRequirement::Required)
            {
                UE_LOG(LogTemp, Error, TEXT("  ❌ [%d] REQUIRED data missing: '%s' (Type: %s)"), 
                    Index, *Desc.Name.ToString(), 
                    Desc.Struct ? *Desc.Struct->GetName() : TEXT("NULL"));
                return false;
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("  ⚠️ [%d] Optional data not provided: '%s'"), 
                    Index, *Desc.Name.ToString());
            }
        }
        else if (Desc.Requirement == EStateTreeExternalDataRequirement::Required)
        {
            ProvidedCount++;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("🔍 CollectExternalData COMPLETE - %d/%d required items provided"), 
        ProvidedCount, RequiredCount);
    
    return ProvidedCount >= RequiredCount;
}



UFollowerAgentComponent* UFollowerStateTreeComponent::FindFollowerComponent()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	UFollowerAgentComponent* OwnerFollowerComp = Owner->FindComponentByClass<UFollowerAgentComponent>();

	if (!OwnerFollowerComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("UFollowerStateTreeComponent: No FollowerAgentComponent found on '%s'"), *Owner->GetName());
	}

	return OwnerFollowerComp;
}

UHealthComponent* UFollowerStateTreeComponent::FindHealthComponent()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	UHealthComponent* OwnerHealthComp = Owner->FindComponentByClass<UHealthComponent>();

	if (!OwnerHealthComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("UFollowerStateTreeComponent: No HealthComponent found on '%s'"), *Owner->GetName());
	}

	return OwnerHealthComp;
}

UTeamCommsComponent* UFollowerStateTreeComponent::FindTeamCommsComponent()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	UTeamCommsComponent* OwnerTeamCommsComp = Owner->FindComponentByClass<UTeamCommsComponent>();

	if (!OwnerTeamCommsComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("UFollowerStateTreeComponent: No TeamCommsComponent found on '%s'"), *Owner->GetName());
	}

	return OwnerTeamCommsComp;
}

void UFollowerStateTreeComponent::BindToFollowerEvents()
{
	if (!FollowerComponent)
	{
		return;
	}

	// Bind to HealthComponent death event (v9.0 - Dead state support)
	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &UFollowerStateTreeComponent::HandleOnHealthComponentDeath);
		UE_LOG(LogTemp, Log, TEXT("UFollowerStateTreeComponent: Bound to HealthComponent::OnDeath"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("UFollowerStateTreeComponent: HealthComponent not found - death events will not work!"));
	}

	UE_LOG(LogTemp, Log, TEXT("UFollowerStateTreeComponent: Bound to FollowerAgentComponent events"));
}

void UFollowerStateTreeComponent::HandleOnHealthComponentDeath(const FDeathEventData& DeathEvent)
{
	// v9.0: Called when HealthComponent broadcasts OnDeath event
	// Delegates to OnFollowerDied() which updates context and sends StateTree event

	AActor* Owner = GetOwner();
	UE_LOG(LogTemp, Warning, TEXT("💀 [DEATH EVENT] %s died (Killer: %s, Damage: %.1f)"),
		Owner ? *Owner->GetName() : TEXT("NULL"),
		DeathEvent.Killer ? *DeathEvent.Killer->GetName() : TEXT("NULL"),
		DeathEvent.FinalDamage);

	OnFollowerDied();
}

void UFollowerStateTreeComponent::OnFollowerDied()
{
	Context.bIsAlive = false;

	UE_LOG(LogTemp, Log, TEXT("UFollowerStateTreeComponent: Follower died, transitioning to Dead state"));

	// Send StateTree event for event-driven transition
	SendStateTreeEvent(Event_FollowerDied);
}

void UFollowerStateTreeComponent::OnFollowerRespawned()
{
	// Called when agent respawns after death or episode reset
	Context.bIsAlive = true;
	AActor* Owner = GetOwner();

	// Stop StateTree if running (clears dead state)
	if (IsStateTreeRunning())
	{
		StopLogic("Respawn");
		UE_LOG(LogTemp, Log, TEXT("  → StateTree stopped to clear dead state"));
	}

	// CRITICAL FIX: Check if World is valid before accessing TimerManager
	UWorld* World = GetWorld();
	if (!World || !World->IsValidLowLevel())
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ Follower '%s': World not ready during respawn, skipping StateTree restart"), *GetNameSafe(Owner));
		return;
	}

	// Restart StateTree on next tick (allows proper re-initialization)
	World->GetTimerManager().SetTimerForNextTick([WeakThis = TWeakObjectPtr<UFollowerStateTreeComponent>(this)]()
		{
			UFollowerStateTreeComponent* Comp = WeakThis.Get();
			if (!Comp) return;

			Comp->StartLogic();
			UE_LOG(LogTemp, Warning, TEXT("✅ StateTree restarted for '%s'"), *GetNameSafe(Comp->GetOwner()));

			// NOTE: Event_FollowerRespawned event is not used in current StateTree asset
			// (StateTree uses IsAlive condition checks instead of event-driven transitions)
			// Keeping event send for potential future use or custom StateTree assets
			Comp->SendStateTreeEvent(Event_FollowerRespawned);

			// Re-enable movement component (may be disabled during death)
			if (ACharacter* Character = Cast<ACharacter>(Comp->GetOwner()))
			{
				UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
				if (MoveComp && !MoveComp->IsActive())
				{
					MoveComp->Activate();
					UE_LOG(LogTemp, Warning, TEXT("  → Re-activated CharacterMovementComponent"));
				}
			}
		});
}

bool UFollowerStateTreeComponent::CheckRequirementsAndStart()
{
	AActor* Owner = GetOwner();
	FString OwnerName = Owner ? Owner->GetName() : TEXT("NULL_OWNER");

	// Already running - no need to start again
	if (IsStateTreeRunning())
	{
		UE_LOG(LogTemp, Warning, TEXT("  UFollowerStateTreeComponent:✅ StateTree already running for '%s'"), *OwnerName);
		return true;
	}

	// FIX (Issue #2): Only require FollowerComponent, make AIController truly optional
	// StateTree should start immediately without waiting for AIController assignment
	if (!FollowerComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("  UFollowerStateTreeComponent:⏳ FollowerComponent = NULL for '%s'"), *OwnerName);
		return false;
	}

	// Update AIController reference if available (but don't block on it)
	if (!Context.AIController)
	{
		APawn* OwnerPawn = Cast<APawn>(Owner);
		if (OwnerPawn)
		{
			Context.AIController = Cast<AAIController>(OwnerPawn->GetController());
		}
	}

	// If StateTree was previously stopped, ensure it's fully reset before restarting
	EStateTreeRunStatus CurrentStatus = GetStateTreeRunStatus();
	if (CurrentStatus == EStateTreeRunStatus::Stopped || CurrentStatus == EStateTreeRunStatus::Failed)
	{
		UE_LOG(LogTemp, Log, TEXT("  UFollowerStateTreeComponent: StateTree in %s state, stopping before restart"),
			*UEnum::GetValueAsString(CurrentStatus));
		StopLogic("Restart");
	}

	// FIX (Issue #2): Start immediately - AIController will be updated later when available
	// This ensures all agents start simultaneously instead of one-by-one
	UE_LOG(LogTemp, Warning, TEXT("  UFollowerStateTreeComponent:🚀 Starting StateTree immediately (AIController=%s)"),
		Context.AIController ? TEXT("Valid") : TEXT("NULL - will update when assigned"));
	StartLogic();

	return IsStateTreeRunning();
}

void UFollowerStateTreeComponent::HandleOnStateTreeRunStatusChanged(const EStateTreeRunStatus Status)
{
	FString StatusStr = UEnum::GetValueAsString(Status);
	UE_LOG(LogTemp, Warning, TEXT("UFollowerStateTreeComponent: ⚠️ [StatusChanged] '%s' changed to: %s"),
		*GetNameSafe(GetOwner()), *StatusStr);

	if (Status == EStateTreeRunStatus::Failed)
	{
		UE_LOG(LogTemp, Error, TEXT("UFollowerStateTreeComponent: ❌ StateTree Failed! Check the active State/Task requirements."));
	}
	else if (Status == EStateTreeRunStatus::Succeeded)
	{
		UE_LOG(LogTemp, Warning, TEXT("UFollowerStateTreeComponent: ✅ StateTree Succeeded (Finished). Logic stopped."));
	}
}

void UFollowerStateTreeComponent::SendStateTreeEvent(const FGameplayTag& EventTag, FConstStructView Payload)
{
	if (!IsStateTreeRunning())
	{
		UE_LOG(LogTemp, Warning, TEXT("UFollowerStateTreeComponent: Cannot send event '%s' - StateTree not running"),
			*EventTag.ToString());
		return;
	}

	// UE 5.6: Validate tag before sending to prevent "invalid tag and payload" errors
	if (!EventTag.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("UFollowerStateTreeComponent: Cannot send event - Invalid GameplayTag! Tag='%s'"),
			*EventTag.ToString());
		return;
	}

	FStateTreeEvent Event(EventTag, Payload, FName(TEXT("FollowerComponent")));

	// 부모 클래스(UStateTreeComponent)의 SendStateTreeEvent 호출
	Super::SendStateTreeEvent(Event);

	UE_LOG(LogTemp, Log, TEXT("UFollowerStateTreeComponent: Event sent - '%s'"), *EventTag.ToString());
}

void UFollowerStateTreeComponent::OnControllerChanged(APawn* InPawn, AController* OldController, AController* NewController)
{
	AActor* Owner = GetOwner();
	UE_LOG(LogTemp, Warning, TEXT("🔄 [StateTree] Controller changed on '%s': %s → %s"),
		*GetNameSafe(Owner),
		*GetNameSafe(OldController),
		*GetNameSafe(NewController));

	// Update AIController reference in context
	Context.AIController = Cast<AAIController>(NewController);

	// If StateTree is running, it might stop due to controller change
	// Schedule a restart on next frame to ensure smooth transition
	if (NewController)
	{
		UWorld* World = GetWorld();
		if (World && World->IsValidLowLevel())
		{
			// Stop current StateTree if running (clears stale state)
			if (IsStateTreeRunning())
			{
				StopLogic("Controller Changed");
				UE_LOG(LogTemp, Log, TEXT("  → StateTree stopped to clear stale controller state"));
			}

			// Restart StateTree on next tick (allows new controller to settle)
			World->GetTimerManager().SetTimerForNextTick([WeakThis = TWeakObjectPtr<UFollowerStateTreeComponent>(this)]()
			{
				UFollowerStateTreeComponent* Comp = WeakThis.Get();
				if (!Comp) return;

				// Update context with new controller
				if (APawn* OwnerPawn = Cast<APawn>(Comp->GetOwner()))
				{
					Comp->Context.AIController = Cast<AAIController>(OwnerPawn->GetController());
				}

				// Restart StateTree
				Comp->CheckRequirementsAndStart();

				if (Comp->IsStateTreeRunning())
				{
					UE_LOG(LogTemp, Warning, TEXT("  ✅ StateTree restarted after controller change for '%s'"),
						*GetNameSafe(Comp->GetOwner()));
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("  ❌ StateTree failed to restart after controller change for '%s'"),
						*GetNameSafe(Comp->GetOwner()));
				}
			});
		}
	}
}
