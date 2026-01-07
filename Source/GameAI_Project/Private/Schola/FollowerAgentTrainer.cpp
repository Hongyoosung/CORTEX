// FollowerAgentTrainer.cpp - Trainer wrapper implementation

#include "Schola/FollowerAgentTrainer.h"
#include "Schola/ScholaAgentComponent.h"
#include "Schola/ScholaCombatEnvironment.h"
#include "Schola/TacticalRewardProvider.h"
#include "Team/FollowerAgentComponent.h"
#include "Combat/HealthComponent.h"
#include "Core/SimulationManagerGameMode.h"

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

	// CRITICAL: Unpossess existing controller (FollowerAIController) before training
	AController* ExistingController = ControlledPawn->GetController();
	if (ExistingController && ExistingController != this)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerTrainer] Training takeover: unpossessing %s"), *ExistingController->GetName());
		ExistingController->UnPossess();
	}

	// Possess the pawn for training
	Possess(ControlledPawn);

	// DIAGNOSTIC: Verify PathFollowingComponent exists after possession
	UPathFollowingComponent* PathComp = GetPathFollowingComponent();
	UE_LOG(LogTemp, Warning, TEXT("[FollowerTrainer] After Possess('%s'): PathFollowingComponent=%s"),
		*ControlledPawn->GetName(),
		PathComp ? TEXT("✅ Valid") : TEXT("❌ NULL - MOVEMENT WILL FAIL!"));

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
	if (!RewardProvider)
	{
		return 0.0f;
	}

	float StepReward = RewardProvider->GetReward();
	EpisodeReward += StepReward;
	EpisodeSteps++;

	return StepReward;
}

EAgentTrainingStatus AFollowerAgentTrainer::ComputeStatus()
{
	// CRITICAL FIX: Episode termination is now environment-driven, NOT agent-driven
	// Check if SimulationManager says the episode is ending (team annihilation or timeout)
	// This ensures all agents terminate together when the episode ends

	if (!ScholaAgent || !ScholaAgent->ScholaEnvironment)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerTrainer] ComputeStatus: ScholaAgent or Environment is NULL!"));
		return EAgentTrainingStatus::Running;
	}

	// Get SimulationManager from environment
	AScholaCombatEnvironment* Env = Cast<AScholaCombatEnvironment>(ScholaAgent->ScholaEnvironment);
	if (!Env || !Env->SimulationManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[FollowerTrainer] ComputeStatus: SimulationManager not found!"));
		return EAgentTrainingStatus::Running;
	}

	ASimulationManagerGameMode* SimManager = Env->SimulationManager;

	// Check if episode is ending (set by SimulationManager when team is annihilated or timeout)
	if (SimManager->IsEpisodeEnding())
	{
		// Determine if it was a timeout (truncated) or team annihilation (completed)
		bool bWasTimeout = (SimManager->GetMaxStepsPerEpisode() > 0 && SimManager->GetCurrentStep() >= SimManager->GetMaxStepsPerEpisode()) ||
						   (SimManager->GetMaxEpisodeDuration() > 0.0f && (GetWorld()->GetTimeSeconds() - SimManager->GetEpisodeStartTime()) >= SimManager->GetMaxEpisodeDuration());

		if (bWasTimeout)
		{
			UE_LOG(LogTemp, Log, TEXT("[FollowerTrainer] %s: Episode TRUNCATED (timeout)"), *TrainerConfiguration.Name);
			return EAgentTrainingStatus::Truncated;
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[FollowerTrainer] %s: Episode COMPLETED (team annihilation)"), *TrainerConfiguration.Name);
			return EAgentTrainingStatus::Completed;
		}
	}

	// Episode is still running
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

	// CRITICAL FIX: Ensure trainer is possessing its pawn
	// State.bExists is set to false in OnUnPossess() and true in OnPossess()
	// If the pawn was unpossessed for any reason, re-possess it here
	APawn* CurrentPawn = GetPawn();
	if (CurrentPawn && !CurrentPawn->GetController())
	{
		UE_LOG(LogTemp, Warning, TEXT("[FollowerTrainer] %s: Pawn not possessed during reset - re-possessing"), *TrainerConfiguration.Name);
		Possess(CurrentPawn);
	}
	else if (!CurrentPawn && ScholaAgent)
	{
		APawn* TargetPawn = ScholaAgent->GetControlledPawn();
		if (TargetPawn)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FollowerTrainer] %s: No pawn during reset - possessing from ScholaAgent"), *TrainerConfiguration.Name);
			Possess(TargetPawn);
		}
	}

	// Reset ScholaAgent episode state
	if (ScholaAgent)
	{
		ScholaAgent->ResetEpisode();
	}
}

void AFollowerAgentTrainer::OnCompletion()
{
	// Called when episode ends
	UE_LOG(LogTemp, Log, TEXT("[FollowerTrainer] %s - Episode completed (Total reward: %.2f, Steps: %d)"),
		*TrainerConfiguration.Name, EpisodeReward, EpisodeSteps);
}

void AFollowerAgentTrainer::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

}

void AFollowerAgentTrainer::OnUnPossess()
{
	UE_LOG(LogTemp, Error, TEXT("[FollowerTrainer] ❌ OnUnPossess called: Losing control of '%s' - CALLSTACK NEEDED!"),
		*GetNameSafe(GetPawn()));

	Super::OnUnPossess();
}

//------------------------------------------------------------------------------
// REMOVED INTERNAL HELPERS (now using environment-level termination)
//------------------------------------------------------------------------------
// IsAgentDead() - REMOVED: Individual agent death no longer triggers episode end
// IsEpisodeTimeout() - REMOVED: Timeout checked at environment level via SimulationManager
