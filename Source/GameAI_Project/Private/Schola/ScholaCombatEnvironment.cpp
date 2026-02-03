// ScholaCombatEnvironment.cpp - Schola environment implementation

#include "Schola/ScholaCombatEnvironment.h"
#include "Schola/ScholaAgentComponent.h"
#include "Schola/Utils/FollowerAgentTrainer.h"
#include "Schola/Components/EnvRegistryComponent.h"
#include "Schola/Components/EpisodeManagerComponent.h"
#include "Core/SimulationManagerGameMode.h"
#include "Core/ScholaGameInstance.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "Communicator/CommunicationManager.h"
#include "Subsystem/ScholaManagerSubsystem.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/DefaultPawn.h"
#include "UObject/UObjectIterator.h"
#include "Actor/FollowerCharacter.h"

AScholaCombatEnvironment::AScholaCombatEnvironment(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;


	EnvRegistry			= CreateDefaultSubobject<UEnvRegistryComponent>(TEXT("EnvRegistry"));
	EpisodeManager		= CreateDefaultSubobject<UEpisodeManagerComponent>(TEXT("EpisodeManager"));
}

void AScholaCombatEnvironment::BeginPlay()
{
	// NOTE: Do NOT call Super::BeginPlay() yet - we need to set up first

	// ===== MULTI-ACTOR ARCHITECTURE: Each actor = 1 physical environment =====
	// For 4 physical environments, spawn 4 actors in the level
	// Each actor manages agents from teams specified in EnvRegistry->TeamIDs
	// Schola's CollectEnvironments() will find all actors and create TrainingDefinition


	// Reset registration flag for new PIE session
	bAgentsRegistered = false;

	// Get SimulationManager
	SimulationManager = Cast<ASimulationManagerGameMode>(UGameplayStatics::GetGameMode(this));
	if (!SimulationManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] SimulationManagerGameMode not found!"));
		return;
	}



	if (EpisodeManager)
	{
		EpisodeManager->BindToSimulationManager(SimulationManager);
	}

	// NOTE: Do NOT call Super::BeginPlay() here!
	// The ScholaManagerSubsystem will call Initialize() via GymConnector->Init()
	// Calling it here would cause duplicate initialization
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Setup complete. ScholaManagerSubsystem will call Initialize() via GymConnector"));

	// Note: ScholaManagerSubsystem automatically handles server startup and agent registration.
	// We do not need to manually start the server here.

	// Debug: Check if environment is properly initialized
	if (GetWorld())
	{
		UGameInstance* GameInstance = GetWorld()->GetGameInstance();
		if (GameInstance)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] GameInstance: %s"), *GameInstance->GetClass()->GetName());
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Initialized with %d agents"), RegisteredAgents.Num());
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Base class: %s"), *GetClass()->GetSuperClass()->GetName());

	// List all registered agents
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Registered agents:"));
	for (int32 i = 0; i < RegisteredAgents.Num(); i++)
	{
		UScholaAgentComponent* Agent = RegisteredAgents[i];
		if (Agent && Agent->GetOwner())
		{
			UE_LOG(LogTemp, Warning, TEXT("  [%d] %s (ActorName: %s)"),
				i, *Agent->GetName(), *Agent->GetOwner()->GetName());
		}
	}
}

void AScholaCombatEnvironment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Environment %s ending"), *GetName());

	Super::EndPlay(EndPlayReason);
}

//------------------------------------------------------------------------------
// SCHOLA ENVIRONMENT INTERFACE
//------------------------------------------------------------------------------

void AScholaCombatEnvironment::InitializeEnvironment()
{
	// Called by AAbstractScholaEnvironment::Initialize()
	// Setup any environment-specific initialization here
}

void AScholaCombatEnvironment::ResetEnvironment()
{
	// Multi-actor architecture: Reset THIS environment only
	// Schola calls this when Python requests reset() for this specific environment
	// EnvID is assigned by Schola based on actor order (0, 1, 2, 3)

	// v9.0 REFACTOR: Check for duplicate reset using EpisodeManager
	if (EpisodeManager && EpisodeManager->CheckDuplicateReset(EnvId))
	{
		return;  // Skip duplicate reset
	}

	// Mark training as active (Python has connected and is resetting the environment)
	bTrainingActive = true;

	// v9.0 REFACTOR: Get team IDs from EnvRegistry
	FString TeamsStr = EnvRegistry && EnvRegistry->GetRegisteredTeams().Num() > 0
		? FString::JoinBy(EnvRegistry->GetRegisteredTeams(), TEXT(", "), [](int32 ID) { return FString::FromInt(ID); })
		: TEXT("ALL TEAMS");

	UE_LOG(LogTemp, Warning, TEXT("================================================================================"));
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v9.0] ResetEnvironment() called on %s (EnvID: %d)"), *GetName(), EnvId);
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v9.0] Managing teams: [%s]"), *TeamsStr);
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v9.0] Training mode: ACTIVE"));
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
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv v9.0] EpisodeManager is null! Cannot start episode."));
		return;
	}


	OnScholaEnvironmentInitialized_Delegate.Broadcast();

	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET] ResetEnvironment() complete - ready for Python poll()"));
	UE_LOG(LogTemp, Warning, TEXT("================================================================================\n"));
}

