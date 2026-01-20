// ScholaCombatEnvironment.cpp - Schola environment implementation

#include "Schola/ScholaCombatEnvironment.h"
#include "Schola/ScholaAgentComponent.h"
#include "Schola/FollowerAgentTrainer.h"
#include "Core/SimulationManagerGameMode.h"
#include "Core/ScholaGameInstance.h"
#include "Team/FollowerAgentComponent.h"
#include "Team/TeamLeaderComponent.h"
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
}

void AScholaCombatEnvironment::BeginPlay()
{
	// NOTE: Do NOT call Super::BeginPlay() yet - we need to set up first

	// ===== v8.5 VECTORIZED TRAINING: Multi-Environment Support =====
	// Assign unique environment ID based on spawn order
	static int32 GlobalEnvCounter = 0;
	EnvironmentID = GlobalEnvCounter++;

	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [ScholaEnv] Environment #%d initialized: %s                    ║"), EnvironmentID, *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ Vectorized Training: Multiple environments supported          ║"));
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

	// Bind to episode events
	BindEpisodeEvents();

	// Auto-discover agents if enabled
	if (bAutoDiscoverAgents)
	{
		DiscoverAgents();
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

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv #%d] Initialized with %d agents (Training: %s, Port: %d)"),
		EnvironmentID, RegisteredAgents.Num(), bEnableTraining ? TEXT("ON") : TEXT("OFF"), ServerPort);
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv #%d] Base class: %s"), EnvironmentID, *GetClass()->GetSuperClass()->GetName());

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

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Waiting for Python GymConnector to connect on port %d..."), ServerPort);
}

void AScholaCombatEnvironment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv #%d] Environment %s ending"), EnvironmentID, *GetName());

	Super::EndPlay(EndPlayReason);
}

//------------------------------------------------------------------------------
// SCHOLA ENVIRONMENT INTERFACE
//------------------------------------------------------------------------------

void AScholaCombatEnvironment::InitializeEnvironment()
{
	// Called by AAbstractScholaEnvironment::Initialize()
	// Setup any environment-specific initialization here
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv #%d] InitializeEnvironment called on %s"), EnvironmentID, *GetName());

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv #%d] Initializing environment with Schola framework"), EnvironmentID);
}

