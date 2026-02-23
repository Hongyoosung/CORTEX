// ScholaEnvironment.cpp - Schola training environment implementation (v10.2)

#include "Schola/ScholaEnvironment.h"
#include "Schola/Components/ScholaMocAgent.h"
#include "Schola/Components/EpisodeManagerComponent.h"
#include "Schola/Trainers/MocTrainer.h"
#include "Core/MocGameMode.h"
#include "Characters/MocCharacter.h"
#include "Team/SquadManager.h"
#include "Team/TeamManager.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "UObject/UObjectIterator.h"


AScholaEnvironment::AScholaEnvironment(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bAutoDiscoverAgents(true)         // v10.2 default: auto-discover agents
	, bEnableCentralizedPlanning(true)  // v10.2 default: enabled
	, bLogTacticalPlays(false)
{
	PrimaryActorTick.bCanEverTick = false;


	EpisodeManager		= CreateDefaultSubobject<UEpisodeManagerComponent>(TEXT("EpisodeManager"));
}

void AScholaEnvironment::BeginPlay()
{
	// NOTE: Do NOT call Super::BeginPlay() yet - we need to set up first

	// ===== v10.2 MULTI-ENVIRONMENT ARCHITECTURE =====
	// Each actor = 1 physical training environment
	// For 4 parallel environments, spawn 4 actors in the level
	// Schola's CollectEnvironments() will find all actors and create TrainingDefinition


	// Reset flags for new PIE session
	bAgentsRegistered = false;
	bEnvironmentInitialized = false;

	// Get MocGameMode
	GameMode = Cast<AMocGameMode>(UGameplayStatics::GetGameMode(this));
	if (!GameMode)
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] AMocGameMode not found!"));
		return;
	}

	// v10.2: Cache Squad Commander references for centralized planning
	if (bEnableCentralizedPlanning)
	{
		CacheSquadCommanders();
	}

	// NOTE: Do NOT call Super::BeginPlay() here!
	// The ScholaManagerSubsystem will call Initialize() via GymConnector->Init()
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Setup complete. ScholaManagerSubsystem will call Initialize()"));

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Initialized with %d agents"), RegisteredAgents.Num());
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Centralized Planning: %s"),
		bEnableCentralizedPlanning ? TEXT("ENABLED") : TEXT("DISABLED"));
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Squad Commanders Cached: %d"), SquadCommanders.Num());

	// List all registered agents
	for (int32 i = 0; i < RegisteredAgents.Num(); i++)
	{
		UScholaMocAgent* Agent = RegisteredAgents[i];
		if (Agent && Agent->GetOwner())
		{
			UE_LOG(LogTemp, Warning, TEXT("  [%d] %s (Owner: %s)"),
				i, *Agent->GetName(), *Agent->GetOwner()->GetName());
		}
	}
}

void AScholaEnvironment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Environment %s ending"), *GetName());

	// Cleanup spawned trainers
	for (AMocTrainer* Trainer : SpawnedTrainers)
	{
		if (Trainer && IsValid(Trainer))
		{
			Trainer->Destroy();
		}
	}
	SpawnedTrainers.Empty();

	// Clear cached Squad Commanders
	SquadCommanders.Empty();

	Super::EndPlay(EndPlayReason);
}

//------------------------------------------------------------------------------
// SCHOLA ENVIRONMENT INTERFACE
//------------------------------------------------------------------------------

void AScholaEnvironment::InitializeEnvironment()
{
	// Called by AAbstractScholaEnvironment::Initialize()
	// Setup any environment-specific initialization here

	// Guard against duplicate initialization
	if (bEnvironmentInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] InitializeEnvironment() already called, skipping duplicate"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] InitializeEnvironment() called"));

	// v10.2: Auto-discover agents in the world
	if (bAutoDiscoverAgents)
	{
		RegisteredAgents.Empty();

		UWorld* World = GetWorld();
		if (!World)
		{
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] World is null, cannot auto-discover agents"));
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Auto-discovering agents..."));

		// Count total AMocCharacter actors for debugging
		int32 TotalMocCharacters = 0;
		int32 MocCharactersWithScholaAgent = 0;

		// Find all AMocCharacter actors with UScholaMocAgent components
		for (TActorIterator<AMocCharacter> It(World); It; ++It)
		{
			AMocCharacter* MocChar = *It;
			if (!MocChar)
			{
				continue;
			}

			TotalMocCharacters++;

			// Try multiple methods to get ScholaMocAgent component
			UScholaMocAgent* ScholaAgent = MocChar->GetScholaAgent(); // Direct getter
			if (!ScholaAgent)
			{
				ScholaAgent = MocChar->FindComponentByClass<UScholaMocAgent>(); // Search
			}

			if (ScholaAgent)
			{
				MocCharactersWithScholaAgent++;
				RegisteredAgents.Add(ScholaAgent);
				UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2]   ✓ Discovered: %s (Team %d, Component: %s)"),
					*MocChar->GetName(), IMocTeamInterface::Execute_GetTeamID(MocChar), *ScholaAgent->GetName());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2]   ✗ No ScholaAgent: %s (Team %d)"),
					*MocChar->GetName(), IMocTeamInterface::Execute_GetTeamID(MocChar));
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Auto-discovery complete: %d/%d agents found (Total MocCharacters: %d)"),
			RegisteredAgents.Num(), MocCharactersWithScholaAgent, TotalMocCharacters);
	}

	// v10.2: Ensure Squad Commanders are cached
	if (bEnableCentralizedPlanning && SquadCommanders.Num() == 0)
	{
		CacheSquadCommanders();
	}

	// Propagate Phase 1 RL training flag to all TeamManagers (single source of truth)
	// SquadManagers read this flag via their TeamManager reference at runtime
	for (TActorIterator<ATeamManager> It(GetWorld()); It; ++It)
	{
		(*It)->bRLTrainingMode = bPhase1RLTraining;
		UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] Set TeamManager bRLTrainingMode=%s on %s"),
			bPhase1RLTraining ? TEXT("true") : TEXT("false"), *(*It)->GetName());
	}

	bEnvironmentInitialized = true;
}

