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

// Static singleton reference
AScholaCombatEnvironment* AScholaCombatEnvironment::PrimaryEnvironmentInstance = nullptr;

AScholaCombatEnvironment::AScholaCombatEnvironment(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
}

void AScholaCombatEnvironment::BeginPlay()
{
	// NOTE: Do NOT call Super::BeginPlay() yet - we need to set up first

	// SINGLETON ENFORCEMENT: Only allow ONE active environment per world
	if (PrimaryEnvironmentInstance == nullptr)
	{
		// This is the first instance - make it primary
		PrimaryEnvironmentInstance = this;
		bIsPrimaryEnvironment = true;
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] %s is PRIMARY environment"), *GetName());
	}
	else if (PrimaryEnvironmentInstance != this)
	{
		// This is a duplicate instance - disable it
		bIsPrimaryEnvironment = false;
		bEnableTraining = false;  // Disable training on duplicate

		UE_LOG(LogTemp, Error, TEXT("╔════════════════════════════════════════════════════════════════╗"));
		UE_LOG(LogTemp, Error, TEXT("║ DUPLICATE ScholaCombatEnvironment DETECTED!                   ║"));
		UE_LOG(LogTemp, Error, TEXT("║ Primary: %s                                                   ║"), *PrimaryEnvironmentInstance->GetName());
		UE_LOG(LogTemp, Error, TEXT("║ Duplicate: %s (THIS - DISABLED)                               ║"), *GetName());
		UE_LOG(LogTemp, Error, TEXT("║ Training has been DISABLED on this instance.                  ║"));
		UE_LOG(LogTemp, Error, TEXT("║ ACTION: Remove duplicate ScholaCombatEnvironment from level   ║"));
		UE_LOG(LogTemp, Error, TEXT("╚════════════════════════════════════════════════════════════════╝"));

		// Still call parent to maintain proper lifecycle
		Super::BeginPlay();
		// But do NOT initialize this instance
		return;
	}

	// Check for multiple environment instances (common mistake)
	int32 EnvCount = 0;
	for (TActorIterator<AScholaCombatEnvironment> It(GetWorld()); It; ++It)
	{
		EnvCount++;
	}

	if (EnvCount > 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Found %d environments in level (only primary will be active)"), EnvCount);

		// List all environment instances
		int32 Index = 0;
		for (TActorIterator<AScholaCombatEnvironment> It(GetWorld()); It; ++It)
		{
			AScholaCombatEnvironment* Env = *It;
			UE_LOG(LogTemp, Warning, TEXT("  Environment #%d: %s (%s)"),
				Index++, *Env->GetName(),
				Env == PrimaryEnvironmentInstance ? TEXT("PRIMARY") : TEXT("DISABLED"));
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] %s::BeginPlay() - Environment count: %d, Is Primary: %s"),
		*GetName(), EnvCount, bIsPrimaryEnvironment ? TEXT("YES") : TEXT("NO"));

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
		/*FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, [this]()
			{
				DiscoverAgents();
			}, 0.2f, false);*/

		DiscoverAgents();
	}

	// ===== CRITICAL FIX: Ensure NO CDO in RegisteredAgents before Schola scans =====
	// Schola's base class (AAbstractScholaEnvironment) will scan for components during
	// Super::BeginPlay() → Initialize(). We MUST ensure RegisteredAgents only contains
	// valid non-CDO components BEFORE that scan happens.
	
	TArray<UScholaAgentComponent*> FilteredAgents;
	for (UScholaAgentComponent* Agent : RegisteredAgents)
	{
		// Triple-check: No CDO, no Archetype, must have valid owner
		if (Agent && 
			!Agent->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) &&
			Agent->GetOwner() &&
			!Agent->GetOwner()->HasAnyFlags(RF_ClassDefaultObject))
		{
			FilteredAgents.Add(Agent);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Pre-Super filtering CDO/invalid: %s"),
				Agent ? *Agent->GetName() : TEXT("nullptr"));
		}
	}
	
	RegisteredAgents = FilteredAgents;
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Pre-Super::BeginPlay() agent count: %d (CDOs filtered)"),
		RegisteredAgents.Num());
	
	// NOW call Super::BeginPlay() to initialize Schola base class
	// (moved here to ensure our agents are discovered first)
	// NOTE: Super::BeginPlay() internally calls Initialize(), which triggers InternalRegisterAgents()
	// Do NOT call Initialize() manually again!
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Calling Super::BeginPlay() (this will call Initialize() internally)..."));
	Super::BeginPlay();

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

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Initialized with %d agents (Training: %s, Port: %d)"),
		RegisteredAgents.Num(), bEnableTraining ? TEXT("ON") : TEXT("OFF"), ServerPort);
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

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Waiting for Python GymConnector to connect on port %d..."), ServerPort);
}

