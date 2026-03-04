// ScholaEnvironment.cpp - Schola training environment implementation (v10.2 Parallel Isolated)

#include "Schola/ScholaEnvironment.h"
#include "Schola/Components/ScholaMocAgent.h"
#include "Schola/Components/EpisodeManagerComponent.h"
#include "Schola/Trainers/MocTrainer.h"
#include "Core/MocGameMode.h"
#include "Characters/MocCharacter.h"
#include "Team/SquadManager.h"
#include "Team/TeamManager.h"
#include "Actors/SpawnArea.h"
#include "Actors/PickupBase.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"


AScholaEnvironment::AScholaEnvironment(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bAutoDiscoverAgents(true)
	, bEnableCentralizedPlanning(true)
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

	// Pass SpawnArea refs to OwnedTeamManager
	if (OwnedTeamManager)
	{
		OwnedTeamManager->EnvID = ScholaEnvID;
		if (RedSpawnArea)
		{
			OwnedTeamManager->RedSpawnArea = RedSpawnArea;
		}
		if (BlueSpawnArea)
		{
			OwnedTeamManager->BlueSpawnArea = BlueSpawnArea;
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] OwnedTeamManager not assigned for %s!"), *GetName());
	}

	// Set EnvID on owned capture points
	for (ACapturePoint* CP : OwnedCapturePoints)
	{
		if (CP)
		{
			CP->EnvID = ScholaEnvID;
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] CP: %s EnvID=%d PointID=%d"),
				*CP->GetName(), CP->EnvID, (int32)CP->PointID);
		}
			
	}

	// Set EnvID on owned pickups
	for (APickupBase* Pickup : OwnedPickups)
	{
		if (Pickup)
		{
			Pickup->EnvID = ScholaEnvID;
		}
	}

	// Cache Squad Commanders
	if (bEnableCentralizedPlanning)
	{
		CacheSquadCommanders();
	}

	// Subscribe to events
	SubscribeToEvents();

	// Auto-start match
	if (bAutoStartMatch)
	{
		StartMatch();
	}

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] %s setup complete (EnvID: %d, CPs: %d, TM: %s)"),
		*GetName(), ScholaEnvID, OwnedCapturePoints.Num(),
		OwnedTeamManager ? *OwnedTeamManager->GetName() : TEXT("NULL"));
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
	SquadCommanders.Empty();

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
	UpdatePassiveIncome(DeltaTime);
	CheckWinConditions();
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

	if (OwnedTeamManager)
	{
		OwnedTeamManager->SpawnTeams();
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
}

void AScholaEnvironment::AddTeamScore(int32 TeamID, int32 Amount, const FString& Reason)
{
	if (!OwnedTeamManager || (TeamID != 0 && TeamID != 1))
	{
		return;
	}

	OwnedTeamManager->AddTeamScore(TeamID, Amount);
	int32 NewScore = GetTeamScore(TeamID);
	OnEnvScoreUpdated.Broadcast(TeamID, NewScore, Reason);
}

int32 AScholaEnvironment::GetTeamScore(int32 TeamID) const
{
	return OwnedTeamManager ? OwnedTeamManager->GetTeamScore(TeamID) : 0;
}

//------------------------------------------------------------------------------
// GAME LOGIC
//------------------------------------------------------------------------------

void AScholaEnvironment::UpdatePassiveIncome(float DeltaTime)
{
	int32 RedOwned = 0;
	int32 BlueOwned = 0;
	CountOwnedPoints(RedOwned, BlueOwned);

	PassiveIncomeAccumulator += PassiveIncomeRate * DeltaTime;

	if (PassiveIncomeAccumulator >= 1.0f)
	{
		int32 Points = FMath::FloorToInt(PassiveIncomeAccumulator);
		PassiveIncomeAccumulator -= Points;

		if (RedOwned > 0)
		{
			AddTeamScore(0, RedOwned * Points, TEXT("Passive Income"));
		}
		if (BlueOwned > 0)
		{
			AddTeamScore(1, BlueOwned * Points, TEXT("Passive Income"));
		}
	}
}

void AScholaEnvironment::CheckWinConditions()
{
	int32 RedScore = GetTeamScore(0);
	int32 BlueScore = GetTeamScore(1);

	if (RedScore >= WinningScore)
	{
		EndMatch(EMocMatchState::RedTeamWon);
		return;
	}
	if (BlueScore >= WinningScore)
	{
		EndMatch(EMocMatchState::BlueTeamWon);
		return;
	}
	if (MatchTimer >= MaxMatchDuration)
	{
		EndMatch(EMocMatchState::TimeExpired);
	}
}

void AScholaEnvironment::CountOwnedPoints(int32& OutRedOwned, int32& OutBlueOwned) const
{
	OutRedOwned = 0;
	OutBlueOwned = 0;

	for (ACapturePoint* Point : OwnedCapturePoints)
	{
		if (!Point) continue;
		ECapturePointOwnership Ownership = Point->GetOwnership();
		if (Ownership == ECapturePointOwnership::RedTeam)
		{
			OutRedOwned++;
		}
		else if (Ownership == ECapturePointOwnership::BlueTeam)
		{
			OutBlueOwned++;
		}
	}
}

//------------------------------------------------------------------------------
// EVENT HANDLERS
//------------------------------------------------------------------------------

