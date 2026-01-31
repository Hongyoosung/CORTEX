#include "Core/SimulationManagerGameMode.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Team/ObjectiveActor.h"
#include "Combat/Components/HealthComponent.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"

ASimulationManagerGameMode::ASimulationManagerGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f; // 10 Hz
}

void ASimulationManagerGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Auto-discover objective actor if not manually assigned
	if (!ObjectiveActor)
	{
		ObjectiveActor = FindObjectiveActor();
		if (ObjectiveActor)
		{
			UE_LOG(LogTemp, Log, TEXT("[SimulationManager] Auto-discovered ObjectiveActor: %s"), *ObjectiveActor->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[SimulationManager] No ObjectiveActor found with tag '%s'. Objective proximity rewards disabled."), *ObjectiveActorTag.ToString());
		}
	}

	// Bind all ObjectiveActor defeat events for episode termination
	TArray<AActor*> FoundObjectives;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AObjectiveActor::StaticClass(), FoundObjectives);

	for (AActor* ObjActor : FoundObjectives)
	{
		AObjectiveActor* Objective = Cast<AObjectiveActor>(ObjActor);
		if (Objective)
		{
			Objective->OnObjectiveDefeated.AddUniqueDynamic(this, &ASimulationManagerGameMode::OnObjectiveDefeated);
			UE_LOG(LogTemp, Log, TEXT("[SimulationManager] Bound OnObjectiveDefeated for '%s' (Team %d)"),
				*Objective->GetName(), Objective->OwnerTeamID);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[SimulationManager] Bound %d ObjectiveActor defeat delegates"), FoundObjectives.Num());

	if (bAutoStartSimulation)
	{
		StartSimulation();
	}
}

void ASimulationManagerGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// v8.5 VECTORIZED TRAINING: Check termination per-environment instead of globally
	if (bSimulationRunning)
	{
		// Increment global step counter (legacy, kept for backward compatibility)
		CurrentStep++;

		// [DIAGNOSTIC] Log tick frequency every 100 ticks
		static int32 TickCounter = 0;
		TickCounter++;
		if (TickCounter % 100 == 0)
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("[TICK DIAGNOSTIC] Tick #%d, DeltaTime=%.3fs, TickInterval=%.3fs"),
				TickCounter, DeltaTime, PrimaryActorTick.TickInterval);
		}

		// v8.5 CRITICAL FIX: Accumulate game time per environment (ONCE per environment per tick)
		// Previous bug: Loop iterated over TeamToEnvironmentMap (8 teams), causing each environment's
		// time to be incremented 2x per tick (once per team). This caused episodes to end at 2x speed.
		TSet<int32> ProcessedEnvironments;  // Track which environments we've updated this tick

		for (auto& Pair : TeamToEnvironmentMap)
		{
			int32 EnvID = Pair.Value;

			// Skip if we already processed this environment this tick
			if (ProcessedEnvironments.Contains(EnvID))
			{
				continue;
			}
			ProcessedEnvironments.Add(EnvID);

			// Skip environments that are already ending
			if (IsEnvironmentEpisodeEnding(EnvID))
			{
				continue;
			}

			// Increment step counter for this environment
			int32* EnvSteps = EnvironmentSteps.Find(EnvID);
			if (EnvSteps)
			{
				(*EnvSteps)++;
			}

			// Accumulate game time for this environment (NOW ONLY ONCE PER TICK)
			float* EnvGameTime = EnvironmentEpisodeGameTimes.Find(EnvID);
			if (!EnvGameTime)
			{
				EnvironmentEpisodeGameTimes.Add(EnvID, 0.0f);
				EnvGameTime = EnvironmentEpisodeGameTimes.Find(EnvID);
			}
			if (EnvGameTime)
			{
				float OldTime = *EnvGameTime;
				*EnvGameTime += DeltaTime;

				// [DIAGNOSTIC] Log time accumulation every 5 seconds
				if (FMath::FloorToInt(*EnvGameTime / 5.0f) > FMath::FloorToInt(OldTime / 5.0f))
				{
					UE_LOG(LogTemp, Log, TEXT("[ENV %d TIME] Accumulated time: %.1fs (Steps: %d, MaxDuration: %.1fs, MaxSteps: %d)"),
						EnvID, *EnvGameTime, EnvSteps ? *EnvSteps : 0, MaxEpisodeDuration, MaxStepsPerEpisode);
				}
			}

			// Check for max duration termination (per-environment)
			if (MaxEpisodeDuration > 0.0f && EnvGameTime && *EnvGameTime >= MaxEpisodeDuration)
			{
				float* EnvStartTime = EnvironmentEpisodeStartTimes.Find(EnvID);
				float WallClockElapsed = EnvStartTime ? (GetWorld()->GetTimeSeconds() - *EnvStartTime) : 0.0f;

				UE_LOG(LogTemp, Error, TEXT("╔════════════════════════════════════════════════════════════════════╗"));
				UE_LOG(LogTemp, Error, TEXT("║ [ENV %d TIMEOUT] EPISODE ENDING DUE TO MAX DURATION               ║"), EnvID);
				UE_LOG(LogTemp, Error, TEXT("║   Reason: TIME LIMIT REACHED                                      ║"));
				UE_LOG(LogTemp, Error, TEXT("║   Accumulated GameTime: %.1fs                                     ║"), *EnvGameTime);
				UE_LOG(LogTemp, Error, TEXT("║   MaxEpisodeDuration: %.1fs                                       ║"), MaxEpisodeDuration);
				UE_LOG(LogTemp, Error, TEXT("║   Wall Clock Elapsed: %.1fs                                       ║"), WallClockElapsed);
				UE_LOG(LogTemp, Error, TEXT("║   Steps Taken: %d                                                  ║"), EnvSteps ? *EnvSteps : 0);
				UE_LOG(LogTemp, Error, TEXT("╚════════════════════════════════════════════════════════════════════╝"));

				// End episode for this environment only
				// Find teams in this environment
				TArray<int32> TeamsInEnv;
				for (auto& TeamPair : TeamToEnvironmentMap)
				{
					if (TeamPair.Value == EnvID)
					{
						TeamsInEnv.Add(TeamPair.Key);
					}
				}

				if (TeamsInEnv.Num() >= 2)
				{
					// Timeout: no winner
					UE_LOG(LogTemp, Warning, TEXT("[ENV %d TIMEOUT] Ending episode with no winner (timeout)"), EnvID);
					EndEpisode(-1, -1, EnvID);
				}
			}

			// Check for max steps termination (per-environment)
			if (MaxStepsPerEpisode > 0 && EnvSteps && *EnvSteps >= MaxStepsPerEpisode)
			{
				UE_LOG(LogTemp, Error, TEXT("╔════════════════════════════════════════════════════════════════════╗"));
				UE_LOG(LogTemp, Error, TEXT("║ [ENV %d MAX STEPS] EPISODE ENDING DUE TO MAX STEPS                ║"), EnvID);
				UE_LOG(LogTemp, Error, TEXT("║   Reason: STEP LIMIT REACHED                                      ║"));
				UE_LOG(LogTemp, Error, TEXT("║   Steps Taken: %d                                                  ║"), *EnvSteps);
				UE_LOG(LogTemp, Error, TEXT("║   MaxStepsPerEpisode: %d                                           ║"), MaxStepsPerEpisode);
				UE_LOG(LogTemp, Error, TEXT("║   Accumulated GameTime: %.1fs                                     ║"), EnvGameTime ? *EnvGameTime : 0.0f);
				UE_LOG(LogTemp, Error, TEXT("╚════════════════════════════════════════════════════════════════════╝"));

				EndEpisode(-1, -1, EnvID);
			}
		}

		// CONTINUOUS TRAINING: Award objective proximity rewards
		if (bEnableContinuousTraining && ObjectiveActor && IsValid(ObjectiveActor))
		{
			FVector ObjectiveLocation = ObjectiveActor->GetActorLocation();

			for (auto& TeamPair : RegisteredTeams)
			{
				FTeamInfo& TeamInfo = TeamPair.Value;

				for (AActor* Member : TeamInfo.TeamMembers)
				{
					if (!Member || !IsValid(Member))
					{
						continue;
					}

					// v9.0: DISABLED - Replaced by strategy-specific rewards in RewardCalculator
					// Legacy proximity rewards caused identical values for all agents (no differentiation)
					// Now using TacticalRewardProvider → RewardCalculator::CalculateUnifiedReward()
					/*
					// Check distance to objective
					float Distance = FVector::Dist(Member->GetActorLocation(), ObjectiveLocation);

					if (Distance <= ObjectiveProximityRadius)
					{
						// Award proximity reward to alive agents only
						UFollowerAgentComponent* FollowerComp = Member->FindComponentByClass<UFollowerAgentComponent>();
						if (FollowerComp && FollowerComp->GetIsAlive())
						{
							float Reward = ObjectiveProximityReward * DeltaTime;  // Scale by delta time for consistent rewards
							FollowerComp->AccumulateReward(Reward);
						}
					}
					*/
				}
			}
		}

		// Episode termination is now event-driven via OnAgentDied()
		// No per-tick checking needed

		if (bDrawDebugInfo)
		{
			float CurrentTime = GetWorld()->GetTimeSeconds();
			if (CurrentTime - LastDebugDrawTime >= DebugDrawInterval)
			{
				DrawDebugInformation();
				LastDebugDrawTime = CurrentTime;
			}
		}
	}
}

