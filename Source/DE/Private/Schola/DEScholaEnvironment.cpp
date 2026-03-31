// DEScholaEnvironment.cpp - Schola 2.0.1 training environment implementation

#include "Schola/DEScholaEnvironment.h"
#include "Schola/Components/DEScholaAgent.h"
#include "Schola/Components/DEEpisodeManagerComponent.h"
#include "Schola/Trainers/DETrainer.h"
#include "Core/DETrainingGameMode.h"
#include "Core/Subsystems/DERewardSubsystem.h"
#include "Characters/DEAgent.h"
#include "Actors/DECapturePoint.h"
#include "Team/DEMatchManager.h"
#include "Agent/AgentInterface.h"    // IAgent::Execute_Define / Observe / Act (via DynamicEQS)
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"


ADEScholaEnvironment::ADEScholaEnvironment()
	: bAutoDiscoverAgents(true)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;

	EpisodeManager = CreateDefaultSubobject<UDEEpisodeManagerComponent>(TEXT("EpisodeManager"));
}

void ADEScholaEnvironment::EnsureMatchManagerReady()
{
	if (bMatchManagerReady || !OwnedMatchManager) return;

	OwnedMatchManager->SetEnvID(EnvironmentId);
	OwnedMatchManager->CapturePointInitialize();
	if (!OwnedMatchManager->OnMatchConditionMet.IsAlreadyBound(this, &ADEScholaEnvironment::OnMatchConditionReceived))
	{
		OwnedMatchManager->OnMatchConditionMet.AddDynamic(this, &ADEScholaEnvironment::OnMatchConditionReceived);
	}
	bMatchManagerReady = true;
}

void ADEScholaEnvironment::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] %s BeginPlay — EnvironmentId=%d OwnedMatchManager=%s"),
		*GetName(), EnvironmentId,
		OwnedMatchManager ? *OwnedMatchManager->GetName() : TEXT("NULL"));

	bAgentsRegistered = false;
	bEnvironmentInitialized = false;

	EnsureMatchManagerReady();

	if (bAutoStartMatch)
	{
		StartMatch();
	}
}

void ADEScholaEnvironment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (ADETrainer* Trainer : SpawnedTrainers)
	{
		if (Trainer && IsValid(Trainer))
		{
			Trainer->Destroy();
		}
	}
	SpawnedTrainers.Empty();

	Super::EndPlay(EndPlayReason);
}

void ADEScholaEnvironment::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// Win-condition checking, passive income, and match-clock management have
	// moved to ADEMatchManager::Tick. DEScholaEnvironment reacts via the
	// OnMatchConditionMet delegate bound in BeginPlay.
}

//------------------------------------------------------------------------------
// MATCH MANAGEMENT
//------------------------------------------------------------------------------

void ADEScholaEnvironment::OnMatchConditionReceived(EDEMatchState WinnerState, int32 WinningTeamID)
{
	EndMatch(WinnerState, WinningTeamID);
}

float ADEScholaEnvironment::GetMatchTimer() const
{
	return OwnedMatchManager ? OwnedMatchManager->GetMatchTimer() : 0.0f;
}

float ADEScholaEnvironment::GetTimeRemaining() const
{
	return OwnedMatchManager ? OwnedMatchManager->GetTimeRemaining() : 0.0f;
}

void ADEScholaEnvironment::StartMatch()
{
	if (CurrentMatchState != EDEMatchState::WaitingToStart) return;

	CurrentMatchState = EDEMatchState::InProgress;
	OnEnvMatchStateChanged.Broadcast(CurrentMatchState);

	if (OwnedMatchManager)
	{
		OwnedMatchManager->SpawnTeams();
		OwnedMatchManager->StartMatchTimer();
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] %s: Match started (EnvID: %d)"), *GetName(), EnvironmentId);
}

void ADEScholaEnvironment::EndMatch(EDEMatchState WinnerState, int32 WinningTeamID)
{
	if (bMatchEnded) return;

	bMatchEnded = true;
	CurrentMatchState = WinnerState;
	OnEnvMatchStateChanged.Broadcast(CurrentMatchState);

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Env %d: Match ended — State=%d | Winner=%d | Scores=[%d, %d]"),
		EnvironmentId,
		static_cast<int32>(WinnerState),
		WinningTeamID,
		OwnedMatchManager ? OwnedMatchManager->GetTeamScore(0) : -1,
		OwnedMatchManager ? OwnedMatchManager->GetTeamScore(1) : -1);

	UDERewardSubsystem* RewardSubsystem = OwnedMatchManager ? OwnedMatchManager->GetRewardCalculator() : nullptr;
	if (RewardSubsystem && OwnedMatchManager)
	{
		TArray<ADEAgent*> AllEnvAgents;
		for (int32 TeamID = 0; TeamID <= 1; ++TeamID)
		{
			AllEnvAgents.Append(OwnedMatchManager->GetTeamAgents(TeamID));
			AllEnvAgents.Append(OwnedMatchManager->GetTeamState(TeamID).RespawnQueue);
		}
		RewardSubsystem->ApplyMatchEndReward(WinningTeamID, AllEnvAgents);
	}
}


