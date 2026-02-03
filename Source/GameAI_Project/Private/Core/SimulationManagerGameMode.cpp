#include "Core/SimulationManagerGameMode.h"
#include "Core/ScholaGameInstance.h"
#include "Actor/LeaderCharacter.h"
#include "Team/ObjectiveActor.h"
#include "Combat/Components/HealthComponent.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "Schola/ScholaCombatEnvironment.h"
#include "Schola/Components/EnvRegistryComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"


ASimulationManagerGameMode::ASimulationManagerGameMode()
	: Super()
	, bAutoStartSimulation(false)
	, bDrawDebugInfo(false)
	, DebugDrawInterval(1.0f)
	, TotalAgents(0)
	, bSimulationRunning(false)
	, SimulationStartTime(0.0f)
	, LastDebugDrawTime(0.0f)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f; // 10 Hz
}

void ASimulationManagerGameMode::BeginPlay()
{
	Super::BeginPlay();


	DiscoverAndRegisterEnvironments();



	if (bAutoStartSimulation)
	{
		StartSimulation();
	}
}

void ASimulationManagerGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

//------------------------------------------------------------------------------
// TEAM REGISTRATION
//------------------------------------------------------------------------------

bool ASimulationManagerGameMode::RegisterTeam(FTeamInfo TeamInfo)
{
	AScholaCombatEnvironment** ScholaEnvPtr = ScholaEnvironmentsMap.Find(TeamInfo.EnvID);

	if (!ScholaEnvPtr || !*ScholaEnvPtr)
	{
		return false;
	}

	TotalAgents += TeamInfo.AgnetCount;

	AScholaCombatEnvironment* ScholaEnv = *ScholaEnvPtr;

	return ScholaEnv->RegisterTeam(TeamInfo.TeamID);
}


bool ASimulationManagerGameMode::UnRegisterTeam(FTeamInfo TeamInfo)
{
	AScholaCombatEnvironment** ScholaEnvPtr = ScholaEnvironmentsMap.Find(TeamInfo.EnvID);

	if (!ScholaEnvPtr || !*ScholaEnvPtr)
	{
		return false;
	}

	TotalAgents = FMath::Max(0, TotalAgents - TeamInfo.AgnetCount);

	AScholaCombatEnvironment* ScholaEnv = *ScholaEnvPtr;

	return ScholaEnv->UnRegisterTeam(TeamInfo.TeamID);
}


void ASimulationManagerGameMode::RegisterObjective(AObjectiveActor* Objective)
{
	if (!Objective)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SimulationManager] Cannot register null objective"));
		return;
	}

	int32 EnvID = Objective->GetTeamInfo().EnvID;
	int32 TeamID = Objective->GetTeamInfo().TeamID;

	AScholaCombatEnvironment** ScholaEnvPtr = ScholaEnvironmentsMap.Find(EnvID);

	if (!ScholaEnvPtr || !*ScholaEnvPtr)
	{
		UE_LOG(LogTemp, Error, TEXT("[SimulationManager] Cannot register objective '%s' - Environment %d not found"),
			*Objective->GetName(), EnvID);

		return;
	}

	AScholaCombatEnvironment* ScholaEnv = *ScholaEnvPtr;
	ScholaEnv->RegisterObjective(Objective);


	UE_LOG(LogTemp, Log, TEXT("[SimulationManager] Objective '%s' Registered. Env: '%s', Team: '%s'"),
		*Objective->GetName(), EnvID, TeamID);
}


void ASimulationManagerGameMode::UnRegisterObjective(AObjectiveActor* Objective)
{
	if (!Objective)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SimulationManager] Cannot unregister null objective"));
		return;
	}

	int32 EnvID = Objective->GetTeamInfo().EnvID;
	AScholaCombatEnvironment** ScholaEnvPtr = ScholaEnvironmentsMap.Find(EnvID);

	if (!ScholaEnvPtr || !*ScholaEnvPtr)
	{
		UE_LOG(LogTemp, Error, TEXT("[SimulationManager] Cannot unregister objective '%s' - Environment %d not found"),
			*Objective->GetName(), EnvID);
		return;
	}

	AScholaCombatEnvironment* ScholaEnv = *ScholaEnvPtr;
	ScholaEnv->UnRegisterObjective(Objective);
}

