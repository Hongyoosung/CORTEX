// ScholaAgentComponent.cpp - Schola agent component implementation

#include "Schola/ScholaAgentComponent.h"
#include "Schola/TacticalObserver.h"
#include "Schola/TacticalRewardProvider.h"
#include "Schola/CombinedTacticalActuator.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Inference/InferenceComponent.h"
#include "Combat/Components/HealthComponent.h"
#include "Combat/Components/AgentPerceptionComponent.h"
#include "GameFramework/Pawn.h"
#include "Core/SimulationManagerGameMode.h"
#include "Kismet/GameplayStatics.h"

UScholaAgentComponent::UScholaAgentComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		// Disable ticking on CDO (should never execute anyway)
		PrimaryComponentTick.bCanEverTick = false;
		PrimaryComponentTick.bStartWithTickEnabled = false;

		// DO NOT create subobjects for CDO (reduces memory footprint)
		return;
	}
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

	// EVENT-DRIVEN DECISION BINDINGS (v4.0)
	// Bind to perception events for immediate response to critical game state changes
	UAgentPerceptionComponent* PerceptionComp = Owner->FindComponentByClass<UAgentPerceptionComponent>();
	if (PerceptionComp)
	{
		PerceptionComp->OnEnemySpotted.AddDynamic(this, &UScholaAgentComponent::OnEnemySpottedEvent);
		PerceptionComp->OnAllEnemiesLost.AddDynamic(this, &UScholaAgentComponent::OnAllEnemiesLostEvent);
		UE_LOG(LogTemp, Log, TEXT("[SCHOLA EVENT] '%s': Bound to perception events for event-driven decisions"),
			*Owner->GetName());
	}

	// Bind to health events for critical triggers (damaged, health low)
	HealthComponent = Owner->FindComponentByClass<UHealthComponent>();
	if (HealthComponent)
	{
		HealthComponent->OnDamageTaken.AddDynamic(this, &UScholaAgentComponent::OnDamageTakenEventHandler);
		UE_LOG(LogTemp, Log, TEXT("[SCHOLA EVENT] '%s': Bound to health events for event-driven decisions"),
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

// REMOVED: IsEpisodeTerminated() - Episode termination now handled by FollowerAgentTrainer.ComputeStatus()

void UScholaAgentComponent::ConfigureActuators()
{
	if (!FollowerAgent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaAgent]: FollowerAgent is null, cannot configure actuators!"));
		return;
	}

	// v8.0: Configure CombinedTacticalActuator (5 continuous values: 4 tactical + 1 combat priority)
	if (CombinedTacticalActuator)
	{
		CombinedTacticalActuator->FollowerAgent = FollowerAgent;
		CombinedTacticalActuator->bAutoFindFollower = false;
		CombinedTacticalActuator->InitializeActuator();

		if (!this->Actuators.Contains(CombinedTacticalActuator))
		{
			this->Actuators.Add(CombinedTacticalActuator);
		}

		UE_LOG(LogTemp, Log, TEXT("[ScholaAgent v8.0] %s: CombinedTacticalActuator configured (Box([0,1]^5))"),
			*GetOwner()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaAgent v8.0] %s: CombinedTacticalActuator is null!"),
			*GetOwner()->GetName());
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaAgent v8.0] %s: v8.0 action space configured (5 continuous)"),
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
}

void UScholaAgentComponent::Think()
{
	// v7.4: Time-based decision throttling
	// This is the PRIMARY rate limiter - controls when UE5 sends observations to Python
	// Python's poll() should block until we call Super::Think()

	if (bEnableTimeBasedDecisions)
	{
		double CurrentTime = FPlatformTime::Seconds();

		// Initialize on first call
		if (LastDecisionTime == 0.0)
		{
			LastDecisionTime = CurrentTime;
			UE_LOG(LogTemp, Warning, TEXT("[THINK v7.4] %s: Initialized (Interval=%.2fs = %.1f Hz)"),
				*GetOwner()->GetName(), DecisionInterval, 1.0f / DecisionInterval);
		}
		else
		{
			double ElapsedTime = CurrentTime - LastDecisionTime;

			// Skip if too soon - THIS IS THE KEY THROTTLE
			if (ElapsedTime < DecisionInterval)
			{
				// v7.4: Track rejection rate for diagnostics
				static TMap<FString, int32> ThrottleCounters;
				FString OwnerName = GetOwner()->GetName();
				int32& Counter = ThrottleCounters.FindOrAdd(OwnerName, 0);
				Counter++;

				// Log every 5 seconds worth of rejections (at 60 FPS = 300 rejections)
				if (Counter % 300 == 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("[THINK v7.4] %s: Throttled %d times (interval=%.2fs)"),
						*OwnerName, Counter, DecisionInterval);
				}
				return;  // DO NOT send observations to Python
			}

			// v7.4: Log when we actually send a new decision request
			static TMap<FString, int32> DecisionCounters;
			FString OwnerName = GetOwner()->GetName();
			int32& Counter = DecisionCounters.FindOrAdd(OwnerName, 0);
			Counter++;
			UE_LOG(LogTemp, Warning, TEXT("[THINK v7.4] ✅ %s: SENDING decision request #%d (elapsed=%.3fs)"),
				*OwnerName, Counter, ElapsedTime);

			LastDecisionTime = CurrentTime;
		}
	}

	// Call parent's Think() which sends observations to Python and waits for actions
	Super::Think();
}

//--------------------------------------------------------------------------
// EVENT HANDLERS (v4.0 Event-Driven Decisions)
//--------------------------------------------------------------------------

void UScholaAgentComponent::OnEnemySpottedEvent(AActor* Enemy)
{
	// Immediate action request when enemy spotted
	// Bypasses periodic timer for urgent tactical response
	UE_LOG(LogTemp, Warning, TEXT("🚨 [EVENT TRIGGER] '%s': Enemy '%s' spotted → Requesting immediate action from RLlib"),
		*GetOwner()->GetName(), *GetNameSafe(Enemy));

	// Reset decision timer so we don't double-request immediately after
	LastDecisionTime = FPlatformTime::Seconds();
}

void UScholaAgentComponent::OnAllEnemiesLostEvent()
{
	// Strategic decision when transitioning to non-combat
	UE_LOG(LogTemp, Log, TEXT("🔵 [EVENT TRIGGER] '%s': All enemies lost → Requesting strategic decision"),
		*GetOwner()->GetName());

	LastDecisionTime = FPlatformTime::Seconds();
}

void UScholaAgentComponent::OnDamageTakenEventHandler(const FDamageEventData& DamageEvent, float CurrentHealth)
{
	// Immediate response when taking damage
	// Only trigger if significant damage (>10 HP) to avoid spam
	if (DamageEvent.DamageAmount >= 10.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("💥 [EVENT TRIGGER] '%s': Took %.1f damage from '%s' (HP: %.1f) → Requesting immediate evasive action"),
			*GetOwner()->GetName(), DamageEvent.DamageAmount, *GetNameSafe(DamageEvent.DamageCauser), CurrentHealth);

		LastDecisionTime = FPlatformTime::Seconds();
	}
}