//------------------------------------------------------------------------------
// TEAM REGISTRATION
//------------------------------------------------------------------------------

bool ASimulationManagerGameMode::RegisterTeam(
	int32 TeamID,
	UTeamLeaderComponent* TeamLeader,
	const FString& TeamName,
	FLinearColor TeamColor)
{
	if (RegisteredTeams.Contains(TeamID))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Team %d already registered"), TeamID);
		return false;
	}

	if (!TeamLeader)
	{
		UE_LOG(LogTemp, Error, TEXT("SimulationManager: Cannot register team %d - TeamLeader is null"), TeamID);
		return false;
	}

	FTeamInfo NewTeam;
	NewTeam.TeamID = TeamID;
	NewTeam.TeamName = TeamName;
	NewTeam.TeamLeader = TeamLeader;
	NewTeam.TeamColor = TeamColor;
	NewTeam.bIsActive = bSimulationRunning;

	RegisteredTeams.Add(TeamID, NewTeam);

	// [FIX] Register Team Leader actor in ActorToTeamMap so it's recognized as part of the team
	// This fixes "TeamID=-1" logs when checking the leader actor
	if (AActor* LeaderActor = TeamLeader->GetOwner())
	{
		ActorToTeamMap.Add(LeaderActor, TeamID);
	}

	//--------------------------------------------------------------------------
	// v8.5: VECTORIZED TRAINING - Auto-configure enemy relationships
	//--------------------------------------------------------------------------
	// Environment pairing pattern:
	//   - Environment 0: Team 0 vs Team 1
	//   - Environment 1: Team 2 vs Team 3
	//   - Environment 2: Team 4 vs Team 5
	//   - Environment 3: Team 6 vs Team 7
	//
	// Logic: Use XOR with 1 to flip last bit (0↔1, 2↔3, 4↔5, 6↔7)
	// When a team registers, check if its paired enemy exists and create mutual enemy relationship
	//--------------------------------------------------------------------------

	// XOR with 1 flips last bit: 0→1, 1→0, 2→3, 3→2, etc.
	int32 PairedEnemyTeamID = TeamID ^ 1;
	int32 EnvironmentID = TeamID / 2;

	UE_LOG(LogTemp, Warning, TEXT("[SimManager v8.5] Registering Team %d (Env %d). Paired enemy: Team %d"),
		TeamID, EnvironmentID, PairedEnemyTeamID);

	// Check if paired enemy team is already registered
	if (RegisteredTeams.Contains(PairedEnemyTeamID))
	{
		// Both teams exist → establish mutual enemy relationship
		SetMutualEnemies(TeamID, PairedEnemyTeamID);

		// CRITICAL: Verify enemy relationship was actually set
		const FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
		const FTeamInfo* EnemyInfo = RegisteredTeams.Find(PairedEnemyTeamID);

		bool bTeamHasEnemy = TeamInfo && TeamInfo->EnemyTeamIDs.Contains(PairedEnemyTeamID);
		bool bEnemyHasTeam = EnemyInfo && EnemyInfo->EnemyTeamIDs.Contains(TeamID);

		if (bTeamHasEnemy && bEnemyHasTeam)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SimManager v8.5] ✅ Enemy relationship confirmed: Team %d ↔ Team %d (Env %d)"),
				TeamID, PairedEnemyTeamID, EnvironmentID);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[SimManager v8.5] ❌ FAILED to set enemy relationship: Team %d ↔ Team %d (Team→Enemy: %s, Enemy→Team: %s)"),
				TeamID, PairedEnemyTeamID,
				bTeamHasEnemy ? TEXT("OK") : TEXT("MISSING"),
				bEnemyHasTeam ? TEXT("OK") : TEXT("MISSING"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[SimManager v8.5] Team %d registered. Waiting for Team %d to establish enemy relationship (Env %d)"),
			TeamID, PairedEnemyTeamID, EnvironmentID);
	}

	UE_LOG(LogTemp, Log, TEXT("SimulationManager: Registered team %d (%s)"), TeamID, *TeamName);
	return true;
}

void ASimulationManagerGameMode::UnregisterTeam(int32 TeamID)
{
	if (!RegisteredTeams.Contains(TeamID))
	{
		return;
	}

	// Remove all team members from actor map
	FTeamInfo& TeamInfo = RegisteredTeams[TeamID];
	for (AActor* Member : TeamInfo.TeamMembers)
	{
		ActorToTeamMap.Remove(Member);
	}

	// Remove team
	RegisteredTeams.Remove(TeamID);

	UE_LOG(LogTemp, Log, TEXT("SimulationManager: Unregistered team %d"), TeamID);
}

