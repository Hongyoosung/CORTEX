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
	// CRITICAL FIX: Force-enable tick BEFORE Super::BeginPlay()
	// Must happen early because parent class might disable it
	if (bEnableTimeBasedDecisions)
	{
		PrimaryComponentTick.bCanEverTick = true;
		PrimaryComponentTick.bStartWithTickEnabled = true;
		PrimaryComponentTick.TickInterval = 0.0f;  // Tick every frame
		SetComponentTickEnabled(true);
	}

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
		// IMPORTANT: Set DecisionRequestFrequency to a very high value to effectively disable frame-based gating
		// Our Think() override handles time-based gating instead using TimeSinceLastDecision accumulator
		// Setting to 1 would cause decisions every frame, which defeats the purpose
		Brain->DecisionRequestFrequency = 1000000;  // Effectively disable frame-based gating

		UE_LOG(LogTemp, Log, TEXT("[ScholaAgent] %s: Time-based decisions enabled (%.3fs interval = %.1f Hz)"),
			*Owner->GetName(), DecisionInterval, 1.0f / DecisionInterval);
	}

	// CRITICAL FIX: Re-enable tick AFTER Super::BeginPlay() in case parent disabled it
	if (bEnableTimeBasedDecisions)
	{
		PrimaryComponentTick.bCanEverTick = true;
		PrimaryComponentTick.bStartWithTickEnabled = true;
		PrimaryComponentTick.TickInterval = 0.0f;
		SetComponentTickEnabled(true);

		UE_LOG(LogTemp, Verbose, TEXT("[ScholaAgent] %s: Time-based decisions enabled (tick configured)"),
			*Owner->GetName());
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

	#if !UE_BUILD_SHIPPING
	// DIAGNOSTIC: Verify TickComponent is actually being called
	static int32 GlobalTickCounter = 0;
	GlobalTickCounter++;
	if (GlobalTickCounter == 1)
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaAgent] %s: TickComponent FIRST CALL! DeltaTime=%.4fs"),
			*GetOwner()->GetName(), DeltaTime);
	}
	#endif

	// Store DeltaTime for time-based decision throttling
	// This is used by Think() which doesn't receive DeltaTime directly
	if (bEnableTimeBasedDecisions)
	{
		TimeSinceLastDecision += DeltaTime;

		#if !UE_BUILD_SHIPPING
		// Diagnostic: Log tick accumulation every 60 frames
		static int32 TickCounter = 0;
		TickCounter++;
		if (TickCounter % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ScholaAgent] %s: TickComponent frame %d - DeltaTime=%.4fs, TimeSinceLastDecision=%.4fs (accumulated)"),
				*GetOwner()->GetName(), TickCounter, DeltaTime, TimeSinceLastDecision);
		}
		#endif
	}
	else
	{
		#if !UE_BUILD_SHIPPING
		static bool bLoggedTickDisabled = false;
		if (!bLoggedTickDisabled)
		{
			UE_LOG(LogTemp, Error, TEXT("[ScholaAgent] %s: TickComponent called but bEnableTimeBasedDecisions=FALSE!"),
				*GetOwner()->GetName());
			bLoggedTickDisabled = true;
		}
		#endif
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

	UE_LOG(LogTemp, Log, TEXT("[ScholaAgent] %s: TacticalObserver configured (71 features)"),
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

	UE_LOG(LogTemp, Log, TEXT("[ScholaAgent] %s: RewardProvider configured"),
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

	UE_LOG(LogTemp, Log, TEXT("[ScholaAgent] %s: TacticalActuator configured (7D actions)"),
		*GetOwner()->GetName());
}

void UScholaAgentComponent::ResetEpisode()
{
	// CRITICAL: Prevent multiple rapid resets (Schola may call this multiple times)
	// Only reset if at least 0.1s has passed since last reset
	double CurrentTime = FPlatformTime::Seconds();
	if (LastResetTime > 0.0 && (CurrentTime - LastResetTime) < 0.1)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[ScholaAgent] %s: Ignoring rapid reset (%.3fs since last)"),
			*GetOwner()->GetName(), CurrentTime - LastResetTime);
		return;
	}

	LastResetTime = CurrentTime;

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
	LastDecisionTime = 0.0;

	UE_LOG(LogTemp, Log, TEXT("[ScholaAgent] %s: Episode reset"),
		*GetOwner()->GetName());
}

void UScholaAgentComponent::Think()
{
	// SECONDARY SAFETY NET: Time-based decision throttling
	//
	// PRIMARY rate limiting is done in Python (sbdapm_env.py step() method)
	// This UE-side throttling provides defense-in-depth:
	//   - Prevents excessive gRPC requests if Python rate limiter fails
	//   - Protects against bugs in training script
	//
	// Note: This only throttles REQUESTS to Python, not received actions
	// Python step() sleep() enforces the actual action send rate (10 Hz)

	if (bEnableTimeBasedDecisions)
	{
		double CurrentTime = FPlatformTime::Seconds();

		// Initialize on first call
		if (LastDecisionTime == 0.0)
		{
			LastDecisionTime = CurrentTime;
		}
		else
		{
			double ElapsedTime = CurrentTime - LastDecisionTime;

			// Skip if too soon (secondary throttle)
			if (ElapsedTime < DecisionInterval)
			{
				return;
			}

			LastDecisionTime = CurrentTime;
		}
	}

	// Call parent's Think() which handles the actual decision request
	Super::Think();
}
