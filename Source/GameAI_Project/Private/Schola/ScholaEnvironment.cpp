// ScholaEnvironment.cpp - Schola training environment implementation (v10.2 Parallel Isolated)

#include "Schola/ScholaEnvironment.h"
#include "Schola/Components/ScholaMocAgent.h"
#include "Schola/Components/EpisodeManagerComponent.h"
#include "Schola/Trainers/MocTrainer.h"
#include "Core/MocGameMode.h"
#include "Core/Subsystems/MocRewardSubsystem.h"
#include "Characters/MocCharacter.h"
#include "Team/MatchManager.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"


AScholaEnvironment::AScholaEnvironment(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bAutoDiscoverAgents(true)
	, bLogTacticalPlays(false)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;

	EpisodeManager = CreateDefaultSubobject<UEpisodeManagerComponent>(TEXT("EpisodeManager"));
}

void AScholaEnvironment::BeginPlay()
{
	// NOTE: Do NOT call Super::BeginPlay() — Schola subsystem calls Initialize() later.

	bAgentsRegistered = false;
	bEnvironmentInitialized = false;

	if (OwnedMatchManager)
	{
		OwnedMatchManager->SetEnvID(ScholaEnvID);
		OwnedMatchManager->CapturePointInitialize();
	}

	// Auto-start match
	if (bAutoStartMatch)
	{
		StartMatch();
	}
}

void AScholaEnvironment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Cleanup spawned trainers
	for (AMocTrainer* Trainer : SpawnedTrainers)
	{
		if (Trainer && IsValid(Trainer))
		{
			Trainer->Destroy();
		}
	}
	SpawnedTrainers.Empty();

	Super::EndPlay(EndPlayReason);
}

void AScholaEnvironment::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (CurrentMatchState != EMocMatchState::InProgress)
	{
		return;
	}

	MatchTimer += DeltaTime;
}

//------------------------------------------------------------------------------
// MATCH MANAGEMENT
//------------------------------------------------------------------------------

void AScholaEnvironment::StartMatch()
{
	if (CurrentMatchState != EMocMatchState::WaitingToStart)
	{
		return;
	}

	MatchTimer = 0.0f;
	PassiveIncomeAccumulator = 0.0f;
	CurrentMatchState = EMocMatchState::InProgress;
	OnEnvMatchStateChanged.Broadcast(CurrentMatchState);

	if (OwnedMatchManager)
	{
		OwnedMatchManager->SpawnTeams();
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] %s: Match started (EnvID: %d)"), *GetName(), ScholaEnvID);
}

void AScholaEnvironment::EndMatch(EMocMatchState WinnerState)
{
	if (bMatchEnded)
	{
		return;
	}

	bMatchEnded = true;
	CurrentMatchState = WinnerState;
	OnEnvMatchStateChanged.Broadcast(CurrentMatchState);

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] %s: Match ended - State: %d"), *GetName(), static_cast<int32>(WinnerState));

	// 전역 매치 종료 보상 라우팅 (Match End Reward Routing)
	UMocRewardSubsystem* RewardSubsystem = GetWorld()->GetSubsystem<UMocRewardSubsystem>();
	if (RewardSubsystem && OwnedMatchManager)
	{
		int32 WinningTeamID = -1;
		if (WinnerState == EMocMatchState::RedTeamWon) WinningTeamID = 0;
		else if (WinnerState == EMocMatchState::BlueTeamWon) WinningTeamID = 1;
	}
}


//------------------------------------------------------------------------------
// SCHOLA ENVIRONMENT INTERFACE
//------------------------------------------------------------------------------

void AScholaEnvironment::InitializeEnvironment()
{
	if (bEnvironmentInitialized)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] InitializeEnvironment() called on %s (EnvID: %d)"), *GetName(), ScholaEnvID);

	// v10.2: Discover agents from OwnedMatchManager (scoped, not world-wide)
	if (bAutoDiscoverAgents && OwnedMatchManager)
	{
		RegisteredAgents.Empty();

		// Get agents from both teams via the owned MatchManager
		for (int32 TeamID = 0; TeamID <= 1; TeamID++)
		{
			TArray<AMocCharacter*> TeamAgents = OwnedMatchManager->GetTeamAgents(TeamID);
			for (AMocCharacter* MocChar : TeamAgents)
			{
				if (!MocChar) continue;

				UScholaMocAgent* ScholaAgent = MocChar->GetScholaAgent();
				if (!ScholaAgent)
				{
					ScholaAgent = MocChar->FindComponentByClass<UScholaMocAgent>();
				}

				if (ScholaAgent)
				{
					RegisteredAgents.Add(ScholaAgent);
					UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2]   Discovered: %s (Team %d, EnvID %d)"),
						*MocChar->GetName(), TeamID, MocChar->GetEnvID_Implementation());
				}
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Scoped discovery complete: %d agents for EnvID %d"),
			RegisteredAgents.Num(), ScholaEnvID);
	}


	// Propagate Phase 1 RL training flag
	if (OwnedMatchManager)
	{
		OwnedMatchManager->bRLTrainingMode = bPhase1RLTraining;
	}

	bEnvironmentInitialized = true;
}