void AScholaEnvironment::ResetEnvironment()
{
	// Multi-environment architecture: Reset THIS environment only
	// Schola calls this when Python requests reset() for this specific environment
	// EnvID is assigned by Schola based on actor order (0, 1, 2, 3)

	// v10.2: Check for duplicate reset using EpisodeManager
	if (EpisodeManager && EpisodeManager->CheckDuplicateReset(EnvId))
	{
		return;  // Skip duplicate reset
	}

	// Mark training as active (Python has connected and is resetting the environment)
	bTrainingActive = true;

	UE_LOG(LogTemp, Warning, TEXT("================================================================================"));
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v10.2] ResetEnvironment() called on %s (EnvID: %d)"), *GetName(), EnvId);
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v10.2] Architecture: Centralized Commander-Executor"));
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v10.2] Squad Commanders Active: %d"), SquadCommanders.Num());
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v10.2] Registered Agents: %d"), RegisteredAgents.Num());
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v10.2] Training mode: ACTIVE"));
	UE_LOG(LogTemp, Warning, TEXT("================================================================================"));

	if (EpisodeManager)
	{
		// Set EnvID in EpisodeManager
		EpisodeManager->SetEnvironmentID(EnvId);

		// Start new episode (handles episode counter increment internally)
		EpisodeManager->StartNewEpisode(EnvId);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] EpisodeManager is null! Cannot start episode."));
		return;
	}

	// PROPER DELEGATION: Let GameMode orchestrate all game-level resets
	if (GameMode)
	{
		GameMode->ResetMatch();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] GameMode is null! Cannot reset game state."));
	}

	// Reset Squad Commanders (triggers SampleRandomTacticalPlay in RL training mode)
	for (auto& Pair : SquadCommanders)
	{
		if (Pair.Value)
		{
			Pair.Value->Reset();
		}
	}

	OnScholaEnvironmentInitialized_Delegate.Broadcast();

	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v10.2] ResetEnvironment() complete - ready for Python poll()"));
	UE_LOG(LogTemp, Warning, TEXT("================================================================================\n"));
}

