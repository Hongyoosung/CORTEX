// ScholaAgentComponent.cpp - Schola agent component implementation

#include "Schola/ScholaAgentComponent.h"
#include "Schola/Observers/TacticalObserver.h"
#include "Schola/Rewards/TacticalRewardProvider.h"
#include "Schola/Actuators/CombinedTacticalActuator.h"
#include "Actor/FollowerCharacter.h"
#include "Inference/InferenceComponent.h"
#include "Combat/Components/HealthComponent.h"
#include "GameFramework/Pawn.h"
#include "Core/SimulationManagerGameMode.h"
#include "Kismet/GameplayStatics.h"

UScholaAgentComponent::UScholaAgentComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TacticalObserver(nullptr)
	, RewardProvider(nullptr)
	, CombinedTacticalActuator(nullptr)
	, FollowerAgent(nullptr)
	, ScholaEnvironment(nullptr)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}


void UScholaAgentComponent::BeginPlay()
{
	Super::BeginPlay();


	// Note: gRPC server is now managed by ScholaCombatEnvironment
	// This component will be auto-registered by the environment during initialization
	UE_LOG(LogTemp, Warning, TEXT("[ScholaAgent]: Initialized"));
}


void UScholaAgentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	FollowerAgent = nullptr;
}


void UScholaAgentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}


void UScholaAgentComponent::InitializeScholaComponents()
{
	// Find follower agent component
	FindFollowerAgent();
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

	TacticalObserver->SetFollowerAgent(FollowerAgent);
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

	RewardProvider->Initialize(FollowerAgent);

	UE_LOG(LogTemp, Log, TEXT("[ScholaAgent] %s: RewardProvider configured"),
		*GetOwner()->GetName());
}

void UScholaAgentComponent::FindFollowerAgent()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	AFollowerCharacter* Follower = Cast<AFollowerCharacter>(Owner);

	if (!Follower)
	{
		return;
	}


	FollowerAgent = Follower;
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
		CombinedTacticalActuator->SetFollowerAgent(FollowerAgent);
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
}
