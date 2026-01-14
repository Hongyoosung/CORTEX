#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvQueryContext_Teammates.generated.h"

/**
 * EQS Context that provides teammate (friendly agent) locations for formation maintenance
 * Used by v8.0 TacticalPositionQuery to score positions based on team cohesion
 *
 * Production Mode: Reads teammates from FollowerAgentComponent's team reference
 * Testing Mode: Fallback to searching actors with same TeamID (for EQS Testing Pawn)
 *
 * v8.0: Used for FormationSpread parameter in tactical movement
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_Teammates : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
