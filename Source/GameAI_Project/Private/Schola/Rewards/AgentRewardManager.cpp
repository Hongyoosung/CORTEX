// TacticalRewardProvider.cpp - v9.0: Strategy-specific reward provider

#include "Schola/Rewards/AgentRewardManager.h"
#include "Observation/Components/ObservationBuilderComponent.h"
#include "Team/TeamTypes.h"  


UAgentRewardManager::UAgentRewardManager()
	: Super()
	, LastRewardValue(0.0f)
{
}


float UAgentRewardManager::GetReward()
{
	return CurrentReward;
}

void UAgentRewardManager::AccumulateReward(float Reward)
{
	CurrentReward += Reward;
}

void UAgentRewardManager::Reset()
{
	CurrentReward = 0.0f;
}