bool ASimulationManagerGameMode::RegisterTeamMember(int32 TeamID, AActor* Agent)
{
	if (!Agent)
	{
		return false;
	}

	// CRITICAL FIX: Check if already registered to prevent duplicate registration
	int32* ExistingTeamID = ActorToTeamMap.Find(Agent);
	if (ExistingTeamID)
	{
		if (*ExistingTeamID == TeamID)
		{
			// Already registered to this team - skip duplicate registration
			UE_LOG(LogTemp, Log, TEXT("[DUPLICATE REGISTRATION BLOCKED] %s already registered to Team %d, skipping"),
				*Agent->GetName(), TeamID);
			return true;
		}
		else
		{
			// Moving to a different team - unregister from old team first
			UE_LOG(LogTemp, Warning, TEXT("[TEAM CHANGE] %s moving from Team %d to Team %d"),
				*Agent->GetName(), *ExistingTeamID, TeamID);
			UnregisterTeamMember(*ExistingTeamID, Agent);
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[NEW REGISTRATION] %s being registered to Team %d (first time)"),
			*Agent->GetName(), TeamID);
	}

	FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	if (!TeamInfo)
	{
		UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Cannot register member - team %d not found"), TeamID);
		return false;
	}

	// Add to new team
	TeamInfo->TeamMembers.AddUnique(Agent);
	ActorToTeamMap.Add(Agent, TeamID);

	// Add gameplay tag for team identification (e.g., "Team.0", "Team.1")
	FName TeamTag = FName(*FString::Printf(TEXT("Team.%d"), TeamID));
	if (!Agent->Tags.Contains(TeamTag))
	{
		Agent->Tags.Add(TeamTag);
		UE_LOG(LogTemp, Log, TEXT("SimulationManager: Added tag '%s' to %s"), *TeamTag.ToString(), *Agent->GetName());
	}

	// Store initial spawn transform for episode reset
	if (!SpawnTransforms.Contains(Agent))
	{
		SpawnTransforms.Add(Agent, Agent->GetActorTransform());
		UE_LOG(LogTemp, Log, TEXT("SimulationManager: Stored spawn transform for %s"), *Agent->GetName());
	}

	// Auto-bind HealthComponent::OnDeath to OnAgentDied for episode tracking
	UHealthComponent* HealthComp = Agent->FindComponentByClass<UHealthComponent>();
	if (HealthComp)
	{
		HealthComp->OnDeath.AddUniqueDynamic(this, &ASimulationManagerGameMode::OnAgentDied);
		UE_LOG(LogTemp, Log, TEXT("SimulationManager: Bound OnDeath for %s"), *Agent->GetName());
	}

	// [DIAGNOSTIC] Count total agents across all teams
	int32 TotalAgents = 0;
	for (const auto& TeamPair : RegisteredTeams)
	{
		TotalAgents += TeamPair.Value.TeamMembers.Num();
	}

	UE_LOG(LogTemp, Warning, TEXT("[AGENT REGISTRATION] Registered %s to Team %d. Total agents across all teams: %d"),
		*Agent->GetName(), TeamID, TotalAgents);

	// [DIAGNOSTIC] Log per-team agent counts every 8 registrations (once per team in 4v4 setup)
	if (TotalAgents % 8 == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════════════╗"));
		UE_LOG(LogTemp, Warning, TEXT("║ [AGENT COUNT] Current Team Sizes:                                 ║"));
		for (const auto& TeamPair : RegisteredTeams)
		{
			int32 MemberCount = TeamPair.Value.TeamMembers.Num();
			int32 EnvID = GetEnvironmentIDForTeam(TeamPair.Key);
			UE_LOG(LogTemp, Warning, TEXT("║   Team %d (Env %d): %d agents                                      ║"),
				TeamPair.Key, EnvID, MemberCount);
		}
		UE_LOG(LogTemp, Warning, TEXT("║ TOTAL: %d agents registered                                       ║"), TotalAgents);
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════════════╝"));
	}

	return true;
}

void ASimulationManagerGameMode::UnregisterTeamMember(int32 TeamID, AActor* Agent)
{
	if (!Agent)
	{
		return;
	}

	FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	if (TeamInfo)
	{
		TeamInfo->TeamMembers.Remove(Agent);
	}

	// Unbind OnDeath delegate
	UHealthComponent* HealthComp = Agent->FindComponentByClass<UHealthComponent>();
	if (HealthComp)
	{
		HealthComp->OnDeath.RemoveDynamic(this, &ASimulationManagerGameMode::OnAgentDied);
	}

	ActorToTeamMap.Remove(Agent);
	SpawnTransforms.Remove(Agent);
}

void ASimulationManagerGameMode::RegisterTeamEnvironment(int32 TeamID, int32 EnvironmentID)
{
	// Check if already registered (idempotent)
	if (int32* ExistingEnvID = TeamToEnvironmentMap.Find(TeamID))
	{
		if (*ExistingEnvID == EnvironmentID)
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("SimulationManager: Team %d already registered to Environment %d, skipping"),
				TeamID, EnvironmentID);
			return;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Team %d already registered to Environment %d, changing to %d"),
				TeamID, *ExistingEnvID, EnvironmentID);
		}
	}

	TeamToEnvironmentMap.Add(TeamID, EnvironmentID);
	UE_LOG(LogTemp, Log, TEXT("SimulationManager: Registered Team %d to Environment %d"), TeamID, EnvironmentID);
}

int32 ASimulationManagerGameMode::GetTeamIDForActor(AActor* Agent) const
{
	if (!Agent)
	{
		return -1;
	}

	const int32* TeamID = ActorToTeamMap.Find(Agent);
	return TeamID ? *TeamID : -1;
}

//------------------------------------------------------------------------------
// ENEMY TEAM MANAGEMENT
//------------------------------------------------------------------------------

void ASimulationManagerGameMode::SetEnemyTeams(int32 TeamID, const TArray<int32>& EnemyTeamIDs)
{
	FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	if (!TeamInfo)
	{
		return;
	}

	TeamInfo->EnemyTeamIDs.Empty();
	for (int32 EnemyID : EnemyTeamIDs)
	{
		if (RegisteredTeams.Contains(EnemyID))
		{
			TeamInfo->EnemyTeamIDs.Add(EnemyID);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("SimulationManager: Team %d now has %d enemy teams"),
		TeamID, TeamInfo->EnemyTeamIDs.Num());
}

void ASimulationManagerGameMode::AddEnemyTeam(int32 TeamID, int32 EnemyTeamID)
{
	FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	if (!TeamInfo || !RegisteredTeams.Contains(EnemyTeamID))
	{
		return;
	}

	TeamInfo->EnemyTeamIDs.Add(EnemyTeamID);
	UE_LOG(LogTemp, Log, TEXT("SimulationManager: Team %d added enemy team %d"), TeamID, EnemyTeamID);
}

void ASimulationManagerGameMode::RemoveEnemyTeam(int32 TeamID, int32 EnemyTeamID)
{
	FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	if (!TeamInfo)
	{
		return;
	}

	TeamInfo->EnemyTeamIDs.Remove(EnemyTeamID);
}

void ASimulationManagerGameMode::SetMutualEnemies(int32 TeamID1, int32 TeamID2)
{
	if (!RegisteredTeams.Contains(TeamID1) || !RegisteredTeams.Contains(TeamID2))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Cannot set mutual enemies - one or both teams not found"));
		return;
	}

	AddEnemyTeam(TeamID1, TeamID2);
	AddEnemyTeam(TeamID2, TeamID1);

	UE_LOG(LogTemp, Log, TEXT("SimulationManager: Teams %d and %d are now mutual enemies"), TeamID1, TeamID2);
}