void AScholaCombatEnvironment::ResetEnvironment()
{
	UE_LOG(LogTemp, Warning, TEXT("================================================================================"));
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET #%d] ResetEnvironment() called on %s"), EnvironmentID, *GetName());
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET #%d] Episode %d starting"), EnvironmentID,
		SimulationManager ? SimulationManager->GetCurrentEpisode() + 1 : -1);
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

		// Re-bind episode events if SimulationManager was recreated
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Re-binding episode events to new SimulationManager..."));
		BindEpisodeEvents();
	}

	// Verify teams are registered
	TArray<int32> AllTeamIDs = SimulationManager->GetAllTeamIDs();
	if (AllTeamIDs.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] Reset blocked - no teams registered"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET] %d teams registered"), AllTeamIDs.Num());

	// CRITICAL FIX: Ensure simulation is running before starting new episode
	// This ensures agents can send observations immediately after reset
	if (!SimulationManager->IsSimulationRunning())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Simulation not running - starting simulation before reset"));
		SimulationManager->StartSimulation();
	}

	// CRITICAL: Clear the persistent termination flag BEFORE starting new episode
	// This flag was set in EndEpisode() and persists during ResetCompletedEnvironments()
	// so agents could report their termination status to Python.
	// Now that reset() has been called, we can clear it.
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET] Clearing bLastEpisodeWasTerminated flag..."));
	SimulationManager->SetLastEpisodeWasTerminated(false);
	SimulationManager->SetLastEpisodeWasTimeout(false);

	// CRITICAL FIX: Re-bind episode events to ensure delegates persist across episodes
	// This prevents the delegate binding loss that causes episode desync
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET] Re-validating episode event bindings..."));
	BindEpisodeEvents();

	// Start new episode (this will reset agents and trigger OnEpisodeStarted event)
	// OnEpisodeStarted will reset LastDecisionTime in all ScholaAgentComponents
	// This ensures Think() can send observations immediately, preventing poll() from blocking
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET] Calling StartNewEpisode()..."));
	SimulationManager->StartNewEpisode();
	UE_LOG(LogTemp, Warning, TEXT("[SCHOLA RESET] StartNewEpisode() completed successfully"));

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
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [ScholaEnv #%d] Registering agents                           ║"), EnvironmentID);
	UE_LOG(LogTemp, Warning, TEXT("║ Environment: %s                                             ║"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ This environment WILL communicate with Python via gRPC       ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));

	if (bAgentsRegistered)
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] Already registered, skipping"));
		return;
	}

	OutAgentTrainerPairs.Empty();

	// ===== 수정: CDO를 완전히 필터링하고 실제 에이전트만 처리 =====
	TArray<UScholaAgentComponent*> ValidComponents;

	// DiscoverAgents()에서 이미 필터링된 RegisteredAgents만 사용
	for (UScholaAgentComponent* Agent : RegisteredAgents)
	{
		if (!Agent || !Agent->GetOwner())
		{
			continue;
		}

		// 이중 체크: CDO와 Archetype 완전히 제외
		if (Agent->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Filtering CDO: %s"), *Agent->GetName());
			continue;
		}

		if (Agent->GetOwner()->HasAnyFlags(RF_ClassDefaultObject))
		{
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Filtering CDO owner: %s"),
				*Agent->GetOwner()->GetName());
			continue;
		}

		ValidComponents.Add(Agent);
	}

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Valid components after filtering: %d"),
		ValidComponents.Num());

	int32 TrainersCreated = 0;
	int32 TrainersFailed = 0;

	// Process valid agents
	for (int32 i = 0; i < ValidComponents.Num(); i++)
	{
		UScholaAgentComponent* Agent = ValidComponents[i];

		if (!Agent->FollowerAgent)
		{
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] Agent %s missing FollowerAgent"), *Agent->GetOwner()->GetName());
			TrainersFailed++;
			continue;
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

		UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] - Validated pawn: %s"), *ControlledPawn->GetName());

		// Spawn trainer (v8.5: Include EnvironmentID for multi-environment uniqueness)
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = FName(*FString::Printf(TEXT("Trainer_Env%d_%s"), EnvironmentID, *Agent->GetOwner()->GetName()));

		UE_LOG(LogTemp, Log, TEXT("[ScholaEnv #%d] - Creating trainer: %s"),
			EnvironmentID, *SpawnParams.Name.ToString());

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
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] - ✓ Created trainer: %s (Pawn: %s)"),
				*Trainer->GetName(), *ControlledPawn->GetName());
		}
		else
		{
			TrainersFailed++;
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] - ✗ Failed to spawn trainer"));
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] === REGISTRATION COMPLETE ==="));
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Trainers Created: %d | Failed: %d"),
		TrainersCreated, TrainersFailed);
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Note: Schola may discover CDO components - Python wrapper handles filtering"));

	bAgentsRegistered = true;
}

void AScholaCombatEnvironment::SetEnvironmentOptions(const TMap<FString, FString>& Options)
{
	// Called by Schola GymConnector to configure environment
	// Can be used to pass settings from Python (e.g., difficulty, map variant)
	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv #%d] SetEnvironmentOptions called on %s with %d options"),
		EnvironmentID, *GetName(), Options.Num());

	for (const auto& Pair : Options)
	{
		UE_LOG(LogTemp, Log, TEXT("  %s = %s"), *Pair.Key, *Pair.Value);
	}
}

void AScholaCombatEnvironment::SeedEnvironment(int Seed)
{
	// Called by Schola GymConnector to seed randomness
	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv #%d] SeedEnvironment called on %s with seed %d"),
		EnvironmentID, *GetName(), Seed);

	FMath::RandInit(Seed);
}

//------------------------------------------------------------------------------
// AGENT DISCOVERY & REGISTRATION
//------------------------------------------------------------------------------

