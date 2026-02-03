// ScholaCombatEnvironment.cpp - Schola environment implementation

#include "Schola/ScholaCombatEnvironment.h"
#include "Schola/ScholaAgentComponent.h"
#include "Schola/Utils/FollowerAgentTrainer.h"
#include "Schola/Components/EnvRegistryComponent.h"
#include "Schola/Components/EpisodeManagerComponent.h"
// v9.0 PHASE 4: TeamCommsComponent merged into character (unused include removed)
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

	// v9.0 REFACTOR: Create components automatically
	EnvRegistry = CreateDefaultSubobject<UEnvRegistryComponent>(TEXT("EnvRegistry"));
	EpisodeManager = CreateDefaultSubobject<UEpisodeManagerComponent>(TEXT("EpisodeManager"));
}

void AScholaCombatEnvironment::BeginPlay()
{
	// NOTE: Do NOT call Super::BeginPlay() yet - we need to set up first

	// ===== MULTI-ACTOR ARCHITECTURE: Each actor = 1 physical environment =====
	// For 4 physical environments, spawn 4 actors in the level
	// Each actor manages agents from teams specified in EnvRegistry->TeamIDs
	// Schola's CollectEnvironments() will find all actors and create TrainingDefinition

	// v9.0 REFACTOR: Get team IDs from EnvRegistry component
	FString TeamsStr = EnvRegistry && EnvRegistry->TeamIDs.Num() > 0
		? FString::JoinBy(EnvRegistry->TeamIDs, TEXT(", "), [](int32 ID) { return FString::FromInt(ID); })
		: TEXT("ALL TEAMS");

	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [ScholaEnv v9.0] Environment Actor initialized: %s            ║"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ Architecture: Component-based (SRP refactoring)               ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ Managing Teams: [%s]                                          ║"), *TeamsStr);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════════╝"));

	// Reset registration flag for new PIE session
	bAgentsRegistered = false;

	// Get SimulationManager
	SimulationManager = Cast<ASimulationManagerGameMode>(UGameplayStatics::GetGameMode(this));
	if (!SimulationManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] SimulationManagerGameMode not found!"));
		return;
	}

	// v9.0 REFACTOR: Initialize components
	if (EnvRegistry)
	{
		EnvRegistry->Initialize(SimulationManager);
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

	// v9.0 REFACTOR: Get team IDs from EnvRegistry
	FString TeamsStr = EnvRegistry && EnvRegistry->TeamIDs.Num() > 0
		? FString::JoinBy(EnvRegistry->TeamIDs, TEXT(", "), [](int32 ID) { return FString::FromInt(ID); })
		: TEXT("ALL TEAMS");

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v9.0] InitializeEnvironment called on %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v9.0] Component-based architecture: This actor manages teams [%s]"), *TeamsStr);
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v9.0] EnvID will be assigned by ScholaManagerSubsystem (0-based index)"));
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
	FString TeamsStr = EnvRegistry && EnvRegistry->TeamIDs.Num() > 0
		? FString::JoinBy(EnvRegistry->TeamIDs, TEXT(", "), [](int32 ID) { return FString::FromInt(ID); })
		: TEXT("ALL TEAMS");

	UE_LOG(LogTemp, Warning, TEXT("================================================================================"));
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v9.0] ResetEnvironment() called on %s (EnvID: %d)"), *GetName(), EnvId);
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v9.0] Managing teams: [%s]"), *TeamsStr);
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v9.0] Training mode: ACTIVE"));
	UE_LOG(LogTemp, Warning, TEXT("================================================================================"));

	// CRITICAL FIX: Validate SimulationManager before proceeding
	if (!SimulationManager || !IsValid(SimulationManager))
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] SimulationManager is null or invalid! Attempting to reacquire..."));
		SimulationManager = Cast<ASimulationManagerGameMode>(UGameplayStatics::GetGameMode(this));

		if (!SimulationManager)
		{
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] CRITICAL: Failed to reacquire SimulationManager!"));
			return;
		}

		// v9.0 REFACTOR: Re-bind episode events using EpisodeManager
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v9.0] Re-binding episode events to new SimulationManager..."));
		if (EpisodeManager)
		{
			EpisodeManager->BindToSimulationManager(SimulationManager);
		}
	}

	// Verify teams are registered
	TArray<int32> AllTeamIDs = SimulationManager->GetAllTeamIDs();
	if (AllTeamIDs.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] Reset blocked - no teams registered"));
		return;
	}

	if (!SimulationManager->IsSimulationRunning())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Simulation not running - starting simulation before reset"));
		SimulationManager->StartSimulation();
	}

	// v9.0 REFACTOR: Use EpisodeManager to start new episode
	// EnvID from Schola matches the actor index (0-3)
	FString TeamsStrLog = EnvRegistry && EnvRegistry->TeamIDs.Num() > 0
		? FString::JoinBy(EnvRegistry->TeamIDs, TEXT(", "), [](int32 ID) { return FString::FromInt(ID); })
		: TEXT("ALL TEAMS");

	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET v9.0] Resetting environment %d (teams: %s)..."),
		EnvId, *TeamsStrLog);

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

	// Verify simulation is still running after reset
	if (!SimulationManager->IsSimulationRunning())
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] CRITICAL: Simulation stopped after StartNewEpisode()! Restarting..."));
		SimulationManager->StartSimulation();
	}

	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET] ResetEnvironment() complete - ready for Python poll()"));
	UE_LOG(LogTemp, Warning, TEXT("================================================================================\n"));
}