bool ASimulationManagerGameMode::AreTeamsEnemies(int32 TeamID1, int32 TeamID2) const
{
	const FTeamInfo* Team1 = RegisteredTeams.Find(TeamID1);
	if (!Team1)
	{
		return false;
	}

	return Team1->EnemyTeamIDs.Contains(TeamID2);
}

bool ASimulationManagerGameMode::AreActorsEnemies(AActor* Actor1, AActor* Actor2) const
{
	if (!Actor1 || !Actor2)
	{
		return false;
	}

	// Primary method: Use team ID system
	int32 Team1 = GetTeamIDForActor(Actor1);
	int32 Team2 = GetTeamIDForActor(Actor2);

	if (Team1 != -1 && Team2 != -1)
	{
		return AreTeamsEnemies(Team1, Team2);
	}

	// Fallback method: Use gameplay tags
	// Check for Team.Ally vs Team.Enemy tags
	bool bActor1IsAlly = Actor1->ActorHasTag("Team.Ally");
	bool bActor1IsEnemy = Actor1->ActorHasTag("Team.Enemy");
	bool bActor2IsAlly = Actor2->ActorHasTag("Team.Ally");
	bool bActor2IsEnemy = Actor2->ActorHasTag("Team.Enemy");

	// Enemies if one is tagged Ally and the other is tagged Enemy
	if ((bActor1IsAlly && bActor2IsEnemy) || (bActor1IsEnemy && bActor2IsAlly))
	{
		return true;
	}

	// Also support team number tags (Team.0, Team.1, etc.)
	// Extract team number from tags
	int32 Tag1TeamNum = -1;
	int32 Tag2TeamNum = -1;

	for (const FName& Tag : Actor1->Tags)
	{
		FString TagStr = Tag.ToString();
		if (TagStr.StartsWith("Team.") && TagStr.Len() > 5)
		{
			FString NumStr = TagStr.RightChop(5); // Remove "Team."
			if (NumStr.IsNumeric())
			{
				Tag1TeamNum = FCString::Atoi(*NumStr);
				break;
			}
		}
	}

	for (const FName& Tag : Actor2->Tags)
	{
		FString TagStr = Tag.ToString();
		if (TagStr.StartsWith("Team.") && TagStr.Len() > 5)
		{
			FString NumStr = TagStr.RightChop(5); // Remove "Team."
			if (NumStr.IsNumeric())
			{
				Tag2TeamNum = FCString::Atoi(*NumStr);
				break;
			}
		}
	}

	// If both have team number tags and they're in registered enemy teams
	if (Tag1TeamNum != -1 && Tag2TeamNum != -1)
	{
		return AreTeamsEnemies(Tag1TeamNum, Tag2TeamNum);
	}

	// Default: not enemies
	return false;
}

TArray<int32> ASimulationManagerGameMode::GetEnemyTeamIDs(int32 TeamID) const
{
	const FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	if (!TeamInfo)
	{
		return TArray<int32>();
	}

	return TeamInfo->EnemyTeamIDs.Array();
}

TArray<AActor*> ASimulationManagerGameMode::GetEnemyActors(int32 TeamID) const
{
	TArray<AActor*> EnemyActors;

	const FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	if (!TeamInfo)
	{
		return EnemyActors;
	}

	for (int32 EnemyTeamID : TeamInfo->EnemyTeamIDs)
	{
		const FTeamInfo* EnemyTeam = RegisteredTeams.Find(EnemyTeamID);
		if (EnemyTeam)
		{
			EnemyActors.Append(EnemyTeam->TeamMembers);
		}
	}

	return EnemyActors;
}

//------------------------------------------------------------------------------
// TEAM QUERIES
//------------------------------------------------------------------------------

bool ASimulationManagerGameMode::GetTeamInfo(int32 TeamID, FTeamInfo& OutTeamInfo) const
{
	const FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	if (!TeamInfo)
	{
		return false;
	}

	OutTeamInfo = *TeamInfo;
	return true;
}

UTeamLeaderComponent* ASimulationManagerGameMode::GetTeamLeader(int32 TeamID) const
{
	const FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	return TeamInfo ? TeamInfo->TeamLeader : nullptr;
}

TArray<AActor*> ASimulationManagerGameMode::GetTeamMembers(int32 TeamID) const
{
	const FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	return TeamInfo ? TeamInfo->TeamMembers : TArray<AActor*>();
}

TArray<int32> ASimulationManagerGameMode::GetAllTeamIDs() const
{
	TArray<int32> TeamIDs;
	RegisteredTeams.GetKeys(TeamIDs);
	return TeamIDs;
}

bool ASimulationManagerGameMode::IsTeamRegistered(int32 TeamID) const
{
	return RegisteredTeams.Contains(TeamID);
}

//------------------------------------------------------------------------------
// SIMULATION CONTROL
//------------------------------------------------------------------------------

void ASimulationManagerGameMode::StartSimulation()
{
	bSimulationRunning = true;
	SimulationStartTime = GetWorld()->GetTimeSeconds();

	for (auto& Pair : RegisteredTeams)
	{
		Pair.Value.bIsActive = true;
	}

	UE_LOG(LogTemp, Log, TEXT("SimulationManager: Simulation started with %d teams"), RegisteredTeams.Num());
}

void ASimulationManagerGameMode::StopSimulation()
{
	bSimulationRunning = false;

	for (auto& Pair : RegisteredTeams)
	{
		Pair.Value.bIsActive = false;
	}

	UE_LOG(LogTemp, Log, TEXT("SimulationManager: Simulation stopped"));
}

void ASimulationManagerGameMode::ResetSimulation()
{
	StopSimulation();

	RegisteredTeams.Empty();
	ActorToTeamMap.Empty();
	SimulationStartTime = 0.0f;

	UE_LOG(LogTemp, Log, TEXT("SimulationManager: Simulation reset"));
}