void AScholaEnvironment::ResetEnvironment()
{
	// Check for duplicate reset
	if (EpisodeManager && EpisodeManager->CheckDuplicateReset(EnvId))
	{
		return;
	}

	bTrainingActive = true;

	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v10.2] %s (EnvID: %d) - resetting"), *GetName(), ScholaEnvID);

	if (EpisodeManager)
	{
		EpisodeManager->SetEnvironmentID(EnvId);
		EpisodeManager->StartNewEpisode(EnvId);
	}

	// Reset match state directly (no GameMode dependency)
	MatchTimer = 0.0f;
	PassiveIncomeAccumulator = 0.0f;
	bMatchEnded = false;
	CurrentMatchState = EMocMatchState::InProgress;

	// Reset MatchManager (teams, agents, squad commanders)
	if (OwnedMatchManager)
	{
		OwnedMatchManager->ResetEnvironment();
	}


	// Squad Commanders are reset inside OwnedTeamManager->ResetTeams().

	OnScholaEnvironmentInitialized_Delegate.Broadcast();

	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v10.2] %s reset complete"), *GetName());
}

void AScholaEnvironment::RegisterAgents(TArray<APawn*>& OutTrainerControlledPawns)
{
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] RegisterAgents on %s (EnvID: %d, Agents: %d)"),
		*GetName(), ScholaEnvID, RegisteredAgents.Num());

	if (!bTrainingMode)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Inference mode — skipping trainer registration"));
		return;
	}

	if (bAgentsRegistered)
	{
		for (UScholaMocAgent* Agent : RegisteredAgents)
		{
			if (Agent && Agent->GetOwner())
			{
				APawn* Pawn = Cast<APawn>(Agent->GetOwner());
				if (Pawn && Pawn->GetController())
				{
					OutTrainerControlledPawns.Add(Pawn);
				}
			}
		}
		return;
	}

	OutTrainerControlledPawns.Empty();

	TSubclassOf<AMocTrainer> TrainerClassToUse = TrainerClass;
	if (!TrainerClassToUse)
	{
		TrainerClassToUse = AMocTrainer::StaticClass();
	}

	int32 TrainersAssigned = 0;
	int32 TrainersSpawned = 0;

	for (int32 i = 0; i < RegisteredAgents.Num(); i++)
	{
		UScholaMocAgent* Agent = RegisteredAgents[i];
		if (!Agent || !Agent->GetOwner()) continue;

		AMocCharacter* MocChar = Cast<AMocCharacter>(Agent->GetOwner());
		if (!MocChar) continue;

		APawn* ControlledPawn = Cast<APawn>(MocChar);
		if (!ControlledPawn) continue;

		AMocTrainer* Trainer = Cast<AMocTrainer>(ControlledPawn->GetController());

		if (Trainer)
		{
			Trainer->InitializeMocTrainer(Agent);
			TrainersAssigned++;
		}
		else if (bAutoSpawnTrainers)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			Trainer = GetWorld()->SpawnActor<AMocTrainer>(
				TrainerClassToUse,
				ControlledPawn->GetActorLocation(),
				FRotator::ZeroRotator,
				SpawnParams
			);

			if (Trainer)
			{
				Trainer->Possess(ControlledPawn);
				Trainer->Initialize(this->EnvId, i, ControlledPawn);
				Trainer->InitializeMocTrainer(Agent);
				SpawnedTrainers.Add(Trainer);
				TrainersSpawned++;
			}
			else
			{
				continue;
			}
		}
		else
		{
			continue;
		}

		OutTrainerControlledPawns.Add(ControlledPawn);
	}

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Registration complete: Found=%d, Spawned=%d, Total=%d"),
		TrainersAssigned, TrainersSpawned, OutTrainerControlledPawns.Num());

	bAgentsRegistered = true;
}


void AScholaEnvironment::SeedEnvironment(int Seed)
{
	int32 UniqueSeed = Seed + (ScholaEnvID * 1337);
	EnvRandomStream.Initialize(UniqueSeed);

	if (OwnedMatchManager)
	{
		OwnedMatchManager->SetEnvRandomStream(EnvRandomStream);
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] SeedEnvironment: EnvID %d initialized RandomStream with seed %d"), ScholaEnvID, UniqueSeed);
}
