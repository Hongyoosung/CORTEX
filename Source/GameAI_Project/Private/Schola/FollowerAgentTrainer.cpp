// FollowerAgentTrainer.cpp - Trainer wrapper implementation

#include "Schola/FollowerAgentTrainer.h"
#include "Schola/ScholaAgentComponent.h"
#include "Schola/ScholaCombatEnvironment.h"
#include "Schola/TacticalRewardProvider.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "Combat/Components/HealthComponent.h"
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
	AgentHealthComponent = InAgent->HealthComponent;

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
		*FollowerAgent.Get()->GetName(), *RewardProvider->GetName(), AgentHealthComponent.Get()->bIsAlive ? 1 : 0);

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
	// Multi-actor architecture: Each environment (actor) terminates independently
	// Check if THIS actor's environment has finished

	// DIAGNOSTIC: Track ComputeStatus() calls to verify it's being invoked
	static int32 CallCounter = 0;
	static int32 RunningCounter = 0;
	static int32 CompletedCounter = 0;
	static int32 TruncatedCounter = 0;
	CallCounter++;

	if (CallCounter % 100 == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[ComputeStatus DIAGNOSTIC] Total calls: %d (Running: %d, Completed: %d, Truncated: %d)"),
			CallCounter, RunningCounter, CompletedCounter, TruncatedCounter);
	}

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

	// Multi-actor architecture: Use the environment actor's EnvId directly
	// Each actor manages its own environment, so EnvId IS the environment ID
	int32 EnvironmentID = Env->GetEnvId();
	

	// Optional: Get TeamID for logging purposes
	int32 InTeamID = -1;
	if (FollowerAgent && FollowerAgent->TeamLeader)
	{
		InTeamID = FollowerAgent->TeamLeader->TeamID;
	}

	// Check per-environment termination flags for THIS environment
	bool bShouldTerminate = SimManager->IsEnvironmentEpisodeEnding(EnvironmentID) ||
	                       SimManager->GetEnvironmentLastTerminated(EnvironmentID);

	// Check if THIS environment's episode is ending
	if (bShouldTerminate)
	{
		// Determine if it was a timeout (truncated) or team annihilation (completed)
		bool bWasTimeout = SimManager->GetEnvironmentLastTimeout(EnvironmentID);

		if (bWasTimeout)
		{
			TruncatedCounter++;
			UE_LOG(LogTemp, Warning, TEXT("[ENV %d | Team %d] %s: Episode TRUNCATED (timeout) - Total: %d"),
				EnvironmentID, InTeamID, *TrainerConfiguration.Name, TruncatedCounter);
			return EAgentTrainingStatus::Truncated;
		}
		else
		{
			CompletedCounter++;
			UE_LOG(LogTemp, Warning, TEXT("[ENV %d | Team %d] %s: Episode COMPLETED (team elim) - Total: %d"),
				EnvironmentID, InTeamID, *TrainerConfiguration.Name, CompletedCounter);
			return EAgentTrainingStatus::Completed;
		}
	}

	// Episode is still running for THIS environment
	RunningCounter++;
	if (CallCounter % 100 == 0)
	{
		UE_LOG(LogTemp, VeryVerbose, TEXT("[ENV %d | Team %d] %s: Status=Running (step %d)"),
			EnvironmentID, InTeamID, *TrainerConfiguration.Name, EpisodeSteps);
	}
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
		Info.Add(TEXT("is_alive"), AgentHealthComponent.Get()->bIsAlive ? TEXT("true") : TEXT("false"));
	}

	if (RewardProvider)
	{
		Info.Add(TEXT("current_reward"), FString::SanitizeFloat(RewardProvider->GetReward()));
	}

	// Multi-actor architecture: Pass environment ID to Python
	// Each actor IS a physical environment, so we use the actor's EnvId
	int32 EnvironmentID = -1;
	if (ScholaAgent && ScholaAgent->ScholaEnvironment)
	{
		AScholaCombatEnvironment* Env = Cast<AScholaCombatEnvironment>(ScholaAgent->ScholaEnvironment);
		if (Env)
		{
			EnvironmentID = Env->GetEnvId();
		}
	}
	Info.Add(TEXT("environment_id"), FString::FromInt(EnvironmentID));

	// Also include team ID for debugging
	if (FollowerAgent && FollowerAgent->TeamLeader)
	{
		Info.Add(TEXT("team_id"), FString::FromInt(FollowerAgent->TeamLeader->TeamID));
	}

	// DIAGNOSTIC: Add current training status to info
	EAgentTrainingStatus CurrentStatus = State.TrainingStatus;
	FString StatusString;
	switch (CurrentStatus)
	{
		case EAgentTrainingStatus::Running:
			StatusString = TEXT("Running");
			break;
		case EAgentTrainingStatus::Completed:
			StatusString = TEXT("Completed");
			break;
		case EAgentTrainingStatus::Truncated:
			StatusString = TEXT("Truncated");
			break;
		default:
			StatusString = TEXT("Unknown");
			break;
	}
	Info.Add(TEXT("training_status"), StatusString);
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
	// DIAGNOSTIC: Enhanced logging to track episode completion
	EAgentTrainingStatus FinalStatus = State.TrainingStatus;
	FString StatusString = (FinalStatus == EAgentTrainingStatus::Completed) ? TEXT("Completed") :
		(FinalStatus == EAgentTrainingStatus::Truncated) ? TEXT("Truncated") : TEXT("Unknown");

	UE_LOG(LogTemp, Warning, TEXT("╔═══════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ EPISODE COMPLETION: %s"), *TrainerConfiguration.Name);
	UE_LOG(LogTemp, Warning, TEXT("║ Status: %s"), *StatusString);
	UE_LOG(LogTemp, Warning, TEXT("║ Total Reward: %.2f"), EpisodeReward);
	UE_LOG(LogTemp, Warning, TEXT("║ Total Steps: %d"), EpisodeSteps);
	UE_LOG(LogTemp, Warning, TEXT("╚═══════════════════════════════════════════════════════════════╝"));
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
