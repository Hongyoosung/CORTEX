// Copyright Epic Games, Inc. All Rights Reserved.

#include "Team/DESquadManager.h"
#include "Characters/DECharacter.h"
#include "Types/DERewardTypes.h"
#include "Actors/DECapturePoint.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"


// ─────────────────────────────────────────────────────────────────────────────
// Debug helper — a module-local weak pointer to the most-recently created
// UDESquadManager (Team 0 by convention) for console commands.
// ─────────────────────────────────────────────────────────────────────────────

static TWeakObjectPtr<UDESquadManager> GDebugSquadManager;


// ─────────────────────────────────────────────────────────────────────────────
// Constructor — default role assignments + one-time console command registration
// ─────────────────────────────────────────────────────────────────────────────

UDESquadManager::UDESquadManager()
{
	CurrentRoleAssignments = {
		EDEStrategyType::Assault,
		EDEStrategyType::Assault,
		EDEStrategyType::Defend,
		EDEStrategyType::Defend,
		EDEStrategyType::Support
	};

	static FAutoConsoleCommand CmdSquadState(
		TEXT("moc.debug.squadstate"),
		TEXT("Print current role assignments for Team 0"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UDESquadManager* SM = GDebugSquadManager.Get())
			{
				UE_LOG(LogTemp, Display, TEXT("=== Squad Commander State (Team %d) ==="), SM->TeamID);
				UE_LOG(LogTemp, Display, TEXT("  Episode: %d"), SM->EpisodeCount);

				for (int32 i = 0; i < SM->CurrentRoleAssignments.Num(); ++i)
				{
					UE_LOG(LogTemp, Display, TEXT("  Agent %d: %s"),
						i, *UEnum::GetValueAsString(SM->CurrentRoleAssignments[i]));
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("No DESquadManager available for debug"));
			}
		})
	);
}


// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

void UDESquadManager::Initialize(int32 InTeamID)
{
	TeamID = InTeamID;

	// Register as the debug target (last-created wins — fine for single-env sessions)
	GDebugSquadManager = this;
}

void UDESquadManager::Configure(const FDESquadConfig& InConfig)
{
	Config = InConfig;
}

void UDESquadManager::BindCapturePoints(const TArray<ADECapturePoint*>& CapturePoints)
{
	CachedCapturePoints = CapturePoints;
	UE_LOG(LogTemp, Log, TEXT("[DESquadManager] Team %d: Bound %d capture points"),
		TeamID, CapturePoints.Num());
}


// ─────────────────────────────────────────────────────────────────────────────
// Episode Management
// ─────────────────────────────────────────────────────────────────────────────

void UDESquadManager::Reset(const TArray<ADECharacter*>& TeamAgents)
{
	UE_LOG(LogTemp, Log, TEXT("[DESquadManager] Resetting planner for Team %d"), TeamID);

	ValidationTickCounter = 0.0f;

	CurrentRoleAssignments.Empty();
	CurrentRoleAssignments.Init(EDEStrategyType::Assault, 5);

	EpisodeCount++;

	// Assign strategies immediately so the first Schola observation reflects them.
	SampleRandomTacticalPlay(TeamAgents);

	UE_LOG(LogTemp, Log, TEXT("[DESquadManager] Team %d reset complete"), TeamID);
}


// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void UDESquadManager::TickPlanner(float DeltaTime,
                                const TArray<ADECharacter*>& TeamAgents,
                                const TArray<ADECharacter*>& EnemyAgents)
{
	ValidationTickCounter += DeltaTime;
	if (ValidationTickCounter >= 2.0f)
	{
		ValidationTickCounter = 0.0f;

		if (CurrentRoleAssignments.Num() != 5)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Validation] Team %d: role count=%d (expected 5)"),
				TeamID, CurrentRoleAssignments.Num());
		}
	}
}


// ─────────────────────────────────────────────────────────────────────────────
// Role Query
// ─────────────────────────────────────────────────────────────────────────────

EDEStrategyType UDESquadManager::GetAgentStrategy(int32 AgentIndex) const
{
	return (AgentIndex >= 0 && AgentIndex < CurrentRoleAssignments.Num())
		? CurrentRoleAssignments[AgentIndex]
		: EDEStrategyType::Assault;
}


// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

void UDESquadManager::SampleRandomTacticalPlay(const TArray<ADECharacter*>& TeamAgents)
{
	// Round-robin strategy wheel: agent i → StrategyWheel[(i + EpisodeCount) % 3].
	// Over every 3 episodes each agent rotates through all 3 strategies
	// for balanced RL training signal.
	static const EDEStrategyType StrategyWheel[3] = {
		EDEStrategyType::Assault,
		EDEStrategyType::Defend,
		EDEStrategyType::Support
	};

	TArray<EDEStrategyType> Roles;
	Roles.SetNum(5);
	int32 nA = 0, nD = 0, nS = 0;
	for (int32 i = 0; i < 5; i++)
	{
		const EDEStrategyType S = StrategyWheel[(i + EpisodeCount) % 3];
		Roles[i] = S;
		if      (S == EDEStrategyType::Assault) nA++;
		else if (S == EDEStrategyType::Defend)  nD++;
		else                                   nS++;
	}

	DistributeRoles(Roles, TeamAgents);
	CurrentRoleAssignments = Roles;

	UE_LOG(LogTemp, Log,
		TEXT("[DESquadManager] Phase 1 RL: Episode %d round-robin → %dA %dD %dS"),
		EpisodeCount, nA, nD, nS);
}


void UDESquadManager::DistributeRoles(const TArray<EDEStrategyType>& Roles,
                                    const TArray<ADECharacter*>& TeamAgents) const
{
	for (int32 i = 0; i < FMath::Min(Roles.Num(), TeamAgents.Num()); ++i)
	{
		ADECharacter* Agent = TeamAgents[i];
		if (Agent && Agent->IsAlive())
		{
			Agent->SetCommandedStrategy(Roles[i]);

			if (Config.bShowDebugInfo)
			{
				UE_LOG(LogTemp, Log, TEXT("[DESquadManager] Team %d Agent %d → %s"),
					TeamID, i, *UEnum::GetValueAsString(Roles[i]));
			}
		}
	}
}
