// DEScholaEnvironment.cpp - Schola training environment implementation (v10.2 Parallel Isolated)

#include "Schola/DEScholaEnvironment.h"
#include "Schola/Components/DEScholaAgent.h"
#include "Schola/Components/DEEpisodeManagerComponent.h"
#include "Schola/Trainers/DETrainer.h"
#include "Core/DEGameMode.h"
#include "Core/Subsystems/DERewardSubsystem.h"
#include "Characters/DECharacter.h"
#include "Actors/DECapturePoint.h"
#include "Team/DEMatchManager.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"


ADEScholaEnvironment::ADEScholaEnvironment(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bAutoDiscoverAgents(true)
	, bLogTacticalPlays(false)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;

	EpisodeManager = CreateDefaultSubobject<UDEEpisodeManagerComponent>(TEXT("EpisodeManager"));
}

void ADEScholaEnvironment::BeginPlay()
{
	// NOTE: Do NOT call Super::BeginPlay() — Schola subsystem calls Initialize() later.

	bAgentsRegistered = false;
	bEnvironmentInitialized = false;

	if (OwnedMatchManager)
	{
		OwnedMatchManager->SetEnvID(EnvironmentId);
		OwnedMatchManager->CapturePointInitialize();
	}

	// Auto-start match
	if (bAutoStartMatch)
	{
		StartMatch();
	}
}

void ADEScholaEnvironment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Cleanup spawned trainers
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

	if (CurrentMatchState != EDEMatchState::InProgress)
	{
		return;
	}

	MatchTimer += DeltaTime;

	if (!OwnedMatchManager) return;

	// ── Passive income: each owned capture point earns PassiveIncomeRate pts/s ──
	if (PassiveIncomeRate > 0.0f)
	{
		PassiveIncomeAccumulator += DeltaTime;
		if (PassiveIncomeAccumulator >= 1.0f)
		{
			const float WholeSeconds = FMath::FloorToFloat(PassiveIncomeAccumulator);
			PassiveIncomeAccumulator -= WholeSeconds;

			for (const ADECapturePoint* CP : OwnedMatchManager->GetCapturePoints())
			{
				if (!CP) continue;
				const int32 OwnerTeam = CP->GetTeamID_Implementation();
				if (OwnerTeam >= 0)
				{
					OwnedMatchManager->AddTeamScore(OwnerTeam,
						FMath::RoundToInt(PassiveIncomeRate * WholeSeconds));
				}
			}
		}
	}

	// ── Win condition: first team to reach WinningScore ends the match ──
	for (int32 TeamID = 0; TeamID <= 1; ++TeamID)
	{
		if (OwnedMatchManager->GetTeamScore(TeamID) >= WinningScore)
		{
			const EDEMatchState WinState = (TeamID == 0)
				? EDEMatchState::RedTeamWon
				: EDEMatchState::BlueTeamWon;
			UE_LOG(LogTemp, Warning,
				TEXT("[ScholaEnv] Env %d: Team %d reached winning score %d — ending match"),
				EnvironmentId, TeamID, WinningScore);
			EndMatch(WinState);
			return;
		}
	}

	// ── Domination win condition: one team owns ALL capture points ──
	if (bDominationWinEnabled)
	{
		const TArray<ADECapturePoint*>& CPs = OwnedMatchManager->GetCapturePoints();
		if (CPs.Num() > 0)
		{
			for (int32 TeamID = 0; TeamID <= 1; ++TeamID)
			{
				bool bOwnsAll = true;
				for (const ADECapturePoint* CP : CPs)
				{
					if (!CP || CP->GetTeamID_Implementation() != TeamID)
					{
						bOwnsAll = false;
						break;
					}
				}
				if (bOwnsAll)
				{
					const EDEMatchState WinState = (TeamID == 0)
						? EDEMatchState::RedTeamWon
						: EDEMatchState::BlueTeamWon;
					UE_LOG(LogTemp, Warning,
						TEXT("[ScholaEnv] Env %d: Team %d achieved domination (all %d caps) — ending match"),
						EnvironmentId, TeamID, CPs.Num());
					EndMatch(WinState);
					return;
				}
			}
		}
	}

	// ── Timeout: team with higher score wins ──
	if (MatchTimer >= MaxMatchDuration)
	{
		const int32 LeadTeam = OwnedMatchManager->GetWinnerTeamID();
		EDEMatchState TimeoutState;
		if (LeadTeam == 0)       TimeoutState = EDEMatchState::RedTeamWon;
		else if (LeadTeam == 1)  TimeoutState = EDEMatchState::BlueTeamWon;
		else                     TimeoutState = EDEMatchState::TimeExpired; // tie

		const FString ResultStr = (LeadTeam == -1)
			? TEXT("Draw")
			: FString::Printf(TEXT("Team %d wins"), LeadTeam);
		UE_LOG(LogTemp, Warning,
			TEXT("[ScholaEnv] Env %d: Timeout — Scores [%d, %d] → %s"),
			EnvironmentId,
			OwnedMatchManager->GetTeamScore(0),
			OwnedMatchManager->GetTeamScore(1),
			*ResultStr);
		EndMatch(TimeoutState);
	}
}