void AScholaCombatEnvironment::DiscoverAgents()
{
	RegisteredAgents.Empty();
	int32 ValidatedCount = 0;
	int32 SkippedCDO = 0;
	int32 SkippedTeamFilter = 0;

	// v8.5 VECTORIZED TRAINING: Team ID-based filtering for multi-environment support
	FString TeamFilterString = TrainingTeamIDs.Num() > 0
		? FString::JoinBy(TrainingTeamIDs, TEXT(", "), [](int32 ID) { return FString::FromInt(ID); })
		: TEXT("ALL TEAMS");

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv #%d] Starting agent discovery..."), EnvironmentID);
	UE_LOG(LogTemp, Warning, TEXT("  - Training Team Filter: [%s]"), *TeamFilterString);
	UE_LOG(LogTemp, Warning, TEXT("  - Team Filtering: %s"), TrainingTeamIDs.Num() > 0 ? TEXT("ENABLED") : TEXT("DISABLED (all teams)"));

	// Iterate through all AFollowerCharacter actors
	for (TActorIterator<AFollowerCharacter> It(GetWorld()); It; ++It)
	{
		AFollowerCharacter* Follower = *It;

		// 1. Validate and filter CDO/Archetypes
		if (!IsValid(Follower) || Follower->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			SkippedCDO++;
			continue;
		}

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

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv #%d] Discovery complete: %d agents registered"), EnvironmentID, ValidatedCount);
	UE_LOG(LogTemp, Warning, TEXT("  - Skipped (CDO): %d"), SkippedCDO);
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

	// Check team filter - Get team ID directly from FollowerAgent's TeamLeader (fixes timing issue)
	if (TrainingTeamIDs.Num() > 0)
	{
		int32 TeamID = -1;

		// Get FollowerAgentComponent to access TeamLeader reference
		UFollowerAgentComponent* FollowerComp = Agent->GetOwner()->FindComponentByClass<UFollowerAgentComponent>();
		if (FollowerComp)
		{
			// Try to get TeamLeader (may be set in editor or via TeamLeaderActor property)
			UTeamLeaderComponent* Leader = FollowerComp->TeamLeader;
			if (!Leader && FollowerComp->TeamLeaderActor)
			{
				// Fallback: Get from TeamLeaderActor if not cached yet
				Leader = FollowerComp->TeamLeaderActor->FindComponentByClass<UTeamLeaderComponent>();
			}

			if (Leader)
			{
				TeamID = Leader->TeamID;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ⚠️ Agent %s has no TeamLeader reference (set TeamLeaderActor in FollowerAgentComponent)"),
					*Agent->GetOwner()->GetName());
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Agent %s detected with TeamID: %d (Training filter: [%s])"),
			*Agent->GetOwner()->GetName(), TeamID,
			*FString::JoinBy(TrainingTeamIDs, TEXT(", "), [](int32 ID) { return FString::FromInt(ID); }));

		if (!TrainingTeamIDs.Contains(TeamID))
		{
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ✗ Skipping agent %s (Team %d not in training list)"),
				*Agent->GetOwner()->GetName(), TeamID);
			return false;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ✓ Agent %s accepted (Team %d is in training list)"),
				*Agent->GetOwner()->GetName(), TeamID);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] No team filter active (accepting all teams)"));
	}

	// Add to registered agents
	RegisteredAgents.AddUnique(Agent);

	// Link agent to this environment
	Agent->ScholaEnvironment = this;

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] ✓ Successfully registered agent: %s"), *Agent->GetOwner()->GetName());
	return true;
}

bool AScholaCombatEnvironment::ValidateAgent(UScholaAgentComponent* Agent) const
{
	if (!Agent || !Agent->GetOwner())
	{
		return false;
	}

	// Check required components
	UFollowerAgentComponent* FollowerComp = Agent->GetOwner()->FindComponentByClass<UFollowerAgentComponent>();
	if (!FollowerComp || !Agent->TacticalObserver || !Agent->RewardProvider)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Agent %s missing required components (FollowerComp, Observer, or RewardProvider)"), *Agent->GetOwner()->GetName());
		return false;
	}


	// Initialize components (only done once per agent)
	Agent->InitializeScholaComponents();

	return true;
}