FSimulationStats ASimulationManagerGameMode::GetSimulationStats() const
{
	FSimulationStats Stats;

	Stats.TotalTeams = RegisteredTeams.Num();
	Stats.ActiveTeams = 0;
	Stats.TotalAgents = 0;
	Stats.AliveAgents = 0;

	for (const auto& Pair : RegisteredTeams)
	{
		if (Pair.Value.bIsActive)
		{
			Stats.ActiveTeams++;
		}

		Stats.TotalAgents += Pair.Value.TeamMembers.Num();

		// Count alive agents (non-null and not pending kill)
		for (AActor* Member : Pair.Value.TeamMembers)
		{
			if (Member && IsValid(Member))
			{
				Stats.AliveAgents++;
			}
		}
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

	// Draw team information
	for (const auto& Pair : RegisteredTeams)
	{
		const FTeamInfo& Team = Pair.Value;

		// Count alive members and detected enemies
		int32 AliveCount = GetAliveAgentCount(Team.TeamID);
		int32 DetectedEnemyCount = 0;

		// Count alive enemies across all enemy teams
		for (int32 EnemyTeamID : Team.EnemyTeamIDs)
		{
			DetectedEnemyCount += GetAliveAgentCount(EnemyTeamID);
		}

		FString TeamStr = FString::Printf(TEXT("Team %d (%s): %d/%d alive, %d enemy teams, %d detected enemies"),
			Team.TeamID, *Team.TeamName, AliveCount, Team.TeamMembers.Num(), Team.EnemyTeamIDs.Num(), DetectedEnemyCount);

		DrawDebugString(World, DebugOrigin + FVector(0, 0, YOffset), TeamStr, nullptr,
			Team.TeamColor.ToFColor(true), DebugDrawInterval, false, 1.2f);

		YOffset -= 50.0f;
	}

	// Draw statistics
	FSimulationStats Stats = GetSimulationStats();
	FString StatsStr = FString::Printf(TEXT("Stats: %d/%d teams active, %d/%d agents alive, Time: %.1fs"),
		Stats.ActiveTeams, Stats.TotalTeams, Stats.AliveAgents, Stats.TotalAgents, Stats.SimulationTime);

	DrawDebugString(World, DebugOrigin + FVector(0, 0, YOffset), StatsStr, nullptr,
		FColor::White, DebugDrawInterval, false, 1.5f);
}

//------------------------------------------------------------------------------
// v8.5 VECTORIZED TRAINING: Per-Environment Episode State Queries
//------------------------------------------------------------------------------

bool ASimulationManagerGameMode::IsEnvironmentEpisodeEnding(int32 EnvironmentID) const
{
	const bool* bEnding = EnvironmentEpisodeEnding.Find(EnvironmentID);
	return bEnding ? *bEnding : false;
}

bool ASimulationManagerGameMode::GetEnvironmentLastTerminated(int32 EnvironmentID) const
{
	const bool* bTerminated = EnvironmentLastTerminated.Find(EnvironmentID);
	return bTerminated ? *bTerminated : false;
}

bool ASimulationManagerGameMode::GetEnvironmentLastTimeout(int32 EnvironmentID) const
{
	const bool* bTimeout = EnvironmentLastTimeout.Find(EnvironmentID);
	return bTimeout ? *bTimeout : false;
}

void ASimulationManagerGameMode::SetEnvironmentTerminationFlags(int32 EnvironmentID, bool bEnding, bool bTerminated, bool bTimeout)
{
	EnvironmentEpisodeEnding.Add(EnvironmentID, bEnding);
	EnvironmentLastTerminated.Add(EnvironmentID, bTerminated);
	EnvironmentLastTimeout.Add(EnvironmentID, bTimeout);
}

int32 ASimulationManagerGameMode::GetEnvironmentIDForTeam(int32 TeamID) const
{
	const int32* EnvID = TeamToEnvironmentMap.Find(TeamID);
	return EnvID ? *EnvID : -1;
}

//------------------------------------------------------------------------------
// EPISODE MANAGEMENT
//------------------------------------------------------------------------------

void ASimulationManagerGameMode::OnAgentDied(const FDeathEventData& DeathEvent)
{
	if (!DeathEvent.DeadActor)
	{
		return;
	}

	int32 TeamID = GetTeamIDForActor(DeathEvent.DeadActor);
	int32 AliveCount = GetAliveAgentCount(TeamID);

	UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Agent '%s' (Team %d) died - %d agents remaining alive (Killed by %s)"),
		*DeathEvent.DeadActor->GetName(), TeamID, AliveCount,
		DeathEvent.Killer ? *DeathEvent.Killer->GetName() : TEXT("Unknown"));

	// v8.5 VECTORIZED TRAINING: Check termination for the environment this team belongs to
	int32 EnvironmentID = GetEnvironmentIDForTeam(TeamID);
	if (EnvironmentID >= 0 && !IsEnvironmentEpisodeEnding(EnvironmentID))
	{
		CheckEpisodeTermination();
	}
}

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

bool ASimulationManagerGameMode::IsTeamEliminated(int32 TeamID) const
{
	return GetAliveAgentCount(TeamID) == 0;
}

int32 ASimulationManagerGameMode::GetAliveAgentCount(int32 TeamID) const
{
	const FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	if (!TeamInfo)
	{
		return 0;
	}

	int32 AliveCount = 0;
	for (AActor* Member : TeamInfo->TeamMembers)
	{
		if (!Member || !IsValid(Member))
		{
			continue;
		}

		// Skip leader if configured
		if (!bIncludeLeaderInElimination && TeamInfo->TeamLeader && Member == TeamInfo->TeamLeader->GetOwner())
		{
			continue;
		}

		// Check HealthComponent for alive status
		UHealthComponent* HealthComp = Member->FindComponentByClass<UHealthComponent>();
		if (HealthComp && HealthComp->IsAlive())
		{
			AliveCount++;
		}
		else if (!HealthComp)
		{
			// No health component - assume alive if valid
			AliveCount++;
		}
	}

	return AliveCount;
}

void ASimulationManagerGameMode::CheckEpisodeTermination()
{
	// v8.5 VECTORIZED TRAINING: Check termination per-environment
	// Group teams by environment and check elimination status

	// Build map of EnvironmentID -> Teams
	TMap<int32, TArray<int32>> EnvironmentTeams;
	for (auto& Pair : TeamToEnvironmentMap)
	{
		int32 TeamID = Pair.Key;
		int32 EnvID = Pair.Value;

		if (!EnvironmentTeams.Contains(EnvID))
		{
			EnvironmentTeams.Add(EnvID, TArray<int32>());
		}
		EnvironmentTeams[EnvID].Add(TeamID);
	}

	// Check each environment independently
	for (auto& EnvPair : EnvironmentTeams)
	{
		int32 EnvID = EnvPair.Key;
		TArray<int32>& TeamsInEnv = EnvPair.Value;

		// Skip if this environment's episode is already ending
		if (IsEnvironmentEpisodeEnding(EnvID))
		{
			continue;
		}

		// Check team elimination for this environment
		TArray<int32> AliveTeams;
		TArray<int32> EliminatedTeams;

		for (int32 TeamID : TeamsInEnv)
		{
			int32 AliveCount = GetAliveAgentCount(TeamID);
			bool bEliminated = IsTeamEliminated(TeamID);

			if (bEliminated)
			{
				EliminatedTeams.Add(TeamID);
			}
			else
			{
				AliveTeams.Add(TeamID);
			}
		}

		UE_LOG(LogTemp, VeryVerbose, TEXT("[ENV %d] CheckTermination: %d alive, %d eliminated (Total: %d)"),
			EnvID, AliveTeams.Num(), EliminatedTeams.Num(), TeamsInEnv.Num());

		// Handle team elimination for this environment
		if (AliveTeams.Num() <= 1 && TeamsInEnv.Num() > 1)
		{
			int32 WinningTeamID = AliveTeams.Num() == 1 ? AliveTeams[0] : -1;
			int32 LosingTeamID = EliminatedTeams.Num() > 0 ? EliminatedTeams[0] : -1;

			float* EnvStartTime = EnvironmentEpisodeStartTimes.Find(EnvID);
			float EpisodeElapsed = EnvStartTime ? (GetWorld()->GetTimeSeconds() - *EnvStartTime) : 0.0f;
			float* EnvGameTime = EnvironmentEpisodeGameTimes.Find(EnvID);
			int32* EnvSteps = EnvironmentSteps.Find(EnvID);

			UE_LOG(LogTemp, Error, TEXT("╔════════════════════════════════════════════════════════════════════╗"));
			UE_LOG(LogTemp, Error, TEXT("║ [ENV %d TEAM ELIMINATION] EPISODE ENDING                          ║"), EnvID);
			UE_LOG(LogTemp, Error, TEXT("║   Reason: TEAM ANNIHILATION                                       ║"));
			UE_LOG(LogTemp, Error, TEXT("║   Winner: Team %d                                                  ║"), WinningTeamID);
			UE_LOG(LogTemp, Error, TEXT("║   Loser: Team %d                                                   ║"), LosingTeamID);
			UE_LOG(LogTemp, Error, TEXT("║   Episode Duration: %.1fs                                         ║"), EpisodeElapsed);
			UE_LOG(LogTemp, Error, TEXT("║   Accumulated GameTime: %.1fs                                     ║"), EnvGameTime ? *EnvGameTime : 0.0f);
			UE_LOG(LogTemp, Error, TEXT("║   Steps Taken: %d                                                  ║"), EnvSteps ? *EnvSteps : 0);
			UE_LOG(LogTemp, Error, TEXT("║   Continuous Training: %s                                          ║"),
				bEnableContinuousTraining ? TEXT("ENABLED (will respawn)") : TEXT("DISABLED (will end episode)"));
			UE_LOG(LogTemp, Error, TEXT("╚════════════════════════════════════════════════════════════════════╝"));

			// CONTINUOUS TRAINING MODE: Respawn losing team instead of ending episode
			if (bEnableContinuousTraining && WinningTeamID != -1 && LosingTeamID != -1)
			{
				UE_LOG(LogTemp, Warning, TEXT("🔄 [ENV %d] Team %d eliminated! Team %d wins (Continuous Training) - RESPAWNING"),
					EnvID, LosingTeamID, WinningTeamID);

				AwardObjectiveCaptureBonus(WinningTeamID);
				ScheduleTeamRespawn(LosingTeamID, RespawnDelay);
			}
			// TRADITIONAL MODE: End episode on team elimination
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[ENV %d ELIMINATION] Team %d eliminated! Winner: Team %d - ENDING EPISODE"),
					EnvID, LosingTeamID, WinningTeamID);

				EndEpisode(WinningTeamID, LosingTeamID, EnvID);
			}
		}
	}
}