//------------------------------------------------------------------------------
// IMultiAgentScholaEnvironment INTERFACE
//------------------------------------------------------------------------------

void ADEScholaEnvironment::InitializeEnvironment_Implementation(
	TMap<FString, FInteractionDefinition>& OutAgentDefinitions)
{
	if (bEnvironmentInitialized) return;

	// GymConnectorManager may call InitializeEnvironment before this actor's own BeginPlay.
	// EnsureMatchManagerReady() is idempotent — safe to call here and in BeginPlay.
	EnsureMatchManagerReady();

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] InitializeEnvironment on %s (EnvID: %d)"), *GetName(), EnvironmentId);

	// ── Step 1: Discover agents ──
	if (bAutoDiscoverAgents && OwnedMatchManager)
	{
		// Guard against the race where Schola calls InitializeEnvironment before
		// BeginPlay's StartMatch() -> SpawnTeams() has run for this env.
		// Check team 0 as a proxy; if empty, force-spawn now.
		if (OwnedMatchManager->GetTeamAgents(0).Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] EnvID %d: No agents found yet — SpawnTeams() likely not called. Triggering StartMatch() now."), EnvironmentId);
			StartMatch();
		}

		RegisteredAgents.Empty();
		for (int32 TeamID = 0; TeamID <= 1; TeamID++)
		{
			TArray<ADEAgent*> TeamAgents = OwnedMatchManager->GetTeamAgents(TeamID);
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] EnvID %d Team %d: GetTeamAgents returned %d agents"),
				EnvironmentId, TeamID, TeamAgents.Num());

			int32 SkippedScripted = 0, SkippedNoComponent = 0;
			for (ADEAgent* DEChar : TeamAgents)
			{
				if (!DEChar) continue;
				// Skip scripted AI agents — they don't participate in the Schola pipeline
				if (DEChar->bIsScriptedAI) { ++SkippedScripted; continue; }

				UDEScholaAgent* ScholaAgent = DEChar->GetScholaAgent();
				if (!ScholaAgent)
				{
					ScholaAgent = DEChar->FindComponentByClass<UDEScholaAgent>();
				}
				if (ScholaAgent)
				{
					RegisteredAgents.Add(ScholaAgent);
				}
				else
				{
					++SkippedNoComponent;
					UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] EnvID %d: Agent %s has no UDEScholaAgent component — skipped"),
						EnvironmentId, *DEChar->GetName());
				}
			}

			if (SkippedScripted > 0)
				UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] EnvID %d Team %d: Skipped %d scripted AI agents"),
					EnvironmentId, TeamID, SkippedScripted);
			if (SkippedNoComponent > 0)
				UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] EnvID %d Team %d: Skipped %d agents (no ScholaAgent component)"),
					EnvironmentId, TeamID, SkippedNoComponent);
		}
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Discovered %d agents for EnvID %d"),
			RegisteredAgents.Num(), EnvironmentId);
	}
	else
	{
		if (!bAutoDiscoverAgents)
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] EnvID %d: bAutoDiscoverAgents=false — skipping agent discovery"), EnvironmentId);
		if (!OwnedMatchManager)
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] EnvID %d: OwnedMatchManager is NULL — cannot discover agents. Check Blueprint assignment."), EnvironmentId);
	}

	// ── Step 2: Spawn / assign trainers and build AgentTrainerMap ──
	if (bTrainingMode)
	{
		AgentTrainerMap.Empty();
		SpawnedTrainers.Empty();

		TSubclassOf<ADETrainer> TrainerClassToUse = TrainerClass ? *TrainerClass : ADETrainer::StaticClass();

		for (int32 i = 0; i < RegisteredAgents.Num(); i++)
		{
			UDEScholaAgent* Agent = Cast<UDEScholaAgent>(RegisteredAgents[i]);
			if (!Agent || !Agent->GetOwner()) continue;

			ADEAgent* DEChar = Cast<ADEAgent>(Agent->GetOwner());
			if (!DEChar) continue;

			APawn* ControlledPawn = Cast<APawn>(DEChar);
			if (!ControlledPawn) continue;

			// Force-assign the correct EnvID before trainer initialization.
			// Duplicated/pre-placed agents keep EnvID=0 (default) because SpawnAgent()
			// was never called for them. Setting it here ensures the trainer caches
			// the right ADEMatchManager and ADEScholaEnvironment.
			DEChar->SetEnvID_Implementation(EnvironmentId);

			FString AgentId = FString::Printf(TEXT("env%d_agent%d"), EnvironmentId, i);

			ADETrainer* Trainer = Cast<ADETrainer>(ControlledPawn->GetController());

			if (Trainer)
			{
				Trainer->InitializeDETrainer(Agent);
			}
			else if (bAutoSpawnTrainers)
			{
				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

				Trainer = GetWorld()->SpawnActor<ADETrainer>(
					TrainerClassToUse,
					ControlledPawn->GetActorLocation(),
					FRotator::ZeroRotator,
					SpawnParams
				);

				if (Trainer)
				{
					Trainer->Possess(ControlledPawn);
					Trainer->InitializeDETrainer(Agent);
					SpawnedTrainers.Add(Trainer);
				}
			}

			if (Trainer)
			{
				AgentTrainerMap.Add(AgentId, Trainer);

				// Route through DynamicEQS agent component (Define_Implementation delegates internally).
				FInteractionDefinition Def;
				IAgent::Execute_Define(Agent, Def);
				OutAgentDefinitions.Add(AgentId, Def);
				UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] EnvID %d: Registered agent %s"), EnvironmentId, *AgentId);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] EnvID %d: Failed to assign Trainer for agent index %d (%s). "
					"Controller=%s  bAutoSpawnTrainers=%d"),
					EnvironmentId, i, *AgentId,
					ControlledPawn->GetController() ? *ControlledPawn->GetController()->GetClass()->GetName() : TEXT("nullptr"),
					bAutoSpawnTrainers ? 1 : 0);
			}
		}

		bAgentsRegistered = true;
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] %d agents registered for EnvID %d"),
			AgentTrainerMap.Num(), EnvironmentId);
	}

	bEnvironmentInitialized = true;
}