//------------------------------------------------------------------------------
// EPISODE EVENTS
//------------------------------------------------------------------------------

void AScholaCombatEnvironment::BindEpisodeEvents()
{
	if (!SimulationManager || !IsValid(SimulationManager))
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv #%d] Cannot bind episode events - SimulationManager invalid!"), EnvironmentID);
		return;
	}

	// AddUniqueDynamic ensures no duplicate bindings, but we add explicit validation
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv #%d] Binding episode events to SimulationManager..."), EnvironmentID);
	UE_LOG(LogTemp, Warning, TEXT("  - Environment: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("  - SimulationManager: %s"), *SimulationManager->GetName());
	UE_LOG(LogTemp, Warning, TEXT("  - Current Episode: %d"), SimulationManager->GetCurrentEpisode());

	// Bind to episode lifecycle events (AddUniqueDynamic prevents duplicate bindings)
	SimulationManager->OnEpisodeStarted.AddUniqueDynamic(this, &AScholaCombatEnvironment::OnEpisodeStarted);
	SimulationManager->OnEpisodeEnded.AddUniqueDynamic(this, &AScholaCombatEnvironment::OnEpisodeEnded);

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv #%d] Episode event bindings CONFIRMED ✓"), EnvironmentID);
}

void AScholaCombatEnvironment::OnEpisodeStarted(int32 EpisodeNumber)
{
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [ScholaEnv #%d] Episode %d STARTED                             ║"), EnvironmentID, EpisodeNumber);
	UE_LOG(LogTemp, Warning, TEXT("║ Environment: %s                                               ║"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ SimulationManager Valid: %s                                     ║"), (SimulationManager && IsValid(SimulationManager)) ? TEXT("YES") : TEXT("NO"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════════╝"));

	// CRITICAL: Validate environment state
	if (!SimulationManager || !IsValid(SimulationManager))
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv #%d] CRITICAL: SimulationManager invalid in OnEpisodeStarted! Re-binding..."), EnvironmentID);
		SimulationManager = Cast<ASimulationManagerGameMode>(UGameplayStatics::GetGameMode(this));
		if (SimulationManager)
		{
			BindEpisodeEvents();
		}
	}
}

void AScholaCombatEnvironment::OnEpisodeEnded(const FEpisodeResult& Result)
{
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [ScholaEnv #%d] Episode %d ENDED                               ║"), EnvironmentID, Result.EpisodeNumber);
	UE_LOG(LogTemp, Warning, TEXT("║ Winner: Team %d | Loser: Team %d                               ║"), Result.WinningTeamID, Result.LosingTeamID);
	UE_LOG(LogTemp, Warning, TEXT("║ Duration: %.2fs | Steps: %d                                    ║"), Result.EpisodeDuration, Result.TotalSteps);
	UE_LOG(LogTemp, Warning, TEXT("║ Environment: %s                                                ║"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ SimulationManager Valid: %s                                     ║"), (SimulationManager && IsValid(SimulationManager)) ? TEXT("YES") : TEXT("NO"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════════╝"));

	// CRITICAL: Validate environment state before calling MarkCompleted
	if (!SimulationManager || !IsValid(SimulationManager))
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] CRITICAL: SimulationManager invalid in OnEpisodeEnded!"));
	}

	// CRITICAL FIX v8.0: DO NOT call MarkCompleted() - it triggers Schola auto-reset
	// Instead, agents will detect episode end via bLastEpisodeWasTerminated flag
	// and send termination signals in their observations. Python will call reset() explicitly.
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Episode ended - agents will send termination signals"));
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Waiting for Python to call reset() (NOT auto-resetting)"));

	// NOTE: Removed MarkCompleted() call to prevent Schola auto-reset
	// The environment remains in "Running" state but with bEpisodeEnding=true
	// This allows agents to continue sending observations with done=true flags
	// until Python explicitly calls reset()
}
