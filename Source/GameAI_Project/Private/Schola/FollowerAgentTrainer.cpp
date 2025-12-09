// FollowerAgentTrainer.cpp - Trainer wrapper implementation

#include "Schola/FollowerAgentTrainer.h"
#include "Schola/ScholaAgentComponent.h"
#include "Schola/TacticalRewardProvider.h"
#include "Team/FollowerAgentComponent.h"
#include "Combat/HealthComponent.h"

AFollowerAgentTrainer::AFollowerAgentTrainer()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AFollowerAgentTrainer::Initialize(UScholaAgentComponent* InAgent)
{
	if (!InAgent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerTrainer] Initialize called with null agent"));
		return;
	}

	ScholaAgent = InAgent;
	FollowerAgent = InAgent->FollowerAgent;
	RewardProvider = InAgent->RewardProvider;

	// Verify components
	if (!FollowerAgent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerTrainer] InAgent->FollowerAgent is NULL! Agent won't work!"));
		return;
	}
	if (!RewardProvider)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerTrainer] InAgent->RewardProvider is NULL! Rewards won't work!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[FollowerTrainer] Components verified: FollowerAgent=%s, RewardProvider=%s, bIsAlive=%d"),
		*FollowerAgent->GetName(), *RewardProvider->GetName(), FollowerAgent->bIsAlive ? 1 : 0);

	// Get the controlled pawn first
	APawn* ControlledPawn = InAgent->GetControlledPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerTrainer] GetControlledPawn returned null!"));
		return;
	}

	// Possess the pawn BEFORE calling parent Initialize (required!)
	Possess(ControlledPawn);

	// Copy observers and actuators from ScholaAgentComponent to this trainer
	// This is required by Schola's architecture - MUST be done before parent Initialize
	Observers = ScholaAgent->Observers;
	Actuators = ScholaAgent->Actuators;

	// Use ScholaAgent's InteractionManager
	InteractionManager = ScholaAgent->InteractionManager;

	// Verify InteractionManager
	if (!InteractionManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerTrainer] InteractionManager is NULL! Observations/Actions won't work!"));
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("[FollowerTrainer] InteractionManager verified: %s"), *InteractionManager->GetName());

	// Set trainer configuration
	TrainerConfiguration.DecisionRequestFrequency = 1; // Every step (real-time RL)
	TrainerConfiguration.Name = FString::Printf(TEXT("Follower_%s"), *InAgent->GetOwner()->GetName());

	// Call parent class Initialize to register observation/action spaces with Schola
	// EnvId and AgentId will be set by the environment later, use dummy values for now
	bool bSuccess = AAbstractTrainer::Initialize(0, 0, ControlledPawn);
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerTrainer] Parent Initialize failed for %s"), *InAgent->GetOwner()->GetName());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[FollowerTrainer] Initialized for %s (Observers: %d, Actuators: %d)"),
		*InAgent->GetOwner()->GetName(), Observers.Num(), Actuators.Num());
}

//------------------------------------------------------------------------------
// ABSTRACT TRAINER INTERFACE
//------------------------------------------------------------------------------

float AFollowerAgentTrainer::ComputeReward()
{
	static int32 ComputeRewardCallCount = 0;
	ComputeRewardCallCount++;

	if (ComputeRewardCallCount % 100 == 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerTrainer] ComputeReward #%d called for %s"),
			ComputeRewardCallCount, *TrainerConfiguration.Name);
	}

	if (!RewardProvider)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerTrainer] ComputeReward: RewardProvider is NULL!"));
		return 0.0f;
	}

	// Get reward from TacticalRewardProvider
	float StepReward = RewardProvider->GetReward();
	EpisodeReward += StepReward;
	EpisodeSteps++; // Increment step counter

	return StepReward;
}