void AScholaCombatEnvironment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clear singleton reference if this was the primary instance
	if (PrimaryEnvironmentInstance == this)
	{
		PrimaryEnvironmentInstance = nullptr;
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Primary environment %s ending - clearing singleton"), *GetName());
	}

	Super::EndPlay(EndPlayReason);
}

//------------------------------------------------------------------------------
// SCHOLA ENVIRONMENT INTERFACE
//------------------------------------------------------------------------------

void AScholaCombatEnvironment::InitializeEnvironment()
{
	// Called by AAbstractScholaEnvironment::Initialize()
	// Setup any environment-specific initialization here
	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] InitializeEnvironment called"));
}

void AScholaCombatEnvironment::ResetEnvironment()
{
	// Called by Schola when Python calls env.reset()
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [ScholaEnv] ResetEnvironment called                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════════╝"));

	if (!SimulationManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] ✗ FATAL: SimulationManager not found!"));
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ✓ SimulationManager found"));

	// CRITICAL: Verify teams are registered before calling StartNewEpisode
	// This prevents crashes when reset is called before agents are fully initialized
	TArray<int32> AllTeamIDs = SimulationManager->GetAllTeamIDs();
	if (AllTeamIDs.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("╔════════════════════════════════════════════════════════════════╗"));
		UE_LOG(LogTemp, Error, TEXT("║ RESET BLOCKED - No teams registered!                          ║"));
		UE_LOG(LogTemp, Error, TEXT("╠════════════════════════════════════════════════════════════════╣"));
		UE_LOG(LogTemp, Error, TEXT("║ This happens when env.reset() is called before agents spawn.  ║"));
		UE_LOG(LogTemp, Error, TEXT("║ Solution: Wait for level to finish loading before calling     ║"));
		UE_LOG(LogTemp, Error, TEXT("║ reset(), or add a delay in your Python script.                ║"));
		UE_LOG(LogTemp, Error, TEXT("╚════════════════════════════════════════════════════════════════╝"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ✓ Validation passed - %d teams registered"), AllTeamIDs.Num());

	// Log team details for debugging
	for (int32 TeamID : AllTeamIDs)
	{
		TArray<AActor*> Members = SimulationManager->GetTeamMembers(TeamID);
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv]   - Team %d: %d members"), TeamID, Members.Num());
	}

	// Ensure simulation is running (starts it on first connection/reset)
	if (!SimulationManager->IsSimulationRunning())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] First reset - Starting Simulation"));
		SimulationManager->StartSimulation();
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ✓ Simulation started"));

		// On first start, also trigger initial episode
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Calling StartNewEpisode..."));
		SimulationManager->StartNewEpisode();
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ✓ Episode started successfully"));
	}
	else
	{
		// Subsequent resets: Directly trigger new episode
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Subsequent reset - Restarting episode"));
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Calling StartNewEpisode..."));
		SimulationManager->StartNewEpisode();
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ✓ Episode restarted successfully"));
	}

	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ═══ ResetEnvironment completed successfully ═══"));
}

void AScholaCombatEnvironment::InternalRegisterAgents(TArray<FTrainerAgentPair>& OutAgentTrainerPairs)
{
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

	// 유효한 에이전트만 처리
	for (int32 i = 0; i < ValidComponents.Num(); i++)
	{
		UScholaAgentComponent* Agent = ValidComponents[i];

		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Processing agent %d/%d: %s"),
			i + 1, ValidComponents.Num(), *Agent->GetOwner()->GetName());

		// Initialize if needed
		if (!Agent->FollowerAgent)
		{
			Agent->InitializeScholaComponents();
		}

		if (!Agent->FollowerAgent)
		{
			UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] - ✗ No FollowerAgent after init!"));
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

		// Spawn trainer
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = FName(*FString::Printf(TEXT("Trainer_%s"), *Agent->GetOwner()->GetName()));

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
	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] SetEnvironmentOptions called with %d options"), Options.Num());

	for (const auto& Pair : Options)
	{
		UE_LOG(LogTemp, Log, TEXT("  %s = %s"), *Pair.Key, *Pair.Value);
	}
}

