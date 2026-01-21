#include "Team/RLAgentComponent.h"
#include "RL/RLPolicyNetwork.h"
#include "RL/RewardCalculator.h"

URLAgentComponent::URLAgentComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // No tick needed - called explicitly
}

void URLAgentComponent::BeginPlay()
{
	Super::BeginPlay();

	// Initialize RL policy if not set (v6.0: Single-head network)
	if (!TacticalPolicy && bUseRLPolicy)
	{
		TacticalPolicy = NewObject<URLPolicyNetwork>(this);
		FRLPolicyConfig Config;  // v6.0: Single-head policy config

		UE_LOG(LogTemp, Log, TEXT("[RLAgent] '%s': Created RL policy (v6.0 single-head)"), *GetOwner()->GetName());
	}

	// Find or create RewardCalculator
	RewardCalculator = GetOwner()->FindComponentByClass<URewardCalculator>();
	if (!RewardCalculator)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RLAgent] '%s': No RewardCalculator found, hierarchical rewards disabled"),
			*GetOwner()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[RLAgent] '%s': Found RewardCalculator, hierarchical rewards enabled"),
			*GetOwner()->GetName());
	}

	UE_LOG(LogTemp, Log, TEXT("RLAgentComponent: Initialized on %s"), *GetOwner()->GetName());
}

//------------------------------------------------------------------------------
// REINFORCEMENT LEARNING
//------------------------------------------------------------------------------

void URLAgentComponent::ProvideReward(float Reward, bool bTerminal)
{
	// Always accumulate reward (independent of RL policy or experience collection)
	AccumulatedReward += Reward;

	UE_LOG(LogTemp, Verbose, TEXT("[RLAgent] '%s': Provided reward %.2f (Accumulated: %.2f, Terminal: %s)"),
		*GetOwner()->GetName(), Reward, AccumulatedReward, bTerminal ? TEXT("Yes") : TEXT("No"));

	// Reset episode if terminal
	if (bTerminal)
	{
		ResetEpisode();
	}
}

void URLAgentComponent::ResetEpisode()
{
	AccumulatedReward = 0.0f;

	UE_LOG(LogTemp, Log, TEXT("[RLAgent] '%s': Episode reset"), *GetOwner()->GetName());
}

void URLAgentComponent::OnEpisodeEnded(float EpisodeReward)
{
	// Add episode reward to accumulated reward
	AccumulatedReward += EpisodeReward;

	// Provide terminal reward to RL policy
	ProvideReward(EpisodeReward, true); // bTerminal = true

	UE_LOG(LogTemp, Log, TEXT("[RLAgent] '%s': Episode ended - EpisodeReward=%.2f, TotalAccumulated=%.2f"),
		*GetOwner()->GetName(), EpisodeReward, AccumulatedReward);
}
