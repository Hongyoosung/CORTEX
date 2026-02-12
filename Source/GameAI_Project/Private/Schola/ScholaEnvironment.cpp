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


	// Reset registration flag for new PIE session
	bAgentsRegistered = false;

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

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v10.2] InitializeEnvironment() called"));

	// v10.2: Ensure Squad Commanders are cached
	if (bEnableCentralizedPlanning && SquadCommanders.Num() == 0)
	{
		CacheSquadCommanders();
	}
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

	OnScholaEnvironmentInitialized_Delegate.Broadcast();

	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v10.2] ResetEnvironment() complete - ready for Python poll()"));
	UE_LOG(LogTemp, Warning, TEXT("================================================================================\n"));
}

void AScholaEnvironment::InternalRegisterAgents(TArray<FTrainerAgentPair>& OutAgentTrainerPairs)
{
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [ScholaEnv v10.2] Registering agents (Commander-Executor)   ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ Actor: %s                                                    ║"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ Centralized Planning: %s                                     ║"),
		bEnableCentralizedPlanning ? TEXT("ENABLED") : TEXT("DISABLED"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));

	if (bAgentsRegistered)
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] Already registered, skipping"));
		return;
	}

	OutAgentTrainerPairs.Empty();

	int32 TrainersCreated = 0;
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
		if (MocChar)
		{
			TeamID = MocChar->GetTeamID();
		}

		// Validate pawn (MocCharacter is the pawn in v10.2)
		APawn* ControlledPawn = Cast<APawn>(Agent->GetOwner());
		if (!ControlledPawn || !ControlledPawn->IsValidLowLevel())
		{
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] - ✗ Invalid/NULL pawn for agent %s!"),
				*Agent->GetOwner()->GetName());
			TrainersFailed++;
			continue;
		}

		// Spawn MocTrainer
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = FName(*FString::Printf(TEXT("MocTrainer_Team%d_%s"),
			TeamID, *Agent->GetOwner()->GetName()));

		AMocTrainer* Trainer = GetWorld()->SpawnActor<AMocTrainer>(
			AMocTrainer::StaticClass(),
			Agent->GetOwner()->GetActorLocation(),
			FRotator::ZeroRotator,
			SpawnParams
		);

		if (Trainer)
		{
			APawn* ControllerdPawn = Cast<APawn>(Agent);

			if (!ControllerdPawn)
			{
				UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] Failed Cast to APawn"));
				return;
			}

			Trainer->Initialize(this->EnvId, i, ControllerdPawn);
			FTrainerAgentPair Pair(ControlledPawn, Trainer);
			OutAgentTrainerPairs.Add(Pair);
			TrainersCreated++;
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] - ✓ Agent: %s → Team %d (Trainer: %s) [Role: Executor]"),
				*Agent->GetOwner()->GetName(), TeamID, *Trainer->GetName());
		}
		else
		{
			TrainersFailed++;
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] - ✗ Failed to spawn MocTrainer"));
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] === REGISTRATION COMPLETE ==="));
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] Trainers Created: %d | Failed: %d"),
		TrainersCreated, TrainersFailed);
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v10.2] This actor (EnvID: %d) manages %d executor agents"),
		EnvId, TrainersCreated);
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

const ASquadManager* AScholaEnvironment::GetSquadCommander(int32 TeamID) const
{
	if (const ASquadManager* const* FoundCommander = SquadCommanders.Find(TeamID))
	{
		return *FoundCommander;
	}

	return nullptr;
}

TArray<ASquadManager*> AScholaEnvironment::GetAllSquadCommanders() const
{
	TArray<ASquadManager*> Commanders;
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
			ASquadManager* Commander = TeamManager->GetSquadCommander(TeamID);
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


bool AScholaEnvironment::ValidateAgent(UScholaMocAgent* Agent) const
{
	if (!Agent || !Agent->GetOwner())
	{
		return false;
	}

	// v10.2: Validate that owner is AMocCharacter
	AMocCharacter* MocChar = Cast<AMocCharacter>(Agent->GetOwner());
	if (!MocChar)
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v10.2] Agent owner is not AMocCharacter: %s"),
			*Agent->GetOwner()->GetName());
		return false;
	}

	return true;
}
