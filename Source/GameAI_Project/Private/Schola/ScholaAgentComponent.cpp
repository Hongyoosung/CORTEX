// ScholaAgentComponent.cpp - Schola agent component implementation

#include "Schola/ScholaAgentComponent.h"
#include "Schola/TacticalObserver.h"
#include "Schola/TacticalRewardProvider.h"
#include "Schola/TacticalActuator.h"
#include "Team/FollowerAgentComponent.h"
#include "Inference/InferenceComponent.h"
#include "GameFramework/Pawn.h"

UScholaAgentComponent::UScholaAgentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// CRITICAL FIX: Hide CDO from TObjectIterator scans
	// Schola's base class discovers components via TObjectIterator, which includes CDOs
	// This causes a 5-component discovery (4 agents + 1 CDO) vs 4-entry id_manager mismatch
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		// Disable ticking on CDO (should never execute anyway)
		PrimaryComponentTick.bCanEverTick = false;
		PrimaryComponentTick.bStartWithTickEnabled = false;

		// Mark as transient to discourage serialization/discovery
		SetFlags(RF_Transient);

		// DO NOT create subobjects for CDO (reduces memory footprint)
		return;
	}

	// Create default subobjects for Schola components (only for real instances)
	TacticalObserver = CreateDefaultSubobject<UTacticalObserver>(TEXT("TacticalObserver"));
	RewardProvider = CreateDefaultSubobject<UTacticalRewardProvider>(TEXT("RewardProvider"));
	TacticalActuator = CreateDefaultSubobject<UTacticalActuator>(TEXT("TacticalActuator"));
}

void UScholaAgentComponent::BeginPlay()
{
	Super::BeginPlay();

	// CRITICAL: Do not initialize CDOs (Class Default Objects)
	// CDOs are template objects and should never participate in gameplay or training
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaAgent] Skipping BeginPlay for CDO/Archetype: %s"), *GetName());
		return;
	}

	// Also check owner
	AActor* Owner = GetOwner();
	if (!Owner || Owner->HasAnyFlags(RF_ClassDefaultObject))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaAgent] Skipping BeginPlay for component with invalid/CDO owner: %s"), *GetName());
		return;
	}

	// Auto-configure follower agent if enabled
	if (bAutoConfigureFollower)
	{
		InitializeScholaComponents();
	}

	// CRITICAL: Configure Brain for time-based decisions
	if (bEnableTimeBasedDecisions && Brain)
	{
		// Disable frame-based gating by setting DecisionRequestFrequency=1
		// Our Think() override handles time-based gating instead
		Brain->DecisionRequestFrequency = 1;

		UE_LOG(LogTemp, Log, TEXT("[ScholaAgent] %s: Time-based decisions enabled (%.3fs interval = %.1f Hz)"),
			*Owner->GetName(), DecisionInterval, 1.0f / DecisionInterval);
	}

	// Note: gRPC server is now managed by ScholaCombatEnvironment
	// This component will be auto-registered by the environment during initialization

	UE_LOG(LogTemp, Log, TEXT("[ScholaAgent] %s: Initialized"),
		*Owner->GetName());
}

void UScholaAgentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	FollowerAgent = nullptr;
}

void UScholaAgentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Store DeltaTime for time-based decision throttling
	// This is used by Think() which doesn't receive DeltaTime directly
	if (bEnableTimeBasedDecisions)
	{
		TimeSinceLastDecision += DeltaTime;
	}
}

void UScholaAgentComponent::InitializeScholaComponents()
{
	// Find follower agent component
	FollowerAgent = FindFollowerAgent();
	if (!FollowerAgent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaAgent] %s: FollowerAgentComponent not found!"),
			*GetOwner()->GetName());
		return;
	}

	// Configure components
	ConfigureObservers();
	ConfigureRewardProvider();
	ConfigureActuators();

	UE_LOG(LogTemp, Log, TEXT("[ScholaAgent] %s: Schola components configured successfully"),
		*GetOwner()->GetName());
}

