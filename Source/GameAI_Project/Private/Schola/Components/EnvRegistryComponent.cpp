// EnvRegistryComponent.cpp - Implementation

#include "Schola/Components/EnvRegistryComponent.h"
#include "Core/SimulationManagerGameMode.h"
#include "Team/ObjectiveActor.h"
#include "Team/Components/TeamLeaderComponent.h"

UEnvRegistryComponent::UEnvRegistryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UEnvRegistryComponent::Initialize(ASimulationManagerGameMode* Manager)
{
	SimulationManager = Manager;

	if (!SimulationManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[EnvRegistry] Initialize failed - SimulationManager is null"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[EnvRegistry] Initialized with SimulationManager: %s"),
		*SimulationManager->GetName());
}

//------------------------------------------------------------------------------
// TEAM REGISTRATION
//------------------------------------------------------------------------------

bool UEnvRegistryComponent::RegisterTeam(int32 TeamID)
{

	// Add to registered teams
	RegisteredTeamIDs.AddUnique(TeamID);

	// AUTO-SETUP: If all configured teams are now registered, establish adversarial relationships
	if (TeamIDs.Num() > 0 && RegisteredTeamIDs.Num() == TeamIDs.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EnvRegistry] All teams registered (%d/%d) - auto-calling SetMutual()"),
			RegisteredTeamIDs.Num(), TeamIDs.Num());
		SetMutual();
	}

	return true;
}

bool UEnvRegistryComponent::IsTeamRegistered(int32 TeamID) const
{
	return RegisteredTeamIDs.Contains(TeamID);
}

//------------------------------------------------------------------------------
// OBJECTIVE REGISTRATION
//------------------------------------------------------------------------------

void UEnvRegistryComponent::RegisterObjectiveActor(AObjectiveActor* Objective)
{
	if (!Objective)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EnvRegistry] Cannot register null objective"));
		return;
	}

	// Check if objective belongs to a team managed by this environment
	int32 ObjectiveTeamID = Objective->OwnerTeamID;
	if (!IsTeamRegistered(ObjectiveTeamID))
	{
		UE_LOG(LogTemp, VeryVerbose, TEXT("[EnvRegistry] Objective for Team %d not registered (not in this environment)"),
			ObjectiveTeamID);
		return;
	}

	// Add to registered objectives
	RegisteredObjectives.AddUnique(Objective);

	UE_LOG(LogTemp, Log, TEXT("[EnvRegistry] Registered ObjectiveActor '%s' (Team %d) to Environment %d"),
		*Objective->GetName(), ObjectiveTeamID, EnvironmentID);
}

AObjectiveActor* UEnvRegistryComponent::GetFriendlyObjective(int32 TeamID) const
{
	// Find objective with matching team ID
	for (AObjectiveActor* Objective : RegisteredObjectives)
	{
		if (Objective && Objective->OwnerTeamID == TeamID)
		{
			return Objective;
		}
	}

	return nullptr;
}

AObjectiveActor* UEnvRegistryComponent::GetHostileObjective(int32 TeamID) const
{
	// Get enemy team IDs for this team
	TArray<int32> EnemyTeamIDs = GetEnemyTeamIDs(TeamID);

	// Find objective owned by an enemy team
	for (AObjectiveActor* Objective : RegisteredObjectives)
	{
		if (Objective && EnemyTeamIDs.Contains(Objective->OwnerTeamID))
		{
			return Objective;
		}
	}

	return nullptr;
}

//------------------------------------------------------------------------------
// ADVERSARIAL RELATIONSHIPS
//------------------------------------------------------------------------------

void UEnvRegistryComponent::SetMutual()
{
	if (RegisteredTeamIDs.Num() < 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EnvRegistry] SetMutual requires at least 2 teams (found %d)"),
			RegisteredTeamIDs.Num());
		return;
	}

	// Clear existing adversarial table
	AdversarialTable.Empty();
	UE_LOG(LogTemp, Warning, TEXT("[EnvRegistry] Setting mutual adversaries for Environment %d"), EnvironmentID);

	// For each team, all other teams in this environment are enemies
	for (int32 TeamID : RegisteredTeamIDs)
	{
		FEnemyTeamList EnemyList;  // 구조체 사용
		for (int32 OtherTeamID : RegisteredTeamIDs)
		{
			if (OtherTeamID != TeamID)
			{
				EnemyList.EnemyTeamIDs.Add(OtherTeamID);  // 구조체 내부 배열에 추가
			}
		}
		AdversarialTable.Add(TeamID, EnemyList);
		UE_LOG(LogTemp, Log, TEXT("[EnvRegistry] Team %d enemies: %s"),
			TeamID, *FString::JoinBy(EnemyList.EnemyTeamIDs, TEXT(", "), [](int32 ID) { return FString::FromInt(ID); }));
	}

	UE_LOG(LogTemp, Warning, TEXT("[EnvRegistry] Mutual adversarial relationships established"));
}


TArray<int32> UEnvRegistryComponent::GetEnemyTeamIDs(int32 TeamID) const
{
	const FEnemyTeamList* EnemyList = AdversarialTable.Find(TeamID);
	return EnemyList ? EnemyList->EnemyTeamIDs : TArray<int32>();
}