void ADEScholaEnvironment::Reset_Implementation(TMap<FString, FInitialAgentState>& OutAgentState)
{
	if (EpisodeManager && EpisodeManager->CheckDuplicateReset(EnvironmentId))
	{
		return;
	}

	bTrainingActive = true;

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Reset %s (EnvID: %d)"), *GetName(), EnvironmentId);

	if (EpisodeManager)
	{
		EpisodeManager->SetEnvironmentID(EnvironmentId);
		EpisodeManager->StartNewEpisode(EnvironmentId);
	}

	bMatchEnded = false;
	CurrentMatchState = EDEMatchState::InProgress;

	if (OwnedMatchManager)
	{
		OwnedMatchManager->ResetEnvironment();
	}

	// Reset all trainers.
	for (auto& Pair : AgentTrainerMap)
	{
		if (Pair.Value)
		{
			Pair.Value->ResetTrainer();
		}
	}

	// Collect initial observations.
	for (auto& Pair : AgentTrainerMap)
	{
		ADETrainer* Trainer = Pair.Value;
		if (!Trainer) continue;

		ADEAgent* DEChar = Cast<ADEAgent>(Trainer->GetPawn());
		if (!DEChar) continue;

		FInitialAgentState InitState;
		if (UDEScholaAgent* AgentComp = DEChar->FindComponentByClass<UDEScholaAgent>())
		{
			IAgent::Execute_Observe(AgentComp, InitState.Observations);
		}

		OutAgentState.Add(Pair.Key, InitState);
	}

	OnScholaEnvironmentInitialized_Delegate.Broadcast();
}

void ADEScholaEnvironment::Step_Implementation(
	const TMap<FString, FInstancedStruct>& InActions,
	TMap<FString, FAgentState>& OutAgentStates)
{
	for (auto& Pair : AgentTrainerMap)
	{
		const FString& AgentId = Pair.Key;
		ADETrainer* Trainer = Pair.Value;
		if (!Trainer) continue;

		ADEAgent* DEChar = Cast<ADEAgent>(Trainer->GetPawn());
		if (!DEChar) continue;

		// Apply action and collect observation via DynamicEQS agent component.
		UDEScholaAgent* AgentComp = DEChar->FindComponentByClass<UDEScholaAgent>();
		if (const FInstancedStruct* ActionPtr = InActions.Find(AgentId))
		{
			if (AgentComp) IAgent::Execute_Act(AgentComp, *ActionPtr);
		}

		FAgentState State;
		if (AgentComp)
		{
			IAgent::Execute_Observe(AgentComp, State.Observations);
		}

		// Compute reward and status.
		State.Reward = Trainer->ComputeReward();
		EAgentTrainingStatus TrainingStatus = Trainer->ComputeStatus();
		State.bTerminated = (TrainingStatus == EAgentTrainingStatus::Completed);
		State.bTruncated  = (TrainingStatus == EAgentTrainingStatus::Truncated);
		Trainer->GetInfo(State.Info);

		if (State.bTerminated || State.bTruncated)
		{
			Trainer->OnCompletion();
		}

		OutAgentStates.Add(AgentId, State);
	}
}

void ADEScholaEnvironment::SeedEnvironment_Implementation(int Seed)
{
	int32 UniqueSeed = Seed + (EnvironmentId * 1337);
	EnvRandomStream.Initialize(UniqueSeed);

	if (OwnedMatchManager)
	{
		OwnedMatchManager->SetEnvRandomStream(EnvRandomStream);
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] SeedEnvironment: EnvID %d seed %d"), EnvironmentId, UniqueSeed);
}