EAgentTrainingStatus AFollowerAgentTrainer::ComputeStatus()
{
	static int32 ComputeStatusCallCount = 0;
	ComputeStatusCallCount++;

	// Check if agent is dead
	bool bDead = IsAgentDead();
	bool bRewardTerminated = (RewardProvider && RewardProvider->IsTerminated());
	bool bTimeout = IsEpisodeTimeout();

	if (ComputeStatusCallCount % 100 == 1 || bDead || bRewardTerminated)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerTrainer] ComputeStatus #%d: Dead=%d, RewardTerminated=%d, Timeout=%d, Steps=%d"),
			ComputeStatusCallCount, bDead ? 1 : 0, bRewardTerminated ? 1 : 0, bTimeout ? 1 : 0, EpisodeSteps);

		if (!FollowerAgent)
		{
			UE_LOG(LogTemp, Error, TEXT("[FollowerTrainer] FollowerAgent is NULL!"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[FollowerTrainer] FollowerAgent->bIsAlive=%d"), FollowerAgent->bIsAlive ? 1 : 0);
		}
	}

	if (bDead)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerTrainer] %s - Agent died (Episode reward: %.2f, Steps: %d) → COMPLETED"),
			*TrainerConfiguration.Name, EpisodeReward, EpisodeSteps);
		return EAgentTrainingStatus::Completed;
	}

	if (bRewardTerminated)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerTrainer] %s - Episode terminated by RewardProvider (Episode reward: %.2f, Steps: %d) → COMPLETED"),
			*TrainerConfiguration.Name, EpisodeReward, EpisodeSteps);
		return EAgentTrainingStatus::Completed;
	}

	if (bTimeout)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerTrainer] %s - Episode timeout (Episode reward: %.2f, Steps: %d) → TRUNCATED"),
			*TrainerConfiguration.Name, EpisodeReward, EpisodeSteps);
		return EAgentTrainingStatus::Truncated;
	}

	// Still running
	return EAgentTrainingStatus::Running;
}

void AFollowerAgentTrainer::GetInfo(TMap<FString, FString>& Info)
{
	// Provide debug info for logging/monitoring
	Info.Add(TEXT("agent_name"), TrainerConfiguration.Name);
	Info.Add(TEXT("episode_reward"), FString::SanitizeFloat(EpisodeReward));
	Info.Add(TEXT("episode_steps"), FString::FromInt(EpisodeSteps));

	if (FollowerAgent)
	{
		Info.Add(TEXT("is_alive"), FollowerAgent->bIsAlive ? TEXT("true") : TEXT("false"));
	}

	if (RewardProvider)
	{
		Info.Add(TEXT("current_reward"), FString::SanitizeFloat(RewardProvider->GetReward()));
	}
}

void AFollowerAgentTrainer::ResetTrainer()
{
	// Reset episode counters
	EpisodeReward = 0.0f;
	EpisodeSteps = 0;

	// Reset ScholaAgent episode state
	if (ScholaAgent)
	{
		ScholaAgent->ResetEpisode();
	}

	UE_LOG(LogTemp, Log, TEXT("[FollowerTrainer] %s - Reset for new episode"), *TrainerConfiguration.Name);
}

void AFollowerAgentTrainer::OnCompletion()
{
	// Called when episode ends
	UE_LOG(LogTemp, Log, TEXT("[FollowerTrainer] %s - Episode completed (Total reward: %.2f, Steps: %d)"),
		*TrainerConfiguration.Name, EpisodeReward, EpisodeSteps);
}

//------------------------------------------------------------------------------
// INTERNAL HELPERS
//------------------------------------------------------------------------------

bool AFollowerAgentTrainer::IsAgentDead() const
{
	if (!FollowerAgent)
	{
		return true;
	}

	// Check FollowerAgentComponent's alive flag
	if (!FollowerAgent->bIsAlive)
	{
		return true;
	}

	// Double-check with HealthComponent
	AActor* OwnerActor = FollowerAgent->GetOwner();
	if (OwnerActor)
	{
		UHealthComponent* HealthComp = OwnerActor->FindComponentByClass<UHealthComponent>();
		if (HealthComp && HealthComp->IsDead())
		{
			return true;
		}
	}

	return false;
}

bool AFollowerAgentTrainer::IsEpisodeTimeout() const
{
	// Check for max episode steps (configured in TrainerConfiguration)
	// Reduced from 10000 to 1000 for faster episode completion during training
	// This ensures RLlib sees completed episodes within each iteration
	const int32 MaxSteps = 1000; // ~30 seconds at 30 FPS
	return EpisodeSteps >= MaxSteps;
}