void AScholaEnvironment::RegisterAgents(TArray<APawn*>& OutTrainerControlledPawns)
{
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [ScholaEnv v10.2] Registering agents (Commander-Executor)   ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ Actor: %s                                                    ║"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ Centralized Planning: %s                                     ║"),
		bEnableCentralizedPlanning ? TEXT("ENABLED") : TEXT("DISABLED"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));

	// Inference mode: skip trainer spawning entirely.
	// Pawns are controlled by AMocAIController (BT + ONNX) — no Python connection needed.
	if (!bTrainingMode)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] bTrainingMode=false — skipping trainer registration (inference mode)"));
		return;
	}

	if (bAgentsRegistered)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Already registered, returning existing pawns"));
		// Return pawns that already have trainers
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

	// Determine which trainer class to use
	TSubclassOf<AMocTrainer> TrainerClassToUse = TrainerClass;
	if (!TrainerClassToUse)
	{
		TrainerClassToUse = AMocTrainer::StaticClass();
		UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] Using default AMocTrainer class"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Using custom Trainer class: %s"),
			*TrainerClassToUse->GetName());
	}

	int32 TrainersAssigned = 0;
	int32 TrainersSpawned = 0;
	int32 TrainersFailed = 0;

	// Process all registered agents
	for (int32 i = 0; i < RegisteredAgents.Num(); i++)
	{
		UScholaMocAgent* Agent = RegisteredAgents[i];
		if (!Agent || !Agent->GetOwner())
		{
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] - ✗ Invalid agent at index %d"), i);
			TrainersFailed++;
			continue;
		}

		// Get TeamID from MocCharacter
		int32 TeamID = -1;
		AMocCharacter* MocChar = Cast<AMocCharacter>(Agent->GetOwner());
		if (!MocChar)
		{
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] - ✗ Agent owner is not AMocCharacter: %s"),
				*Agent->GetOwner()->GetName());
			TrainersFailed++;
			continue;
		}

		TeamID = IMocTeamInterface::Execute_GetTeamID(MocChar);
		APawn* ControlledPawn = Cast<APawn>(MocChar);

		if (!ControlledPawn)
		{
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] - ✗ Failed to cast MocCharacter to Pawn"));
			TrainersFailed++;
			continue;
		}

		// Check if pawn already has a trainer controller
		AMocTrainer* Trainer = Cast<AMocTrainer>(ControlledPawn->GetController());

		if (Trainer)
		{
			// Trainer already exists (placed in level or spawned by GameMode)
			// v10.2 FIX: Ensure references are initialized even for pre-existing trainers
			Trainer->InitializeMocTrainer(Agent);

			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] - ✓ Found existing Trainer: %s → Agent: %s (Team %d)"),
				*Trainer->GetName(), *MocChar->GetName(), TeamID);
			TrainersAssigned++;
		}
		else if (bAutoSpawnTrainers)
		{
			// Spawn new trainer
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
				// Possess the pawn (Trainer is an AIController)
				Trainer->Possess(ControlledPawn);

				// Initialize trainer with environment and agent info
				Trainer->Initialize(this->EnvId, i, ControlledPawn);

				// v10.2 FIX: Initialize MocTrainer references BEFORE first Think() call
				// Without this, ControlledCharacter is null when ComputeStatus() first runs,
				// causing immediate Completed status → infinite reset loop
				Trainer->InitializeMocTrainer(Agent);

				SpawnedTrainers.Add(Trainer);  // Track for cleanup
				TrainersSpawned++;

				UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] - ✓ Spawned Trainer: %s → Agent: %s (Team %d) [Role: Executor]"),
					*Trainer->GetName(), *MocChar->GetName(), TeamID);
			}
			else
			{
				TrainersFailed++;
				UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] - ✗ Failed to spawn Trainer for agent %s"),
					*MocChar->GetName());
				continue;
			}
		}
		else
		{
			// No trainer and auto-spawn disabled
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] - ✗ Agent %s has no Trainer and bAutoSpawnTrainers=false"),
				*MocChar->GetName());
			TrainersFailed++;
			continue;
		}

		// Add to output list (parent class will extract trainer via GetController())
		OutTrainerControlledPawns.Add(ControlledPawn);
	}

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] === REGISTRATION COMPLETE ==="));
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Trainers Found: %d | Spawned: %d | Failed: %d"),
		TrainersAssigned, TrainersSpawned, TrainersFailed);
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] This actor (EnvID: %d) manages %d executor agents"),
		EnvId, OutTrainerControlledPawns.Num());
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Squad Commanders: %d (centralized planning)"),
		SquadCommanders.Num());

	bAgentsRegistered = true;
}

void AScholaEnvironment::SetEnvironmentOptions(const TMap<FString, FString>& Options)
{
	// Called by Schola GymConnector to configure environment
	// Can be used to pass settings from Python (e.g., difficulty, map variant)
	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] SetEnvironmentOptions called on %s with %d options"),
		*GetName(), Options.Num());

	for (const auto& Pair : Options)
	{
		UE_LOG(LogTemp, Log, TEXT("  %s = %s"), *Pair.Key, *Pair.Value);

		// v10.2: Parse centralized planning option
		if (Pair.Key == TEXT("enable_centralized_planning"))
		{
			bEnableCentralizedPlanning = Pair.Value.ToBool();
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Centralized Planning set to: %s"),
				bEnableCentralizedPlanning ? TEXT("ENABLED") : TEXT("DISABLED"));
		}
		else if (Pair.Key == TEXT("log_tactical_plays"))
		{
			bLogTacticalPlays = Pair.Value.ToBool();
		}
	}
}

void AScholaEnvironment::SeedEnvironment(int Seed)
{
	// Called by Schola GymConnector to seed randomness
	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] SeedEnvironment called on %s with seed %d"),
		*GetName(), Seed);

	FMath::RandInit(Seed);
}

//------------------------------------------------------------------------------
// v10.2 COMMANDER INTEGRATION
//------------------------------------------------------------------------------

USquadManager* AScholaEnvironment::GetSquadCommander(int32 TeamID) const
{
	if (USquadManager* const* FoundCommander = SquadCommanders.Find(TeamID))
	{
		return *FoundCommander;
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

	if (!GetWorld())
	{
		return;
	}

	// Find TeamManager to access Squad Commanders
	for (TActorIterator<ATeamManager> It(GetWorld()); It; ++It)
	{
		ATeamManager* TeamManager = *It;
		if (!TeamManager)
		{
			continue;
		}

		// v10.2: Cache all Squad Commanders (typically Team 0 and Team 1)
		// Team 0 = Red Team, Team 1 = Blue Team
		for (int32 TeamID = 0; TeamID <= 1; TeamID++)
		{
			USquadManager* Commander = TeamManager->GetSquadCommander(TeamID);
			if (Commander)
			{
				SquadCommanders.Add(TeamID, Commander);
				UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] Cached Squad Commander for Team %d: %s"),
					TeamID, *Commander->GetName());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Squad Commander not found for Team %d"),
					TeamID);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] Squad Commanders cached: %d"), SquadCommanders.Num());
}
