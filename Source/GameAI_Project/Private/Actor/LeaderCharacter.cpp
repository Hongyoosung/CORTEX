// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actor/LeaderCharacter.h"
#include "Core/SimulationManagerGameMode.h"
#include "Team/Components/TeamManagerComponent.h"
#include "Team/Components/StrategicPlannerComponent.h"
#include "Team/ObjectiveActor.h"
#include "Util/Components/VisualLoggerComponent.h"
#include "Team/TeamTypes.h"
#include "Kismet/GameplayStatics.h"


ALeaderCharacter::ALeaderCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	TeamManagerComponent		= CreateDefaultSubobject<UTeamManagerComponent>(TEXT("TeamManagerComponent"));
	StrategicPlannerComponent	= CreateDefaultSubobject<UStrategicPlannerComponent>(TEXT("StrategicPlannerComponent"));
	VisualLoggerComponent		= CreateDefaultSubobject<UVisualLoggerComponent>(TEXT("VisualLoggerComponent"));
}


void ALeaderCharacter::BeginPlay()
{
	Super::BeginPlay();


	//--------------------------------------------------------------------------
	// Initialize manager components
	//--------------------------------------------------------------------------

	if (TeamManagerComponent)
	{
		TeamManagerComponent->OnAllAgentsRegistered_Delegate.AddDynamic(this, &ALeaderCharacter::OnAllAgentsRegisterd);

		UE_LOG(LogTemp, Log, TEXT("[LeaderCharacter] '%s': Objectives discovered"), *GetName());
	}


	// Strategic Planner: Initialize MCTS with configured simulation count
	if (StrategicPlannerComponent)
	{
		StrategicPlannerComponent->InitializeMCTS(StrategicPlannerComponent->MCTSSimulations);
		UE_LOG(LogTemp, Log, TEXT("[LeaderCharacter] '%s': MCTS initialized with %d simulations"),
			*GetName(), StrategicPlannerComponent->MCTSSimulations);
	}


	GameMode = Cast<ASimulationManagerGameMode>(UGameplayStatics::GetGameMode(GetWorld()));
	GameMode->OnSimulationStart_Delegate.AddDynamic(this, &ALeaderCharacter::OnSimulationStart);
	

	UE_LOG(LogTemp, Log, TEXT("[LeaderCharacter] '%s': All manager components initialized"), *GetName());
}


void ALeaderCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}


bool ALeaderCharacter::RegisterFollower(AActor* Follower)
{
	return TeamManagerComponent ? TeamManagerComponent->RegisterFollower(Follower) : false;
}


bool ALeaderCharacter::UnregisterFollower(AActor* Follower)
{
	return TeamManagerComponent ? TeamManagerComponent->UnregisterFollower(Follower) : false;
}


TArray<AActor*> ALeaderCharacter::GetFollowers() const
{
	return TeamManagerComponent ? TeamManagerComponent->GetFollowers() : TArray<AActor*>();
}


int32 ALeaderCharacter::GetFollowerCount() const
{
	return TeamManagerComponent ? TeamManagerComponent->GetFollowerCount() : 0;
}



bool ALeaderCharacter::IsFollowerRegistered(AActor* Follower) const
{
	return TeamManagerComponent ? TeamManagerComponent->IsFollowerRegistered(Follower) : false;
}


//------------------------------------------------------------------------------
// INTELLIGENCE WRAPPERS (delegate to IntelManagerComponent)
//------------------------------------------------------------------------------

void ALeaderCharacter::RegisterEnemy(AActor* Enemy)
{
	if (!TeamManagerComponent)
	{
		return;
	}

	TeamManagerComponent->RegisterEnemy(Enemy);
}


void ALeaderCharacter::UnregisterEnemy(AActor* Enemy)
{
	if (!TeamManagerComponent)
	{
		return;
	}

	TeamManagerComponent->UnregisterEnemy(Enemy);
}


AObjectiveActor* ALeaderCharacter::GetFriendlyObjective() const
{
	return TeamManagerComponent ? TeamManagerComponent->GetFriendlyObjective() : nullptr;
}

AObjectiveActor* ALeaderCharacter::GetHostileObjective() const
{
	return TeamManagerComponent ? TeamManagerComponent->GetHostileObjective() : nullptr;
}


bool ALeaderCharacter::AreObjectivesDiscovered() const
{
	return TeamManagerComponent ? TeamManagerComponent->AreObjectivesDiscovered() : false;
}


int32 ALeaderCharacter::GetTeamID() const
{
	return TeamManagerComponent ? TeamManagerComponent->GetTeamInfo().TeamID : -1;
}


//------------------------------------------------------------------------------
// STRATEGIC PLANNING WRAPPERS (delegate to StrategicPlannerComponent)
//------------------------------------------------------------------------------

void ALeaderCharacter::RunStrategyAssignmentAsync(const TArray<AActor*>& Agents, const TArray<AObjectiveActor*>& Objectives)
{
	if (!StrategicPlannerComponent)
	{
		return;
	}

	StrategicPlannerComponent->RunStrategyAssignmentAsync(Agents, Objectives);
}

bool ALeaderCharacter::IsRunningMCTS() const
{
	return StrategicPlannerComponent ? StrategicPlannerComponent->IsMCTSRunning() : false;
}



void ALeaderCharacter::OnAllAgentsRegisterd()
{
	bool Success = GameMode->RegisterTeam(TeamManagerComponent->GetTeamInfo());

	if (Success)
	{
		UE_LOG(LogTemp, Log, TEXT("[LeaderCharacter] '%s': All agents registered with GameMode"), *GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LeaderCharacter] '%s': Failed to register agents with GameMode"), *GetName());
	}
}

void ALeaderCharacter::OnSimulationStart()
{
	AObjectiveActor* Friendly;
	AObjectiveActor* Hostile;

	GameMode->GetObjectives(TeamManagerComponent->GetTeamInfo().TeamID, Friendly, Hostile);

	TeamManagerComponent->PushContextToFollower(Friendly, Hostile);
}
