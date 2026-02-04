// ScholaAgentComponent.cpp - Schola agent component implementation

#include "Schola/ScholaAgentComponent.h"
#include "Schola/Observers/TacticalObserver.h"
#include "Schola/Rewards/AgentRewardManager.h"
#include "Schola/Actuators/CombinedTacticalActuator.h"
#include "Inference/InferenceComponent.h"
#include "Combat/Components/HealthComponent.h"
#include "GameFramework/Pawn.h"
#include "Core/SimulationManagerGameMode.h"
#include "Kismet/GameplayStatics.h"

UScholaAgentComponent::UScholaAgentComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TacticalObserver(nullptr)
	, AgentRewardManager(nullptr)
	, CombinedTacticalActuator(nullptr)
	, ScholaEnvironment(nullptr)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}


void UScholaAgentComponent::BeginPlay()
{
	Super::BeginPlay();

	AgentRewardManager = NewObject<UAgentRewardManager>();

	// Note: gRPC server is now managed by ScholaCombatEnvironment
	// This component will be auto-registered by the environment during initialization
	UE_LOG(LogTemp, Warning, TEXT("[ScholaAgent]: Initialized"));
}


void UScholaAgentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	OwnerAgent = nullptr;
}


void UScholaAgentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}


void UScholaAgentComponent::InitializeScholaComponents()
{
	// Configure components
	ConfigureObservers();
	ConfigureActuators();

	UE_LOG(LogTemp, Log, TEXT("[ScholaAgent] %s: Schola components configured successfully"),
		*GetOwner()->GetName());
}

void UScholaAgentComponent::ConfigureObservers()
{
	if (!TacticalObserver)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaAgent]: TacticalObserver or FollowerAgent is null!"));
		return;
	}

	TacticalObserver->SetFollowerAgent(GetOwner());
	TacticalObserver->InitializeObserver();

	// Add to InferenceComponent's observers array if not already present (this class IS the InferenceComponent)
	if (!this->Observers.Contains(TacticalObserver))
	{
		this->Observers.Add(TacticalObserver);
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaAgent] %s: TacticalObserver configured (71 features)"),
		*GetOwner()->GetName());
}


float UScholaAgentComponent::GetCurrentReward() const
{
	if (!AgentRewardManager)
	{
		return 0.0f;
	}

	return AgentRewardManager->GetReward();
}

void UScholaAgentComponent::ProviderReward(float Reward)
{
	if (!AgentRewardManager)
	{
		return;
	}

	AgentRewardManager->AccumulateReward(Reward);
}

// REMOVED: IsEpisodeTerminated() - Episode termination now handled by FollowerAgentTrainer.ComputeStatus()

void UScholaAgentComponent::ConfigureActuators()
{
	if (CombinedTacticalActuator)
	{
		CombinedTacticalActuator->SetFollowerAgent(GetOwner());
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
	if (AgentRewardManager)
	{
		AgentRewardManager->Reset();
	}

	// Reset observer
	if (TacticalObserver)
	{
		TacticalObserver->ResetObserver();
	}
}