void ASimulationManagerGameMode::EndEpisode(int32 WinningTeamID, int32 LosingTeamID, int32 EnvironmentID)
{
	// v8.5 VECTORIZED TRAINING: Per-environment episode termination
	// If EnvironmentID not provided, try to determine from teams
	if (EnvironmentID < 0 && WinningTeamID >= 0)
	{
		int32* FoundEnvID = TeamToEnvironmentMap.Find(WinningTeamID);
		if (FoundEnvID)
		{
			EnvironmentID = *FoundEnvID;
		}
	}
	if (EnvironmentID < 0 && LosingTeamID >= 0)
	{
		int32* FoundEnvID = TeamToEnvironmentMap.Find(LosingTeamID);
		if (FoundEnvID)
		{
			EnvironmentID = *FoundEnvID;
		}
	}

	// Fallback to environment 0 if still not determined
	if (EnvironmentID < 0)
	{
		EnvironmentID = 0;
		UE_LOG(LogTemp, Warning, TEXT("[EndEpisode] Could not determine EnvironmentID, using default: 0"));
	}

	// Check if this environment is already ending
	if (IsEnvironmentEpisodeEnding(EnvironmentID))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EndEpisode] ENV %d already ending, skipping"), EnvironmentID);
		return;
	}

	// Determine if timeout
	bool bIsTimeout = (WinningTeamID == -1 && LosingTeamID == -1);

	// v8.5: Set per-environment termination flags
	SetEnvironmentTerminationFlags(EnvironmentID, true, true, bIsTimeout);

	// Also update global flags for backward compatibility
	bEpisodeEnding = true;
	bLastEpisodeWasTerminated = true;
	bLastEpisodeWasTimeout = bIsTimeout;

	// Calculate episode duration for this environment
	float* EnvStartTime = EnvironmentEpisodeStartTimes.Find(EnvironmentID);
	float EpisodeDuration = EnvStartTime ? (GetWorld()->GetTimeSeconds() - *EnvStartTime) : 0.0f;

	// If EnvironmentID not provided, try to determine from teams
	if (EnvironmentID < 0 && WinningTeamID >= 0)
	{
		int32* FoundEnvID = TeamToEnvironmentMap.Find(WinningTeamID);
		if (FoundEnvID)
		{
			EnvironmentID = *FoundEnvID;
		}
	}
	if (EnvironmentID < 0 && LosingTeamID >= 0)
	{
		int32* FoundEnvID = TeamToEnvironmentMap.Find(LosingTeamID);
		if (FoundEnvID)
		{
			EnvironmentID = *FoundEnvID;
		}
	}

	// Fallback to environment 0 if still not determined
	if (EnvironmentID < 0)
	{
		EnvironmentID = 0;
		UE_LOG(LogTemp, Warning, TEXT("[EndEpisode] Could not determine EnvironmentID, using default: 0"));
	}

	// Get episode number from per-environment tracking
	int32 EpisodeNumber = CurrentEpisode;  // Fallback to global
	int32* EnvEpisode = EnvironmentEpisodes.Find(EnvironmentID);
	if (EnvEpisode)
	{
		EpisodeNumber = *EnvEpisode;
	}

	// Build episode result
	LastEpisodeResult.EpisodeNumber = EpisodeNumber;
	LastEpisodeResult.WinningTeamID = WinningTeamID;
	LastEpisodeResult.LosingTeamID = LosingTeamID;
	LastEpisodeResult.EpisodeDuration = EpisodeDuration;
	LastEpisodeResult.TotalSteps = CurrentStep;

	UE_LOG(LogTemp, Warning, TEXT("===== ENV %d: EPISODE %d ENDED ====="), EnvironmentID, EpisodeNumber);
	UE_LOG(LogTemp, Warning, TEXT("  Winner: Team %d"), WinningTeamID);
	UE_LOG(LogTemp, Warning, TEXT("  Loser: Team %d"), LosingTeamID);
	UE_LOG(LogTemp, Warning, TEXT("  Duration: %.2fs"), EpisodeDuration);
	UE_LOG(LogTemp, Warning, TEXT("  Steps: %d"), CurrentStep);
	UE_LOG(LogTemp, Warning, TEXT("============================="));

	// Distribute rewards to team leaders
	for (const auto& Pair : RegisteredTeams)
	{
		int32 TeamID = Pair.Key;
		UTeamLeaderComponent* TeamLeader = Pair.Value.TeamLeader;

		if (TeamLeader)
		{
			float Reward = 0.0f;
			if (TeamID == WinningTeamID)
			{
				Reward = WinReward;
			}
			else if (TeamID == LosingTeamID)
			{
				Reward = LosePenalty;
			}
		}

		// Also notify followers via their FollowerAgentComponent
		for (AActor* Member : Pair.Value.TeamMembers)
		{
			if (!Member) continue;

			UFollowerAgentComponent* FollowerComp = Member->FindComponentByClass<UFollowerAgentComponent>();
			if (FollowerComp)
			{
				float FollowerReward = (TeamID == WinningTeamID) ? WinReward :
									   (TeamID == LosingTeamID) ? LosePenalty : 0.0f;
				FollowerComp->OnEpisodeEnded(FollowerReward);
			}
		}
	}

	// Broadcast episode ended event
	UE_LOG(LogTemp, Warning, TEXT("[EPISODE END] Broadcasting OnEpisodeEnded(EnvID=%d) to bound listeners..."),
		EnvironmentID);
	UE_LOG(LogTemp, Warning, TEXT("[EPISODE END] OnEpisodeEnded.IsBound() = %s"),
		OnEpisodeEnded.IsBound() ? TEXT("TRUE") : TEXT("FALSE"));
	OnEpisodeEnded.Broadcast(EnvironmentID, LastEpisodeResult);
	UE_LOG(LogTemp, Warning, TEXT("[EPISODE END] OnEpisodeEnded.Broadcast() completed"));
}

