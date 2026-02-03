// EnvRegistryComponent.cpp - Implementation

#include "Schola/Components/EnvRegistryComponent.h"
#include "Team/ObjectiveActor.h"
#include "Team/Components/TeamLeaderComponent.h"

UEnvRegistryComponent::UEnvRegistryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}



//------------------------------------------------------------------------------
// TEAM REGISTRATION
//------------------------------------------------------------------------------

bool UEnvRegistryComponent::RegisterTeam(int32 TeamID)
{

	// Add to registered teams
	RegisteredTeamIDs.AddUnique(TeamID);

	

	SetMutual();

	return true;
}

bool UEnvRegistryComponent::IsTeamRegistered(int32 TeamID) const
{
	return RegisteredTeamIDs.Contains(TeamID);
}

//------------------------------------------------------------------------------
// OBJECTIVE REGISTRATION
//------------------------------------------------------------------------------

bool UEnvRegistryComponent::RegisterObjectiveActor(AObjectiveActor* Objective)
{
	if (!Objective)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EnvRegistry] Cannot register null objective"));
		return false;
	}

	// Check if objective belongs to a team managed by this environment
	int32 ObjectiveTeamID = Objective->GetTeamInfo().TeamID;
	if (!IsTeamRegistered(ObjectiveTeamID))
	{
		UE_LOG(LogTemp, VeryVerbose, TEXT("[EnvRegistry] Objective for Team %d not registered (not in this environment)"),
			ObjectiveTeamID);
		return false;
	}

	// Add to registered objectives
	RegisteredObjectives.AddUnique(Objective);

	UE_LOG(LogTemp, Log, TEXT("[EnvRegistry] Registered ObjectiveActor '%s' (Team %d)"),
		*Objective->GetName(), ObjectiveTeamID);

	return true;
}

bool UEnvRegistryComponent::UnRegisterObjectiveActor(AObjectiveActor* Objective)
{
	if (!Objective)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EnvRegistry] Cannot register null objective"));
		return false;
	}

	int32 ObjectiveTeamID = Objective->GetTeamInfo().TeamID;

	RegisteredObjectives.Remove(Objective);

	return true;
}


AObjectiveActor* UEnvRegistryComponent::GetFriendlyObjective(int32 TeamID) const
{
	// Find objective with matching team ID
	for (AObjectiveActor* Objective : RegisteredObjectives)
	{
		if (Objective && Objective->GetTeamInfo().TeamID == TeamID)
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
		if (Objective && EnemyTeamIDs.Contains(Objective->GetTeamInfo().TeamID))
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
	UE_LOG(LogTemp, Warning, TEXT("[EnvRegistry] Setting mutual adversaries for Environment"));

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

