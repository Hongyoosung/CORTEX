// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actor/LeaderCharacter.h"
#include "Actor/FollowerCharacter.h"
#include "Core/SimulationManagerGameMode.h"
#include "Team/Components/TeamManagerComponent.h"
#include "Team/Components/StrategicPlannerComponent.h"
#include "Util/Components/VisualLoggerComponent.h"
#include "Team/TeamTypes.h"
#include "Observation/TeamObservation.h"


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
		TeamManagerComponent->DiscoverWorldObjectives(); // 이걸 게임모드나 다른 곳에서 호출하도록 변경해야함
		
		UE_LOG(LogTemp, Log, TEXT("[LeaderCharacter] '%s': Objectives discovered"), *GetName());
	}


	ASimulationManagerGameMode* GameMode = Cast<ASimulationManagerGameMode>(UGameplayStatics::GetGameMode(GetWorld()));
	GameMode->RegisterTeam(TeamManagerComponent->GetTeamInfo());


	// Strategic Planner: Initialize MCTS with configured simulation count
	if (StrategicPlannerComponent)
	{
		StrategicPlannerComponent->InitializeMCTS(StrategicPlannerComponent->MCTSSimulations);
		UE_LOG(LogTemp, Log, TEXT("[LeaderCharacter] '%s': MCTS initialized with %d simulations"),
			*GetName(), StrategicPlannerComponent->MCTSSimulations);
	}

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

TArray<AActor*> ALeaderCharacter::GetAliveFollowers() const
{
	TArray<AActor*> AliveFollowers;
	TArray<AActor*> RegisteredFollowers = TeamManagerComponent->GetFollowers();

	for (AActor* Follower : RegisteredFollowers)
	{
		if (!Follower)
		{
			continue;
		}

		AFollowerCharacter* FollowerChar = Cast<AFollowerCharacter>(Follower);

		if (FollowerChar->IsAlive_Implementation())
		{
			AliveFollowers.Add(Follower);
		}
	}

	return AliveFollowers;
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

FTeamObservation ALeaderCharacter::BuildTeamObservation(const TArray<AActor*>& Followers)
{
	return TeamManagerComponent ? TeamManagerComponent->BuildTeamObservation(Followers) : FTeamObservation();
}

bool ALeaderCharacter::AreObjectivesDiscovered() const
{
	return TeamManagerComponent ? TeamManagerComponent->AreObjectivesDiscovered() : false;
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

void ALeaderCharacter::ApplyStrategyAssignment(const TArray<FStrategyAssignment>& Assignments)
{

}