//------------------------------------------------------------------------------
// MATCH MANAGEMENT
//------------------------------------------------------------------------------

void ADEScholaEnvironment::StartMatch()
{
	if (CurrentMatchState != EDEMatchState::WaitingToStart)
	{
		return;
	}

	MatchTimer = 0.0f;
	PassiveIncomeAccumulator = 0.0f;
	CurrentMatchState = EDEMatchState::InProgress;
	OnEnvMatchStateChanged.Broadcast(CurrentMatchState);

	if (OwnedMatchManager)
	{
		OwnedMatchManager->SpawnTeams();
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] %s: Match started (EnvID: %d)"), *GetName(), EnvironmentId);
}

void ADEScholaEnvironment::EndMatch(EDEMatchState WinnerState)
{
	if (bMatchEnded)
	{
		return;
	}

	bMatchEnded = true;
	CurrentMatchState = WinnerState;
	OnEnvMatchStateChanged.Broadcast(CurrentMatchState);

	// Determine the winning team (-1 = tie / time expired with equal scores)
	int32 WinningTeamID = -1;
	if (WinnerState == EDEMatchState::RedTeamWon)       WinningTeamID = 0;
	else if (WinnerState == EDEMatchState::BlueTeamWon) WinningTeamID = 1;

	const FString WinnerStr = (WinningTeamID == -1)
		? TEXT("Tie") : FString::Printf(TEXT("Team %d"), WinningTeamID);
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Env %d: Match ended — State=%d | Winner=%s | Scores=[%d, %d]"),
		EnvironmentId,
		static_cast<int32>(WinnerState),
		*WinnerStr,
		OwnedMatchManager ? OwnedMatchManager->GetTeamScore(0) : -1,
		OwnedMatchManager ? OwnedMatchManager->GetTeamScore(1) : -1);

	// Distribute terminal win/loss reward to all agents' sparse reward buffers.
	// DETrainer::ComputeReward() will drain them on the next step, before ComputeStatus()
	// returns Truncated. This guarantees Python receives the reward in the terminal transition.
	UDERewardSubsystem* RewardSubsystem = OwnedMatchManager ? OwnedMatchManager->GetRewardCalculator() : nullptr;
	if (RewardSubsystem && OwnedMatchManager)
	{
		// Collect all agents from both teams (active + queued respawn)
		TArray<ADECharacter*> AllEnvAgents;
		for (int32 TeamID = 0; TeamID <= 1; ++TeamID)
		{
			AllEnvAgents.Append(OwnedMatchManager->GetTeamAgents(TeamID));
			AllEnvAgents.Append(OwnedMatchManager->GetTeamState(TeamID).RespawnQueue);
		}
		RewardSubsystem->ApplyMatchEndReward(WinningTeamID, AllEnvAgents);
	}
}


//------------------------------------------------------------------------------
// SCHOLA ENVIRONMENT INTERFACE
//------------------------------------------------------------------------------

void ADEScholaEnvironment::InitializeEnvironment()
{
	if (bEnvironmentInitialized)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] InitializeEnvironment() called on %s (EnvID: %d)"), *GetName(), EnvironmentId);

	// v10.2: Discover agents from OwnedMatchManager (scoped, not world-wide)
	if (bAutoDiscoverAgents && OwnedMatchManager)
	{
		RegisteredAgents.Empty();

		// Get agents from both teams via the owned DEMatchManager
		for (int32 TeamID = 0; TeamID <= 1; TeamID++)
		{
			TArray<ADECharacter*> TeamAgents = OwnedMatchManager->GetTeamAgents(TeamID);
			for (ADECharacter* DEChar : TeamAgents)
			{
				if (!DEChar) continue;

				UDEScholaAgent* ScholaAgent = DEChar->GetScholaAgent();
				if (!ScholaAgent)
				{
					ScholaAgent = DEChar->FindComponentByClass<UDEScholaAgent>();
				}

				if (ScholaAgent)
				{
					RegisteredAgents.Add(ScholaAgent);
					UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2]   Discovered: %s (Team %d, EnvID %d)"),
						*DEChar->GetName(), TeamID, DEChar->GetEnvID_Implementation());
				}
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Scoped discovery complete: %d agents for EnvID %d"),
			RegisteredAgents.Num(), EnvironmentId);
	}


	// Propagate Phase 1 RL training flag
	if (OwnedMatchManager)
	{
		OwnedMatchManager->bRLTrainingMode = bPhase1RLTraining;
	}

	bEnvironmentInitialized = true;
}

