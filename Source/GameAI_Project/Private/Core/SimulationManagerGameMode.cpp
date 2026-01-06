#include "Core/SimulationManagerGameMode.h"
#include "Team/TeamLeaderComponent.h"
#include "Team/FollowerAgentComponent.h"
#include "Combat/HealthComponent.h"
#include "StateTree/FollowerStateTreeComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"

ASimulationManagerGameMode::ASimulationManagerGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f; // 10 Hz
	
	// Default to false - wait for external signal (e.g. from Python via Schola)
	bAutoStartSimulation = false;
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

	if (bAutoStartSimulation)
	{
		StartSimulation();
	}
}

void ASimulationManagerGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bSimulationRunning && !bEpisodeEnding)
	{
		// Auto-increment step counter (each tick = 1 step)
		CurrentStep++;

		// Check for max steps termination
		if (MaxStepsPerEpisode > 0 && CurrentStep >= MaxStepsPerEpisode)
		{
			CheckEpisodeTermination();
		}

		// Check for max duration termination (10 minutes by default, matches Python MAX_EPISODE_DURATION)
		if (MaxEpisodeDuration > 0.0f)
		{
			float EpisodeElapsed = GetWorld()->GetTimeSeconds() - EpisodeStartTime;
			if (EpisodeElapsed >= MaxEpisodeDuration)
			{
				CheckEpisodeTermination();
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
				}
			}
		}

		UpdateStatistics();

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

	FTeamInfo* TeamInfo = RegisteredTeams.Find(TeamID);
	if (!TeamInfo)
	{
		UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Cannot register member - team %d not found"), TeamID);
		return false;
	}

	// Remove from old team if exists
	int32* OldTeamID = ActorToTeamMap.Find(Agent);
	if (OldTeamID)
	{
		UnregisterTeamMember(*OldTeamID, Agent);
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

	UE_LOG(LogTemp, Log, TEXT("SimulationManager: Registered %s to team %d"), *Agent->GetName(), TeamID);
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

void ASimulationManagerGameMode::UpdateStatistics()
{
	// Statistics are computed on-demand in GetSimulationStats()
	// This function can be used for periodic updates if needed
}

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
// EPISODE MANAGEMENT
//------------------------------------------------------------------------------

void ASimulationManagerGameMode::OnAgentDied(const FDeathEventData& DeathEvent)
{
	if (!DeathEvent.DeadActor || bEpisodeEnding)
	{
		return;
	}

	int32 TeamID = GetTeamIDForActor(DeathEvent.DeadActor);
	int32 AliveCount = GetAliveAgentCount(TeamID);

	UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Agent '%s' (Team %d) died - %d agents remaining alive (Killed by %s)"),
		*DeathEvent.DeadActor->GetName(), TeamID, AliveCount,
		DeathEvent.Killer ? *DeathEvent.Killer->GetName() : TEXT("Unknown"));

	// Check if this death causes episode termination
	CheckEpisodeTermination();
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
	if (bEpisodeEnding)
	{
		return;
	}

	// Check max steps
	if (MaxStepsPerEpisode > 0 && CurrentStep >= MaxStepsPerEpisode)
	{
		UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Episode %d - Max steps reached (%d)"),
			CurrentEpisode, MaxStepsPerEpisode);
		EndEpisode(-1, -1); // Draw
		return;
	}

	// Check max duration (10 minutes by default, matches Python MAX_EPISODE_DURATION)
	if (MaxEpisodeDuration > 0.0f)
	{
		float EpisodeElapsed = GetWorld()->GetTimeSeconds() - EpisodeStartTime;
		if (EpisodeElapsed >= MaxEpisodeDuration)
		{
			UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Episode %d - Max duration reached (%.1fs)"),
				CurrentEpisode, EpisodeElapsed);
			EndEpisode(-1, -1); // Draw (timeout)
			return;
		}
	}

	// Check for team elimination
	TArray<int32> AllTeamIDs = GetAllTeamIDs();
	TArray<int32> AliveTeams;
	TArray<int32> EliminatedTeams;

	for (int32 TeamID : AllTeamIDs)
	{
		int32 AliveCount = GetAliveAgentCount(TeamID);
		bool bEliminated = IsTeamEliminated(TeamID);

		UE_LOG(LogTemp, Log, TEXT("CheckEpisodeTermination: Team %d has %d alive agents (Eliminated: %s)"),
			TeamID, AliveCount, bEliminated ? TEXT("YES") : TEXT("NO"));

		if (bEliminated)
		{
			EliminatedTeams.Add(TeamID);
		}
		else
		{
			AliveTeams.Add(TeamID);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("CheckEpisodeTermination: %d teams alive, %d eliminated (Total: %d)"),
		AliveTeams.Num(), EliminatedTeams.Num(), AllTeamIDs.Num());

	// Handle team elimination
	if (AliveTeams.Num() <= 1 && AllTeamIDs.Num() > 1)
	{
		int32 WinningTeamID = AliveTeams.Num() == 1 ? AliveTeams[0] : -1;
		int32 LosingTeamID = EliminatedTeams.Num() > 0 ? EliminatedTeams[0] : -1;

		// CONTINUOUS TRAINING MODE: Respawn losing team instead of ending episode
		if (bEnableContinuousTraining && WinningTeamID != -1 && LosingTeamID != -1)
		{
			UE_LOG(LogTemp, Warning, TEXT("🔄 SimulationManager: Team %d eliminated! Team %d wins this round (Continuous Training)"),
				LosingTeamID, WinningTeamID);

			// Award objective capture bonus to winning team
			AwardObjectiveCaptureBonus(WinningTeamID);

			// Schedule losing team to respawn
			ScheduleTeamRespawn(LosingTeamID, RespawnDelay);
		}
		// TRADITIONAL MODE: End episode on team elimination
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Episode %d - Team %d eliminated! Winner: %d"),
				CurrentEpisode, LosingTeamID, WinningTeamID);

			EndEpisode(WinningTeamID, LosingTeamID);
		}
	}
}