void ASimulationManagerGameMode::StartNewEpisode(int32 EnvironmentID, int32 EnvironmentEpisodeNumber)
{
	UE_LOG(LogTemp, Warning, TEXT("[StartNewEpisode] Called (EnvID: %d, Episode: %d)"),
		EnvironmentID, EnvironmentEpisodeNumber);


	// CRITICAL: Early validation to prevent crashes during reset
	if (RegisteredTeams.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("===== EPISODE START FAILED ====="));
		UE_LOG(LogTemp, Error, TEXT("No teams registered! Cannot start episode."));
		UE_LOG(LogTemp, Error, TEXT("Make sure agents are spawned and teams are registered before starting episodes."));
		return;
	}

	if (EnvironmentID < 0)
	{
		UE_LOG(LogTemp, Error, TEXT("===== EPISODE START FAILED ====="));
		UE_LOG(LogTemp, Error, TEXT("Invalid EnvironmentID: %d"), EnvironmentID);
		return;
	}

	// Clear timer
	GetWorldTimerManager().ClearTimer(EpisodeRestartTimerHandle);

	// v8.5 VECTORIZED TRAINING: Per-environment episode tracking
	EnvironmentEpisodes.Add(EnvironmentID, EnvironmentEpisodeNumber);
	EnvironmentSteps.Add(EnvironmentID, 0);
	EnvironmentEpisodeStartTimes.Add(EnvironmentID, GetWorld()->GetTimeSeconds());
	EnvironmentEpisodeGameTimes.Add(EnvironmentID, 0.0f);

	// v8.5: Clear per-environment termination flags for this environment
	SetEnvironmentTerminationFlags(EnvironmentID, false, false, false);

	// Reset global state (used by legacy code, but per-environment tracking is preferred)
	CurrentStep = 0;
	EpisodeStartTime = GetWorld()->GetTimeSeconds();
	EpisodeGameTime = 0.0f;  // v8.0 TIMING FIX: Reset accumulated game time
	bEpisodeEnding = false;  // LEGACY: Keep for backward compatibility
	bLastEpisodeWasTerminated = false;  // LEGACY: Keep for backward compatibility
	bLastEpisodeWasTimeout = false;  // LEGACY: Keep for backward compatibility

	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [EPISODE START] ENV %d: EPISODE %d                                ║"), EnvironmentID, EnvironmentEpisodeNumber);
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ EPISODE CONFIGURATION:                                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("║   MaxEpisodeDuration: %.1fs (0 = unlimited)                       ║"), MaxEpisodeDuration);
	UE_LOG(LogTemp, Warning, TEXT("║   MaxStepsPerEpisode: %d (0 = unlimited)                          ║"), MaxStepsPerEpisode);
	UE_LOG(LogTemp, Warning, TEXT("║   Tick Interval: %.2fs (%.1f Hz)                                  ║"), PrimaryActorTick.TickInterval, 1.0f / PrimaryActorTick.TickInterval);
	UE_LOG(LogTemp, Warning, TEXT("║   bEnableContinuousTraining: %s                                    ║"), bEnableContinuousTraining ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("║                                                                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ EXPECTED BEHAVIOR:                                                ║"));
	if (MaxEpisodeDuration > 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("║   Episode will TIMEOUT at %.1fs if no team wins                   ║"), MaxEpisodeDuration);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("║   No time limit - episode runs until team elimination/objective  ║"));
	}
	if (MaxStepsPerEpisode > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("║   Episode will TRUNCATE at step %d                                ║"), MaxStepsPerEpisode);
	}
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════════════╝"));

	// Reset agent health and positions
	for (auto& Pair : RegisteredTeams)
	{
		int32 TeamID = Pair.Key;
		FTeamInfo& TeamInfo = Pair.Value;

		UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Resetting Team %d - %d members in array"),
			TeamID, TeamInfo.TeamMembers.Num());

		UTeamLeaderComponent* TeamLeader = TeamInfo.TeamLeader;


		// Reset team members
		for (AActor* Member : TeamInfo.TeamMembers)
		{
			if (!Member)
			{
				UE_LOG(LogTemp, Error, TEXT("SimulationManager: Team %d has NULL member in array"), TeamID);
				continue;
			}

			if (!IsValid(Member))
			{
				UE_LOG(LogTemp, Error, TEXT("SimulationManager: Team %d member '%s' is INVALID"),
					TeamID, *Member->GetName());
				continue;
			}

			UHealthComponent* HealthComp = Member->FindComponentByClass<UHealthComponent>();
			bool bWasDead = HealthComp ? HealthComp->IsDead() : false;

			// Reset to spawn position
			FTransform* SpawnTransform = SpawnTransforms.Find(Member);
			if (SpawnTransform)
			{
				Member->SetActorTransform(*SpawnTransform, false, nullptr, ETeleportType::ResetPhysics);
				UE_LOG(LogTemp, Log, TEXT("  → Reset position"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("  → No spawn transform stored"));
			}

			// Reset health (resurrects dead agents)
			if (HealthComp)
			{
				HealthComp->ResetHealth();
			}

			// Reset combat stats (kill count, etc.)
			if (HealthComp)
			{
				HealthComp->ResetCombatStats();
			}

			// Mark agent as alive (syncs FollowerAgentComponent state)
			UFollowerAgentComponent* FollowerComp = Member->FindComponentByClass<UFollowerAgentComponent>();
			if (FollowerComp)
			{
				FollowerComp->MarkAsAlive();
			}
		}
	}

	// v8.0: Reset all ObjectiveActors in the world
	// CRITICAL: Without this, objectives retain durability damage from previous episodes
	TArray<AActor*> FoundObjectives;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AObjectiveActor::StaticClass(), FoundObjectives);

	for (AActor* ObjActor : FoundObjectives)
	{
		AObjectiveActor* Objective = Cast<AObjectiveActor>(ObjActor);
		if (Objective)
		{
			Objective->ResetDurability();
			UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Reset ObjectiveActor '%s' (Team %d) to full durability"),
				*Objective->GetName(), Objective->OwnerTeamID);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Reset %d ObjectiveActor(s)"), FoundObjectives.Num());


	// Count agents in THIS environment only
	int32 ThisEnvAgentCount = 0;
	int32 ThisEnvAliveCount = 0;
	TMap<int32, int32> TeamAgentCounts;  // TeamID -> Agent count (for detailed logging)

	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [EPISODE READY] ENV %d EPISODE %d - AGENT COUNT VERIFICATION     ║"), EnvironmentID, EnvironmentEpisodeNumber);
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════════════╣"));

	// Iterate through teams in THIS environment
	for (const auto& TeamPair : RegisteredTeams)
	{
		int32 TeamID = TeamPair.Key;
		int32 TeamEnvID = GetEnvironmentIDForTeam(TeamID);

		// Skip teams not in this environment
		if (TeamEnvID != EnvironmentID)
		{
			continue;
		}

		const FTeamInfo& TeamInfo = TeamPair.Value;
		int32 TeamAgentCount = TeamInfo.TeamMembers.Num();
		int32 TeamAliveCount = GetAliveAgentCount(TeamID);

		ThisEnvAgentCount += TeamAgentCount;
		ThisEnvAliveCount += TeamAliveCount;
		TeamAgentCounts.Add(TeamID, TeamAgentCount);

		UE_LOG(LogTemp, Warning, TEXT("║   Team %d: %d agents (%d alive)                                    ║"),
			TeamID, TeamAgentCount, TeamAliveCount);
	}

	UE_LOG(LogTemp, Warning, TEXT("║                                                                   ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ ENV %d TOTAL: %d agents (%d alive)                                ║"), EnvironmentID, ThisEnvAgentCount, ThisEnvAliveCount);

	// Expected agent count check for THIS environment
	int32 ExpectedAgentsThisEnv = 8;  // 4v4 (2 teams × 4 followers each)
	int32 MissingAgents = ExpectedAgentsThisEnv - ThisEnvAgentCount;

	if (ThisEnvAgentCount != ExpectedAgentsThisEnv)
	{
		UE_LOG(LogTemp, Error, TEXT("║ ⚠️  WARNING: Expected %d agents, but found %d!                    ║"),
			ExpectedAgentsThisEnv, ThisEnvAgentCount);
		UE_LOG(LogTemp, Error, TEXT("║     Missing %d agents - checking per-team breakdown...             ║"),
			MissingAgents);

		// Detailed per-team diagnostic
		for (const auto& TeamCountPair : TeamAgentCounts)
		{
			int32 TeamID = TeamCountPair.Key;
			int32 Count = TeamCountPair.Value;
			if (Count != 4)
			{
				UE_LOG(LogTemp, Error, TEXT("║     → Team %d has %d agents (expected 4) - MISSING %d!            ║"),
					TeamID, Count, 4 - Count);
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("║ ✅ Agent count matches expected: %d agents                         ║"), ThisEnvAgentCount);
	}

	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════════════╝"));

	// 🔥 CRITICAL FIX: Broadcast OnEpisodeStarted so agents send new observations to Python
	// Without this, Python's poll() after reset() blocks indefinitely waiting for observations
	UE_LOG(LogTemp, Warning, TEXT("[EPISODE START] Broadcasting OnEpisodeStarted(EnvID=%d, Episode=%d)..."),
		EnvironmentID, EnvironmentEpisodeNumber);
	OnEpisodeStarted.Broadcast(EnvironmentID, EnvironmentEpisodeNumber);
	UE_LOG(LogTemp, Warning, TEXT("[EPISODE START] OnEpisodeStarted.Broadcast() completed - agents can now send observations"));
}

//------------------------------------------------------------------------------
// CONTINUOUS TRAINING (Respawn System)
//------------------------------------------------------------------------------

void ASimulationManagerGameMode::ScheduleTeamRespawn(int32 TeamID, float Delay)
{
	if (!bEnableContinuousTraining)
	{
		return;
	}

	// Check if team is already in respawn queue
	if (RespawningTeams.Contains(TeamID))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Team %d already scheduled for respawn, ignoring duplicate request"), TeamID);
		return;
	}

	// Verify team exists
	if (!RegisteredTeams.Contains(TeamID))
	{
		UE_LOG(LogTemp, Error, TEXT("SimulationManager: Cannot schedule respawn for unknown team %d"), TeamID);
		return;
	}

	// Mark team as respawning
	RespawningTeams.Add(TeamID);

	// Schedule respawn timer
	FTimerHandle& TimerHandle = TeamRespawnTimers.FindOrAdd(TeamID);
	GetWorldTimerManager().SetTimer(
		TimerHandle,
		[this, TeamID]()
		{
			RespawnTeam(TeamID);
		},
		Delay,
		false
	);

	UE_LOG(LogTemp, Warning, TEXT("🔄 SimulationManager: Team %d scheduled to respawn in %.1fs"), TeamID, Delay);
}