void ADEScholaEnvironment::ResetEnvironment()
{
	// Check for duplicate reset
	if (EpisodeManager && EpisodeManager->CheckDuplicateReset(EnvId))
	{
		return;
	}

	bTrainingActive = true;

	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v10.2] %s (EnvID: %d) - resetting"), *GetName(), EnvironmentId);

	if (EpisodeManager)
	{
		EpisodeManager->SetEnvironmentID(EnvId);
		EpisodeManager->StartNewEpisode(EnvId);
	}

	// Reset match state directly (no GameMode dependency)
	MatchTimer = 0.0f;
	PassiveIncomeAccumulator = 0.0f;
	bMatchEnded = false;
	CurrentMatchState = EDEMatchState::InProgress;

	// Reset DEMatchManager (teams, agents, squad commanders)
	if (OwnedMatchManager)
	{
		OwnedMatchManager->ResetEnvironment();
	}


	// Squad Commanders are reset inside OwnedTeamManager->ResetTeams().

	OnScholaEnvironmentInitialized_Delegate.Broadcast();

	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v10.2] %s reset complete"), *GetName());
}

void ADEScholaEnvironment::RegisterAgents(TArray<APawn*>& OutTrainerControlledPawns)
{
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] RegisterAgents on %s (EnvID: %d, Agents: %d)"),
		*GetName(), EnvironmentId, RegisteredAgents.Num());

	if (!bTrainingMode)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Inference mode — skipping trainer registration"));
		return;
	}

	if (bAgentsRegistered)
	{
		for (UDynamicEQSAgentComponent* AgentComp : RegisteredAgents)
		{
			UDEScholaAgent* Agent = Cast<UDEScholaAgent>(AgentComp);
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

	TSubclassOf<ADETrainer> TrainerClassToUse = TrainerClass;
	if (!TrainerClassToUse)
	{
		TrainerClassToUse = ADETrainer::StaticClass();
	}

	int32 TrainersAssigned = 0;
	int32 TrainersSpawned = 0;

	for (int32 i = 0; i < RegisteredAgents.Num(); i++)
	{
		UDEScholaAgent* Agent = Cast<UDEScholaAgent>(RegisteredAgents[i]);
		if (!Agent || !Agent->GetOwner()) continue;

		ADECharacter* DEChar = Cast<ADECharacter>(Agent->GetOwner());
		if (!DEChar) continue;

		APawn* ControlledPawn = Cast<APawn>(DEChar);
		if (!ControlledPawn) continue;

		ADETrainer* Trainer = Cast<ADETrainer>(ControlledPawn->GetController());

		if (Trainer)
		{
			Trainer->InitializeDETrainer(Agent);
			TrainersAssigned++;
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
				Trainer->Initialize(this->EnvId, i, ControlledPawn);
				Trainer->InitializeDETrainer(Agent);
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


void ADEScholaEnvironment::SeedEnvironment(int Seed)
{
	int32 UniqueSeed = Seed + (EnvironmentId * 1337);
	EnvRandomStream.Initialize(UniqueSeed);

	if (OwnedMatchManager)
	{
		OwnedMatchManager->SetEnvRandomStream(EnvRandomStream);
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] SeedEnvironment: EnvID %d initialized RandomStream with seed %d"), EnvironmentId, UniqueSeed);
}

void ADEScholaEnvironment::RegisterAgent(UDynamicEQSAgentComponent* Agent)
{
	if (Agent && !RegisteredAgents.Contains(Agent))
	{
		RegisteredAgents.Add(Agent);
	}
}

void ADEScholaEnvironment::UnregisterAgent(UDynamicEQSAgentComponent* Agent)
{
	RegisteredAgents.Remove(Agent);
}