void ASimulationManagerGameMode::GetObjectives(int32 TeamID, AObjectiveActor*& Friendly, AObjectiveActor*& Hostile) const
{
	// TeamID로 Environment 찾기
	AScholaCombatEnvironment* ScholaEnv = nullptr;

	for (const auto& Pair : ScholaEnvironmentsMap)
	{
		AScholaCombatEnvironment* Env = Pair.Value;
		if (Env && Env->EnvRegistry)
		{
			if (Env->EnvRegistry->IsTeamRegistered(TeamID))
			{
				ScholaEnv = Env;
				break;
			}
		}
	}

	if (!ScholaEnv || !ScholaEnv->EnvRegistry)
	{
		UE_LOG(LogTemp, Warning, TEXT("GetObjectives: Environment not found for Team %d"), TeamID);
		Friendly = nullptr;
		Hostile = nullptr;
		return;
	}

	// EnvRegistryComponent를 통해 Objective 획득
	Friendly = ScholaEnv->EnvRegistry->GetFriendlyObjective(TeamID);
	Hostile = ScholaEnv->EnvRegistry->GetHostileObjective(TeamID);
}




//------------------------------------------------------------------------------
// SIMULATION CONTROL
//------------------------------------------------------------------------------

void ASimulationManagerGameMode::StartSimulation()
{
	bSimulationRunning = true;
	SimulationStartTime = GetWorld()->GetTimeSeconds();

	OnSimulationStart_Delegate.Broadcast();

	UE_LOG(LogTemp, Log, TEXT("SimulationManager: Simulation started"));
}

void ASimulationManagerGameMode::StopSimulation()
{
	bSimulationRunning = false;

	OnSimulationStop_Delegate.Broadcast();

	UE_LOG(LogTemp, Log, TEXT("SimulationManager: Simulation stopped"));
}

void ASimulationManagerGameMode::ResetSimulation()
{
	StopSimulation();

	SimulationStartTime = 0.0f;

	UE_LOG(LogTemp, Log, TEXT("SimulationManager: Simulation reset"));
}

FSimulationStats ASimulationManagerGameMode::GetSimulationStats() const
{
	FSimulationStats Stats;

	Stats.TotalEnvironments = ScholaEnvironmentsArray.Num();
	Stats.SimulationTime = 0.0f;
	Stats.TotalTeams = 0;
	Stats.TotalAgents = 0;
	Stats.AliveAgents = 0;


	for (const auto& Env : ScholaEnvironmentsArray)
	{
		AScholaCombatEnvironment* ScholaEnv = Cast<AScholaCombatEnvironment>(Env);
		Stats.TotalTeams += ScholaEnv->GetRegisterTeamCount();
		Stats.TotalAgents += TotalAgents;

	}

	if (bSimulationRunning)
	{
		Stats.SimulationTime = GetWorld()->GetTimeSeconds() - SimulationStartTime;
	}

	return Stats;
}


//------------------------------------------------------------------------------
// INTERNAL
//------------------------------------------------------------------------------

void ASimulationManagerGameMode::DrawDebugInformation()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector DebugOrigin(0, 0, 1000);
	float YOffset = 0.0f;
	
	// Draw statistics
	FSimulationStats Stats = GetSimulationStats();

	FString StatsStr = FString::Printf(TEXT("Stats: %d teams, %d agents, Time: %.1fs"), 
		Stats.TotalTeams, Stats.TotalAgents, Stats.SimulationTime);

	DrawDebugString(World, DebugOrigin + FVector(0, 0, YOffset), StatsStr, nullptr,
		FColor::White, DebugDrawInterval, false, 1.5f);
}

void ASimulationManagerGameMode::DiscoverAndRegisterEnvironments()
{
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AScholaCombatEnvironment::StaticClass(), ScholaEnvironmentsArray);

	for (AActor* ScholaEnv : ScholaEnvironmentsArray)
	{
		AScholaCombatEnvironment* CombatEnv = Cast<AScholaCombatEnvironment>(ScholaEnv);
		if (CombatEnv)
		{
			ScholaEnvironmentsMap.Add(CombatEnv->GetEnvId(), CombatEnv);
		}
	}
}



//------------------------------------------------------------------------------
// EPISODE MANAGEMENT
//------------------------------------------------------------------------------