void AScholaCombatEnvironment::InternalRegisterAgents(TArray<FTrainerAgentPair>& OutAgentTrainerPairs)
{
	// v9.0 REFACTOR: Get team IDs from EnvRegistry
	FString TeamsStr = EnvRegistry && EnvRegistry->TeamIDs.Num() > 0
		? FString::JoinBy(EnvRegistry->TeamIDs, TEXT(", "), [](int32 ID) { return FString::FromInt(ID); })
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
	
	DiscoverAgents();

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
	FString TeamsStrReg = EnvRegistry && EnvRegistry->TeamIDs.Num() > 0
		? FString::JoinBy(EnvRegistry->TeamIDs, TEXT(", "), [](int32 ID) { return FString::FromInt(ID); })
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

//------------------------------------------------------------------------------
// AGENT DISCOVERY & REGISTRATION
//------------------------------------------------------------------------------

void AScholaCombatEnvironment::DiscoverAgents()
{
	RegisteredAgents.Empty();
	int32 ValidatedCount = 0;
	int32 SkippedTeamFilter = 0;

	// v9.0 REFACTOR: Get team ID filter from EnvRegistry
	TArray<int32> TeamFilter = EnvRegistry ? EnvRegistry->TeamIDs : TArray<int32>();
	FString TeamFilterString = TeamFilter.Num() > 0
		? FString::JoinBy(TeamFilter, TEXT(", "), [](int32 ID) { return FString::FromInt(ID); })
		: TEXT("ALL TEAMS");

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v9.0] Starting agent discovery..."));
	UE_LOG(LogTemp, Warning, TEXT("  - Training Team Filter: [%s]"), *TeamFilterString);
	UE_LOG(LogTemp, Warning, TEXT("  - Team Filtering: %s"), TeamFilter.Num() > 0 ? TEXT("ENABLED") : TEXT("DISABLED (all teams)"));

	// Iterate through all AFollowerCharacter actors
	for (TActorIterator<AFollowerCharacter> It(GetWorld()); It; ++It)
	{
		AFollowerCharacter* Follower = *It;


		// 2. Extract ScholaAgentComponent
		UScholaAgentComponent* ScholaComp = Follower->FindComponentByClass<UScholaAgentComponent>();

		if (ScholaComp)
		{
			// 3. Register agent (team filtering happens inside RegisterAgent())
			if (RegisterAgent(ScholaComp))
			{
				ValidatedCount++;
			}
			else
			{
				SkippedTeamFilter++;
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Discovery complete: %d agents registered"), ValidatedCount);
	UE_LOG(LogTemp, Warning, TEXT("  - Skipped (Team Filter): %d"), SkippedTeamFilter);
	UE_LOG(LogTemp, Warning, TEXT("  - Total Registered: %d"), RegisteredAgents.Num());
}

bool AScholaCombatEnvironment::RegisterAgent(UScholaAgentComponent* Agent)
{
	if (!Agent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] RegisterAgent failed: Agent is null"));
		return false;
	}

	// Validate agent has required components
	if (!ValidateAgent(Agent))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Agent %s failed validation"), *Agent->GetOwner()->GetName());
		return false;
	}

	// v9.0 REFACTOR: Check team filter using EnvRegistry
	TArray<int32> TeamFilter = EnvRegistry ? EnvRegistry->TeamIDs : TArray<int32>();
	if (TeamFilter.Num() > 0)
	{
		int32 TeamID = -1;

		// v9.0 Phase 4: Use character API instead of FindComponentByClass
		AFollowerCharacter* FollowerChar = Cast<AFollowerCharacter>(Agent->GetOwner());
		if (FollowerChar)
		{
			TeamID = FollowerChar->GetTeamID();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v9.0] ⚠️ Agent %s is not a FollowerCharacter!"),
				*Agent->GetOwner()->GetName());
		}

		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v9.0] Agent %s detected with TeamID: %d (Training filter: [%s])"),
			*Agent->GetOwner()->GetName(), TeamID,
			*FString::JoinBy(TeamFilter, TEXT(", "), [](int32 ID) { return FString::FromInt(ID); }));

		if (!TeamFilter.Contains(TeamID))
		{
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v9.0] ✗ Skipping agent %s (Team %d not in training list)"),
				*Agent->GetOwner()->GetName(), TeamID);
			return false;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v9.0] ✓ Agent %s accepted (Team %d is in training list)"),
				*Agent->GetOwner()->GetName(), TeamID);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv v9.0] No team filter active (accepting all teams)"));
	}

	// Add to registered agents
	RegisteredAgents.AddUnique(Agent);

	// Link agent to this environment
	Agent->ScholaEnvironment = this;

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv v9.0] ✓ Successfully registered agent: %s"), *Agent->GetOwner()->GetName());
	return true;
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

TArray<int32> AScholaCombatEnvironment::GetTrainingTeamIDs() const
{
	return EnvRegistry ? EnvRegistry->TeamIDs : TArray<int32>();
}

//------------------------------------------------------------------------------
// NOTE: v9.0 REFACTOR - Episode event handling moved to EpisodeManagerComponent
// BindEpisodeEvents, OnEpisodeStarted, OnEpisodeEnded removed
//------------------------------------------------------------------------------