void ASimulationManagerGameMode::RespawnTeam(int32 TeamID)
{
	if (!bEnableContinuousTraining)
	{
		return;
	}

	FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	if (!TeamInfo)
	{
		UE_LOG(LogTemp, Error, TEXT("SimulationManager: Cannot respawn unknown team %d"), TeamID);
		RespawningTeams.Remove(TeamID);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("===== RESPAWNING TEAM %d ====="), TeamID);

	int32 RespawnedCount = 0;

	// Reset each team member
	for (AActor* Member : TeamInfo->TeamMembers)
	{
		if (!Member || !IsValid(Member))
		{
			continue;
		}

		// Reset to spawn position
		FTransform* SpawnTransform = SpawnTransforms.Find(Member);
		if (SpawnTransform)
		{
			Member->SetActorTransform(*SpawnTransform, false, nullptr, ETeleportType::ResetPhysics);
			UE_LOG(LogTemp, Log, TEXT("  → %s: Reset position"), *Member->GetName());
		}

		// Reset health (resurrects dead agents)
		UHealthComponent* HealthComp = Member->FindComponentByClass<UHealthComponent>();
		if (HealthComp)
		{
			HealthComp->ResetHealth();
			HealthComp->ResetCombatStats();
		}

		// Mark agent as alive (syncs FollowerAgentComponent state)
		UFollowerAgentComponent* FollowerComp = Member->FindComponentByClass<UFollowerAgentComponent>();
		if (FollowerComp)
		{
			FollowerComp->MarkAsAlive();
		}

		// Restart StateTree
		UFollowerStateTreeComponent* StateTreeComp = Member->FindComponentByClass<UFollowerStateTreeComponent>();
		if (StateTreeComp)
		{
			StateTreeComp->OnFollowerRespawned();
		}

		RespawnedCount++;
	}

	// Remove from respawning queue
	RespawningTeams.Remove(TeamID);

	UE_LOG(LogTemp, Warning, TEXT("✅ Team %d respawned: %d agents back in action!"), TeamID, RespawnedCount);
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

//------------------------------------------------------------------------------
// OBJECTIVE MANAGEMENT
//------------------------------------------------------------------------------

AActor* ASimulationManagerGameMode::FindObjectiveActor()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Search for actors with the objective tag
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsWithTag(World, ObjectiveActorTag, FoundActors);

	if (FoundActors.Num() > 0)
	{
		if (FoundActors.Num() > 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SimulationManager] Found %d actors with tag '%s', using first one: %s"),
				FoundActors.Num(), *ObjectiveActorTag.ToString(), *FoundActors[0]->GetName());
		}
		return FoundActors[0];
	}

	return nullptr;
}