void UScholaAgentComponent::ConfigureObservers()
{
	if (!TacticalObserver || !FollowerAgent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaAgent]: TacticalObserver or FollowerAgent is null!"));
		return;
	}

	// Link observer to follower agent
	TacticalObserver->FollowerAgent = FollowerAgent;
	TacticalObserver->bAutoFindFollower = false;
	TacticalObserver->InitializeObserver();

	// Add to InferenceComponent's observers array if not already present (this class IS the InferenceComponent)
	if (!this->Observers.Contains(TacticalObserver))
	{
		this->Observers.Add(TacticalObserver);
	}

	UE_LOG(LogTemp, Warning, TEXT("[ScholaAgent] %s: TacticalObserver configured (71 features)"),
		*GetOwner()->GetName());
}

void UScholaAgentComponent::ConfigureRewardProvider()
{
	if (!RewardProvider || !FollowerAgent)
	{
		return;
	}

	// Link reward provider to follower agent
	RewardProvider->FollowerAgent = FollowerAgent;
	RewardProvider->bAutoFindFollower = false;
	RewardProvider->Initialize();

	UE_LOG(LogTemp, Warning, TEXT("[ScholaAgent] %s: RewardProvider configured"),
		*GetOwner()->GetName());
}

UFollowerAgentComponent* UScholaAgentComponent::FindFollowerAgent() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	return Owner->FindComponentByClass<UFollowerAgentComponent>();
}

float UScholaAgentComponent::GetCurrentReward() const
{
	if (!RewardProvider)
	{
		return 0.0f;
	}

	return RewardProvider->GetReward();
}

bool UScholaAgentComponent::IsEpisodeTerminated() const
{
	if (!RewardProvider)
	{
		return false;
	}

	return RewardProvider->IsTerminated();
}

void UScholaAgentComponent::ConfigureActuators()
{
	if (!TacticalActuator || !FollowerAgent)
	{
		return;
	}

	// Link actuator to follower agent
	TacticalActuator->FollowerAgent = FollowerAgent;
	TacticalActuator->bAutoFindFollower = false;
	TacticalActuator->InitializeActuator();

	// Add to InferenceComponent's actuators array if not already present (this class IS the InferenceComponent)
	if (!this->Actuators.Contains(TacticalActuator))
	{
		this->Actuators.Add(TacticalActuator);
	}

	UE_LOG(LogTemp, Warning, TEXT("[ScholaAgent] %s: TacticalActuator configured (8D actions)"),
		*GetOwner()->GetName());
}

void UScholaAgentComponent::ResetEpisode()
{
	// Reset reward provider
	if (RewardProvider)
	{
		RewardProvider->Reset();
	}

	// Reset observer
	if (TacticalObserver)
	{
		TacticalObserver->ResetObserver();
	}

	// Reset follower agent episode
	if (FollowerAgent)
	{
		FollowerAgent->ResetEpisode();
	}

	// Reset decision timer
	TimeSinceLastDecision = 0.0f;

	UE_LOG(LogTemp, Verbose, TEXT("[ScholaAgent] %s: Episode reset"),
		*GetOwner()->GetName());
}

void UScholaAgentComponent::Think()
{
	// CRITICAL FIX: Time-based decision throttling for FPS-independent training
	//
	// Problem: Schola's default Think() uses frame-based DecisionRequestFrequency:
	//   - 60 FPS × DecisionRequestFrequency=5 → 12 decisions/sec
	//   - 10 FPS × DecisionRequestFrequency=5 → 2 decisions/sec
	//
	// Solution: Gate decisions by TIME instead of FRAME COUNT
	//   - Decision every 0.05s = 20 Hz (consistent at any FPS)

	if (bEnableTimeBasedDecisions)
	{
		// Check if enough time has passed since last decision
		bool bShouldDecide = (TimeSinceLastDecision >= DecisionInterval);

		if (!bShouldDecide)
		{
			// Skip this Think() call - not time for a new decision yet
			return;
		}

		// Reset timer for next decision interval
		TimeSinceLastDecision = 0.0f;
	}

	// Call parent's Think() which handles the actual decision request
	// This will always return true for IsDecisionStep() since we're gating here
	Super::Think();
}