void AScholaEnvironment::OnPointCaptured(ECapturePointID PointID, ECapturePointOwnership PreviousOwner, ECapturePointOwnership NewOwner)
{
	if (NewOwner == ECapturePointOwnership::RedTeam)
	{
		AddTeamScore(0, CaptureReward, TEXT("Point Capture"));
	}
	else if (NewOwner == ECapturePointOwnership::BlueTeam)
	{
		AddTeamScore(1, CaptureReward, TEXT("Point Capture"));
	}
}

void AScholaEnvironment::OnAgentKilled(int32 VictimTeamID, int32 KillerTeamID, AMocCharacter* Victim)
{
	if (KillerTeamID != -1 && KillerTeamID != VictimTeamID)
	{
		AddTeamScore(KillerTeamID, KillPoints, TEXT("Kill"));
	}
}

void AScholaEnvironment::SubscribeToEvents()
{
	// Subscribe to owned capture points
	for (ACapturePoint* Point : OwnedCapturePoints)
	{
		if (Point)
		{
			Point->OnPointCaptured.AddDynamic(this, &AScholaEnvironment::OnPointCaptured);
		}
	}

	// Subscribe to owned TeamManager events
	if (OwnedTeamManager)
	{
		OwnedTeamManager->OnAgentKilled.AddDynamic(this, &AScholaEnvironment::OnAgentKilled);
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

	// v10.2: Discover agents from OwnedTeamManager (scoped, not world-wide)
	if (bAutoDiscoverAgents && OwnedTeamManager)
	{
		RegisteredAgents.Empty();

		// Get agents from both teams via the owned TeamManager
		for (int32 TeamID = 0; TeamID <= 1; TeamID++)
		{
			TArray<AMocCharacter*> TeamAgents = OwnedTeamManager->GetTeamAgents(TeamID);
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
						*MocChar->GetName(), TeamID, MocChar->EnvID);
				}
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Scoped discovery complete: %d agents for EnvID %d"),
			RegisteredAgents.Num(), ScholaEnvID);
	}

	// Ensure Squad Commanders are cached
	if (bEnableCentralizedPlanning && SquadCommanders.Num() == 0)
	{
		CacheSquadCommanders();
	}

	// Propagate Phase 1 RL training flag
	if (OwnedTeamManager)
	{
		OwnedTeamManager->bRLTrainingMode = bPhase1RLTraining;
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

	// Reset TeamManager (teams, agents, squad commanders)
	if (OwnedTeamManager)
	{
		OwnedTeamManager->ResetTeams();
	}

	// Reset owned capture points
	for (ACapturePoint* Point : OwnedCapturePoints)
	{
		if (Point)
		{
			Point->ResetPoint();
		}
	}

	// Reset owned pickups
	for (APickupBase* Pickup : OwnedPickups)
	{
		if (Pickup)
		{
			Pickup->Reset();
		}
	}

	// Reset Squad Commanders
	for (auto& Pair : SquadCommanders)
	{
		if (Pair.Value)
		{
			Pair.Value->Reset();
		}
	}

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

void AScholaEnvironment::SetEnvironmentOptions(const TMap<FString, FString>& Options)
{
	for (const auto& Pair : Options)
	{
		if (Pair.Key == TEXT("enable_centralized_planning"))
		{
			bEnableCentralizedPlanning = Pair.Value.ToBool();
		}
		else if (Pair.Key == TEXT("log_tactical_plays"))
		{
			bLogTacticalPlays = Pair.Value.ToBool();
		}
	}
}

void AScholaEnvironment::SeedEnvironment(int Seed)
{
	int32 UniqueSeed = Seed + (ScholaEnvID * 1337);
	EnvRandomStream.Initialize(UniqueSeed);

	if (OwnedTeamManager)
	{
		OwnedTeamManager->SetEnvRandomStream(EnvRandomStream);
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] SeedEnvironment: EnvID %d initialized RandomStream with seed %d"), ScholaEnvID, UniqueSeed);
}

//------------------------------------------------------------------------------
// v10.2 COMMANDER INTEGRATION
//------------------------------------------------------------------------------

USquadManager* AScholaEnvironment::GetSquadCommander(int32 TeamID) const
{
	if (USquadManager* const* Found = SquadCommanders.Find(TeamID))
	{
		return *Found;
	}
	return nullptr;
}

TArray<USquadManager*> AScholaEnvironment::GetAllSquadCommanders() const
{
	TArray<USquadManager*> Commanders;
	SquadCommanders.GenerateValueArray(Commanders);
	return Commanders;
}

void AScholaEnvironment::CacheSquadCommanders()
{
	SquadCommanders.Empty();

	if (!OwnedTeamManager)
	{
		return;
	}

	for (int32 TeamID = 0; TeamID <= 1; TeamID++)
	{
		USquadManager* Commander = OwnedTeamManager->GetSquadCommander(TeamID);
		if (Commander)
		{
			SquadCommanders.Add(TeamID, Commander);

			// Bind scoped capture points and environment reference (replaces AMocGameMode queries)
			Commander->BindCapturePoints(OwnedCapturePoints);
			Commander->SetScholaEnvironment(this);

			UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] Cached Squad Commander for Team %d"), TeamID);
		}
	}
}