void AScholaCombatEnvironment::SeedEnvironment(int Seed)
{
	// Called by Schola GymConnector to seed randomness
	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] SeedEnvironment called with seed %d"), Seed);
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

	// AFollowerCharacter 클래스로 이터레이터를 제한하여 성능과 정확도를 높입니다.
	for (TActorIterator<AFollowerCharacter> It(GetWorld()); It; ++It)
	{
		AFollowerCharacter* Follower = *It;

		// 1. 유효성 및 CDO 필터링
		if (!IsValid(Follower) || Follower->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			SkippedCDO++;
			continue;
		}

		// 2. 컴포넌트 추출
		UScholaAgentComponent* ScholaComp = Follower->FindComponentByClass<UScholaAgentComponent>();

		if (ScholaComp)
		{
			// 3. 에이전트 등록
			if (RegisterAgent(ScholaComp))
			{
				ValidatedCount++;
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] Discovery Finished. Registered: %d | Skipped CDO: %d"),
		ValidatedCount, SkippedCDO);
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
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Validation failed: Agent or Owner is null"));
		return false;
	}

	FString ActorName = Agent->GetOwner()->GetName();
	bool bIsValid = true;

	// Check for required FollowerAgentComponent
	UFollowerAgentComponent* FollowerComp = Agent->GetOwner()->FindComponentByClass<UFollowerAgentComponent>();
	if (!FollowerComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ✗ %s: Missing FollowerAgentComponent"), *ActorName);
		bIsValid = false;
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] ✓ %s: Has FollowerAgentComponent"), *ActorName);
	}

	// Check for TacticalObserver
	if (!Agent->TacticalObserver)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ✗ %s: Missing TacticalObserver"), *ActorName);
		bIsValid = false;
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] ✓ %s: Has TacticalObserver"), *ActorName);
	}

	// Check for RewardProvider
	if (!Agent->RewardProvider)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ✗ %s: Missing RewardProvider"), *ActorName);
		bIsValid = false;
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] ✓ %s: Has RewardProvider"), *ActorName);
	}

	// Check for TacticalActuator
	if (!Agent->TacticalActuator)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ✗ %s: Missing TacticalActuator"), *ActorName);
		bIsValid = false;
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] ✓ %s: Has TacticalActuator"), *ActorName);
	}

	if (bIsValid)
	{
		UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] ✓ %s: Validation PASSED"), *ActorName);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] ✗ %s: Validation FAILED"), *ActorName);
	}

	Agent->InitializeScholaComponents();

	return bIsValid;
}

//------------------------------------------------------------------------------
// TRAINING SERVER
//------------------------------------------------------------------------------

bool AScholaCombatEnvironment::StartTrainingServer()
{
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] StartTrainingServer is deprecated. ScholaManagerSubsystem handles server startup automatically."));
	return true;
}

void AScholaCombatEnvironment::StopTrainingServer()
{
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] StopTrainingServer is deprecated. ScholaManagerSubsystem handles server shutdown automatically."));
}

//------------------------------------------------------------------------------
// EPISODE EVENTS
//------------------------------------------------------------------------------

void AScholaCombatEnvironment::BindEpisodeEvents()
{
	if (!SimulationManager)
	{
		return;
	}

	// Bind to episode lifecycle events
	SimulationManager->OnEpisodeStarted.AddUniqueDynamic(this, &AScholaCombatEnvironment::OnEpisodeStarted);
	SimulationManager->OnEpisodeEnded.AddUniqueDynamic(this, &AScholaCombatEnvironment::OnEpisodeEnded);

	UE_LOG(LogTemp, Log, TEXT("[ScholaEnv] Bound to episode events"));
}

void AScholaCombatEnvironment::OnEpisodeStarted(int32 EpisodeNumber)
{
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Episode %d started - Resetting agents"), EpisodeNumber);

	// Reset all registered agents
	for (UScholaAgentComponent* Agent : RegisteredAgents)
	{
		if (Agent)
		{
			Agent->ResetEpisode();
		}
	}

	// NOTE: Do NOT call Reset() here - it creates circular dependency
	// ResetEnvironment() already triggers StartNewEpisode() which broadcasts this event
}

void AScholaCombatEnvironment::OnEpisodeEnded(const FEpisodeResult& Result)
{
	UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Episode %d ended - Winner: Team %d, Duration: %.2fs, Steps: %d"),
		Result.EpisodeNumber, Result.WinningTeamID, Result.EpisodeDuration, Result.TotalSteps);

	// Mark environment as completed (triggers Schola to collect final experiences)
	MarkCompleted();
}