void ASimulationManagerGameMode::EndEpisode(int32 WinningTeamID, int32 LosingTeamID)
{
	if (bEpisodeEnding)
	{
		return;
	}

	bEpisodeEnding = true;
	float EpisodeDuration = GetWorld()->GetTimeSeconds() - EpisodeStartTime;

	// Build episode result
	LastEpisodeResult.EpisodeNumber = CurrentEpisode;
	LastEpisodeResult.WinningTeamID = WinningTeamID;
	LastEpisodeResult.LosingTeamID = LosingTeamID;
	LastEpisodeResult.EpisodeDuration = EpisodeDuration;
	LastEpisodeResult.TotalSteps = CurrentStep;

	UE_LOG(LogTemp, Warning, TEXT("===== EPISODE %d ENDED ====="), CurrentEpisode);
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
	OnEpisodeEnded.Broadcast(LastEpisodeResult);

	// Auto-restart if enabled
	if (bAutoRestartEpisode)
	{
		GetWorldTimerManager().SetTimer(
			EpisodeRestartTimerHandle,
			this,
			&ASimulationManagerGameMode::StartNewEpisode,
			EpisodeRestartDelay,
			false
		);

		UE_LOG(LogTemp, Log, TEXT("SimulationManager: New episode starting in %.1fs"), EpisodeRestartDelay);
	}
}

void ASimulationManagerGameMode::StartNewEpisode()
{
	// CRITICAL: Early validation to prevent crashes during reset
	if (RegisteredTeams.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("===== EPISODE START FAILED ====="));
		UE_LOG(LogTemp, Error, TEXT("No teams registered! Cannot start episode."));
		UE_LOG(LogTemp, Error, TEXT("Make sure agents are spawned and teams are registered before starting episodes."));
		return;
	}

	// Clear timer
	GetWorldTimerManager().ClearTimer(EpisodeRestartTimerHandle);

	// Increment episode counter
	CurrentEpisode++;
	CurrentStep = 0;
	EpisodeStartTime = GetWorld()->GetTimeSeconds();
	bEpisodeEnding = false;

	UE_LOG(LogTemp, Warning, TEXT("===== EPISODE %d STARTED ====="), CurrentEpisode);

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

			UE_LOG(LogTemp, Warning, TEXT("SimulationManager: Processing %s (Team %d) - WasDead: %s"),
				*Member->GetName(), TeamID, bWasDead ? TEXT("YES") : TEXT("NO"));

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
				UE_LOG(LogTemp, Log, TEXT("  → Reset health (Alive: %s)"),
					HealthComp->IsAlive() ? TEXT("YES") : TEXT("NO"));
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
				UE_LOG(LogTemp, Log, TEXT("  → Marked alive & cleared experiences"));
			}
		}
	}

	// Broadcast episode started event
	OnEpisodeStarted.Broadcast(CurrentEpisode);
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
			UE_LOG(LogTemp, Log, TEXT("  → %s: Reset health (%.0f/%.0f)"),
				*Member->GetName(), HealthComp->GetCurrentHealth(), HealthComp->GetMaxHealth());
		}

		// Mark agent as alive (syncs FollowerAgentComponent state)
		UFollowerAgentComponent* FollowerComp = Member->FindComponentByClass<UFollowerAgentComponent>();
		if (FollowerComp)
		{
			FollowerComp->MarkAsAlive();
			UE_LOG(LogTemp, Log, TEXT("  → %s: Marked alive & cleared experiences"), *Member->GetName());
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