void AScholaCombatEnvironment::InternalRegisterAgents(TArray<FTrainerAgentPair>& OutAgentTrainerPairs)
{
	FString TeamsStr = EnvRegistry && EnvRegistry->GetRegisteredTeams().Num() > 0
		? FString::JoinBy(EnvRegistry->GetRegisteredTeams(), TEXT(", "), [](int32 ID) { return FString::FromInt(ID); })
		: TEXT("ALL TEAMS");

	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [ScholaEnv v9.0] Registering agents for environment actor   ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ Actor: %s                                                    ║"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ Managing Teams: [%s]                                         ║"), *TeamsStr);
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));

	if (bAgentsRegistered)
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] Already registered, skipping"));
		return;
	}
	
	OutAgentTrainerPairs.Empty();

	int32 TrainersCreated = 0;
	int32 TrainersFailed = 0;

	// Process valid agents (only agents from TrainingTeamIDs)
	for (int32 i = 0; i < RegisteredAgents.Num(); i++)
	{
		UScholaAgentComponent* Agent = RegisteredAgents[i];

		if (!Agent->FollowerAgent)
		{
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] Agent %s missing FollowerAgent"), *Agent->GetOwner()->GetName());
			TrainersFailed++;
			continue;
		}

		// Get TeamID for logging - v9.0 Phase 4: Use character API
		int32 TeamID = -1;
		AFollowerCharacter* FollowerChar = Cast<AFollowerCharacter>(Agent->GetOwner());
		if (FollowerChar)
		{
			TeamID = FollowerChar->GetTeamID();
		}

		// Validate pawn before creating trainer (critical for Schola)
		APawn* ControlledPawn = Agent->GetControlledPawn();
		if (!ControlledPawn || !ControlledPawn->IsValidLowLevel())
		{
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] - ✗ Invalid/NULL pawn for agent %s!"),
				*Agent->GetOwner()->GetName());
			TrainersFailed++;
			continue;
		}

		if (ValidateAgent(Agent))
		{
			Agent->ScholaEnvironment = this;
		}
		

		// Spawn trainer (EnvID will be assigned by Schola based on actor order)
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = FName(*FString::Printf(TEXT("Trainer_Team%d_%s"),
			TeamID, *Agent->GetOwner()->GetName()));

		AFollowerAgentTrainer* Trainer = GetWorld()->SpawnActor<AFollowerAgentTrainer>(
			AFollowerAgentTrainer::StaticClass(),
			Agent->GetOwner()->GetActorLocation(),
			FRotator::ZeroRotator,
			SpawnParams
		);
		
		if (Trainer)
		{
			Trainer->Initialize(Agent);
			FTrainerAgentPair Pair(ControlledPawn, Trainer);
			OutAgentTrainerPairs.Add(Pair);
			TrainersCreated++;
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] - ✓ Agent: %s → Team %d (Trainer: %s)"),
				*Agent->GetOwner()->GetName(), TeamID, *Trainer->GetName());
		}
		else
		{
			TrainersFailed++;
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] - ✗ Failed to spawn trainer"));
		}
	}

	// v9.0 REFACTOR: Get team IDs from EnvRegistry
	FString TeamsStrReg = EnvRegistry && EnvRegistry->GetRegisteredTeams().Num() > 0
		? FString::JoinBy(EnvRegistry->GetRegisteredTeams(), TEXT(", "), [](int32 ID) { return FString::FromInt(ID); })
		: TEXT("ALL TEAMS");

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v9.0] === REGISTRATION COMPLETE ==="));
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v9.0] Trainers Created: %d | Failed: %d"),
		TrainersCreated, TrainersFailed);
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v9.0] This actor (EnvID: %d) manages %d agents from teams [%s]"),
		EnvId, TrainersCreated, *TeamsStrReg);

	bAgentsRegistered = true;
}

void AScholaCombatEnvironment::SetEnvironmentOptions(const TMap<FString, FString>& Options)
{
	// Called by Schola GymConnector to configure environment
	// Can be used to pass settings from Python (e.g., difficulty, map variant)
	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] SetEnvironmentOptions called on %s with %d options"),
		*GetName(), Options.Num());

	for (const auto& Pair : Options)
	{
		UE_LOG(LogTemp, Log, TEXT("  %s = %s"), *Pair.Key, *Pair.Value);
	}
}

void AScholaCombatEnvironment::SeedEnvironment(int Seed)
{
	// Called by Schola GymConnector to seed randomness
	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] SeedEnvironment called on %s with seed %d"),
		*GetName(), Seed);

	FMath::RandInit(Seed);
}

bool AScholaCombatEnvironment::RegisterTeam(int32 TeamID)
{
	if (!EnvRegistry)
	{
		return false;
	}

	EnvRegistry->RegisterTeam(TeamID);
}

bool AScholaCombatEnvironment::RegisterObjective(AObjectiveActor* Objective)
{
	if (!EnvRegistry)
	{
		return false;
	}

	EnvRegistry->RegisterObjectiveActor(Objective);
}

bool AScholaCombatEnvironment::UnRegisterTeam(int32 TeamID)
{
	if (!EnvRegistry)
	{
		return false;
	}

	return EnvRegistry->UnRegisterTeam(TeamID);
}

bool AScholaCombatEnvironment::UnRegisterObjective(AObjectiveActor* Objective)
{
	if (!EnvRegistry)
	{
		return false;
	}

	return EnvRegistry->UnRegisterObjectiveActor(Objective);
}


bool AScholaCombatEnvironment::ValidateAgent(UScholaAgentComponent* Agent) const
{
	if (!Agent || !Agent->GetOwner())
	{
		return false;
	}

	// Initialize components (only done once per agent)
	Agent->InitializeScholaComponents();

	return true;
}