void ASimulationManagerGameMode::OnObjectiveDefeated(int32 DefeatedTeamID)
{
	// v8.5 VECTORIZED TRAINING: Determine which environment this affects
	int32 EnvironmentID = GetEnvironmentIDForTeam(DefeatedTeamID);

	if (EnvironmentID < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[OnObjectiveDefeated] Could not determine EnvironmentID for Team %d"), DefeatedTeamID);
		EnvironmentID = 0;
	}

	// Check if this environment's episode is already ending
	if (IsEnvironmentEpisodeEnding(EnvironmentID))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ENV %d] OnObjectiveDefeated: Episode already ending, skipping"), EnvironmentID);
		return;
	}

	// Find winner/loser within this environment
	int32 WinningTeamID = -1;

	// Get all teams in this environment
	for (auto& Pair : TeamToEnvironmentMap)
	{
		int32 TeamID = Pair.Key;
		int32 TeamEnvID = Pair.Value;

		if (TeamEnvID == EnvironmentID && TeamID != DefeatedTeamID)
		{
			WinningTeamID = TeamID;
			break;
		}
	}

	float* EnvStartTime = EnvironmentEpisodeStartTimes.Find(EnvironmentID);
	float EpisodeElapsed = EnvStartTime ? (GetWorld()->GetTimeSeconds() - *EnvStartTime) : 0.0f;
	float* EnvGameTime = EnvironmentEpisodeGameTimes.Find(EnvironmentID);
	int32* EnvSteps = EnvironmentSteps.Find(EnvironmentID);

	UE_LOG(LogTemp, Error, TEXT("╔════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Error, TEXT("║ [ENV %d OBJECTIVE DEFEAT] EPISODE ENDING                          ║"), EnvironmentID);
	UE_LOG(LogTemp, Error, TEXT("║   Reason: OBJECTIVE DESTROYED                                     ║"));
	UE_LOG(LogTemp, Error, TEXT("║   Defeated Team: %d                                                ║"), DefeatedTeamID);
	UE_LOG(LogTemp, Error, TEXT("║   Winner: Team %d                                                  ║"), WinningTeamID);
	UE_LOG(LogTemp, Error, TEXT("║   Episode Duration: %.1fs                                         ║"), EpisodeElapsed);
	UE_LOG(LogTemp, Error, TEXT("║   Accumulated GameTime: %.1fs                                     ║"), EnvGameTime ? *EnvGameTime : 0.0f);
	UE_LOG(LogTemp, Error, TEXT("║   Steps Taken: %d                                                  ║"), EnvSteps ? *EnvSteps : 0);
	UE_LOG(LogTemp, Error, TEXT("║   Continuous Training: %s                                          ║"),
		bEnableContinuousTraining ? TEXT("ENABLED (will continue)") : TEXT("DISABLED (will end episode)"));
	UE_LOG(LogTemp, Error, TEXT("╚════════════════════════════════════════════════════════════════════╝"));

	// CONTINUOUS TRAINING MODE: Award bonus and continue
	if (bEnableContinuousTraining && WinningTeamID != -1)
	{
		UE_LOG(LogTemp, Warning, TEXT("🏆 [ENV %d CONTINUOUS] Team %d captured objective! - CONTINUING EPISODE"), EnvironmentID, WinningTeamID);
		AwardObjectiveCaptureBonus(WinningTeamID);
	}
	// TRADITIONAL MODE: End episode on objective capture
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ENV %d OBJECTIVE CAPTURE] Winner: Team %d - ENDING EPISODE"), EnvironmentID, WinningTeamID);
		EndEpisode(WinningTeamID, DefeatedTeamID, EnvironmentID);
	}
}


void ASimulationManagerGameMode::AwardObjectiveCaptureBonus(int32 TeamID)
{
	if (!bEnableContinuousTraining)
	{
		return;
	}

	FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	if (!TeamInfo)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("🎯 Team %d captured objective - awarding bonus rewards"), TeamID);

	// Award immediate capture bonus to all team members
	for (AActor* Member : TeamInfo->TeamMembers)
	{
		if (!Member || !IsValid(Member))
		{
			continue;
		}

		UFollowerAgentComponent* FollowerComp = Member->FindComponentByClass<UFollowerAgentComponent>();
		if (FollowerComp && FollowerComp->GetIsAlive())
		{
			float CaptureBonus = 10.0f;  // Immediate bonus for team wipe
			FollowerComp->AccumulateReward(CaptureBonus);
		}
	}
}