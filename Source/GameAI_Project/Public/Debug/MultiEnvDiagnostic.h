// MultiEnvDiagnostic.h - Console command to diagnose multi-environment enemy relationships

#pragma once

#include "CoreMinimal.h"
#include "MultiEnvDiagnostic.generated.h"

/**
 * Diagnostic utility for multi-environment training setups
 *
 * Usage:
 * 1. Run the game/simulation
 * 2. Open console (~ key)
 * 3. Type: DiagnoseMultiEnv
 * 4. Check output log for detailed analysis
 *
 * This will identify:
 * - Missing or incorrect enemy relationships
 * - Cross-environment contamination
 * - ActorToTeamMap registration issues
 * - ObjectiveActor hostility check errors
 */
UCLASS()
class GAMEAI_PROJECT_API UMultiEnvDiagnostic : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Run comprehensive diagnostic on multi-environment setup
	 * Checks all team registrations, enemy relationships, and cross-environment isolation
	 */
	static void DiagnoseMultiEnvironmentSetup(UWorld* World);
};
