// MultiEnvDiagnostic.cpp - Console command to diagnose multi-environment enemy relationships
// Usage: Type "DiagnoseMultiEnv" in console

#include "Debug/MultiEnvDiagnostic.h"
#include "Core/SimulationManagerGameMode.h"
#include "Team/ObjectiveActor.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

void UMultiEnvDiagnostic::DiagnoseMultiEnvironmentSetup(UWorld* World)
{
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("===== MULTI-ENV DIAGNOSTIC FAILED: No World ====="));
		return;
	}

	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(
		UGameplayStatics::GetGameMode(World)
	);

	if (!SimManager)
	{
		UE_LOG(LogTemp, Error, TEXT("===== MULTI-ENV DIAGNOSTIC FAILED: No SimulationManager ====="));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("  MULTI-ENVIRONMENT DIAGNOSTIC REPORT"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	// Get all registered teams
	TArray<int32> AllTeamIDs = SimManager->GetAllTeamIDs();
	AllTeamIDs.Sort();

	UE_LOG(LogTemp, Warning, TEXT("Total Teams Registered: %d"), AllTeamIDs.Num());
	UE_LOG(LogTemp, Warning, TEXT("Expected: 8 teams (Team 0-7 for 4 environments)"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	// Check each team's enemy relationships
	for (int32 TeamID : AllTeamIDs)
	{
		TArray<int32> EnemyTeamIDs = SimManager->GetEnemyTeamIDs(TeamID);
		int32 ExpectedEnemy = TeamID ^ 1; // XOR with 1 (0↔1, 2↔3, 4↔5, 6↔7)
		int32 EnvironmentID = TeamID / 2;

		UE_LOG(LogTemp, Warning, TEXT("Team %d (Env %d):"), TeamID, EnvironmentID);
		UE_LOG(LogTemp, Warning, TEXT("  Expected Enemy: Team %d"), ExpectedEnemy);
		UE_LOG(LogTemp, Warning, TEXT("  Actual Enemies: %s"),
			EnemyTeamIDs.Num() > 0 ? *FString::FromInt(EnemyTeamIDs[0]) : TEXT("NONE ❌"));

		// Check if enemy relationship is correct
		bool bHasCorrectEnemy = EnemyTeamIDs.Contains(ExpectedEnemy);
		bool bHasIncorrectEnemies = false;
		for (int32 EnemyID : EnemyTeamIDs)
		{
			if (EnemyID != ExpectedEnemy)
			{
				UE_LOG(LogTemp, Error, TEXT("  ❌ INCORRECT ENEMY: Team %d (should only be Team %d)"),
					EnemyID, ExpectedEnemy);
				bHasIncorrectEnemies = true;
			}
		}

		if (bHasCorrectEnemy && !bHasIncorrectEnemies)
		{
			UE_LOG(LogTemp, Log, TEXT("  ✅ Enemy relationship CORRECT"));
		}
		else if (!bHasCorrectEnemy)
		{
			UE_LOG(LogTemp, Error, TEXT("  ❌ MISSING expected enemy Team %d"), ExpectedEnemy);
		}

		// Check team members
		TArray<AActor*> TeamMembers = SimManager->GetTeamMembers(TeamID);
		UE_LOG(LogTemp, Warning, TEXT("  Team Members: %d agents"), TeamMembers.Num());

		// Verify ActorToTeamMap entries
		for (AActor* Member : TeamMembers)
		{
			int32 MappedTeamID = SimManager->GetTeamIDForActor(Member);
			if (MappedTeamID != TeamID)
			{
				UE_LOG(LogTemp, Error, TEXT("  ❌ Actor '%s' mapped to Team %d (should be %d)"),
					*Member->GetName(), MappedTeamID, TeamID);
			}
		}

		UE_LOG(LogTemp, Warning, TEXT(""));
	}

	// Check ObjectiveActors
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("  OBJECTIVE ACTOR CHECKS"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	TArray<AActor*> AllObjectives;
	UGameplayStatics::GetAllActorsOfClass(World, AObjectiveActor::StaticClass(), AllObjectives);

	UE_LOG(LogTemp, Warning, TEXT("Total ObjectiveActors: %d"), AllObjectives.Num());
	UE_LOG(LogTemp, Warning, TEXT("Expected: 8 objectives (2 per environment)"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	for (AActor* ObjActor : AllObjectives)
	{
		AObjectiveActor* Objective = Cast<AObjectiveActor>(ObjActor);
		if (!Objective) continue;

		int32 OwnerTeam = Objective->OwnerTeamID;
		int32 ExpectedEnemy = OwnerTeam ^ 1;
		int32 EnvironmentID = OwnerTeam / 2;

		UE_LOG(LogTemp, Warning, TEXT("Objective '%s':"), *Objective->GetName());
		UE_LOG(LogTemp, Warning, TEXT("  Owner Team: %d (Env %d)"), OwnerTeam, EnvironmentID);

		// Test hostility with all teams
		for (int32 TestTeamID : AllTeamIDs)
		{
			bool bIsHostile = Objective->IsHostileTo(TestTeamID);
			bool bShouldBeHostile = (TestTeamID == ExpectedEnemy);

			if (bIsHostile != bShouldBeHostile)
			{
				UE_LOG(LogTemp, Error, TEXT("  ❌ IsHostileTo(Team %d) = %s (SHOULD BE %s)"),
					TestTeamID,
					bIsHostile ? TEXT("TRUE") : TEXT("FALSE"),
					bShouldBeHostile ? TEXT("TRUE") : TEXT("FALSE"));
			}
			else if (bIsHostile)
			{
				UE_LOG(LogTemp, Log, TEXT("  ✅ IsHostileTo(Team %d) = TRUE (correct)"), TestTeamID);
			}
		}

		UE_LOG(LogTemp, Warning, TEXT(""));
	}

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("  CROSS-ENVIRONMENT CONTAMINATION TEST"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	// Test specific contamination scenarios
	struct FTestCase
	{
		int32 Team1;
		int32 Team2;
		bool bShouldBeEnemies;
		FString Description;
	};

	TArray<FTestCase> TestCases = {
		{0, 1, true, "Env 0: Team 0 vs Team 1 (SHOULD BE ENEMIES)"},
		{0, 2, false, "Cross-env: Team 0 vs Team 2 (SHOULD NOT BE ENEMIES)"},
		{0, 3, false, "Cross-env: Team 0 vs Team 3 (SHOULD NOT BE ENEMIES)"},
		{2, 3, true, "Env 1: Team 2 vs Team 3 (SHOULD BE ENEMIES)"},
		{2, 4, false, "Cross-env: Team 2 vs Team 4 (SHOULD NOT BE ENEMIES)"},
		{4, 5, true, "Env 2: Team 4 vs Team 5 (SHOULD BE ENEMIES)"},
		{6, 7, true, "Env 3: Team 6 vs Team 7 (SHOULD BE ENEMIES)"},
		{1, 7, false, "Cross-env: Team 1 vs Team 7 (SHOULD NOT BE ENEMIES)"},
	};

	for (const FTestCase& Test : TestCases)
	{
		bool bActualResult = SimManager->AreTeamsEnemies(Test.Team1, Test.Team2);

		if (bActualResult == Test.bShouldBeEnemies)
		{
			UE_LOG(LogTemp, Log, TEXT("✅ %s - PASS"), *Test.Description);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("❌ %s - FAIL (Got %s)"),
				*Test.Description,
				bActualResult ? TEXT("ENEMIES") : TEXT("NOT ENEMIES"));
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("  DIAGNOSTIC COMPLETE"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

// Console command registration
static FAutoConsoleCommand DiagnoseMultiEnvCommand(
	TEXT("DiagnoseMultiEnv"),
	TEXT("Diagnose multi-environment enemy relationship setup"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (GEngine && GEngine->GetWorld())
		{
			UMultiEnvDiagnostic::DiagnoseMultiEnvironmentSetup(GEngine->GetWorld());
		}
	})
);
