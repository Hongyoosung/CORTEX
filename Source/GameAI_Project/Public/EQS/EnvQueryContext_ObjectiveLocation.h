#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvQueryContext_ObjectiveLocation.generated.h"

/**
 * EQS Context that provides the current objective's target location
 * Used by tactical queries (ForwardCover, Advance, etc.) to orient toward strategic goals
 *
 * Production Mode: Reads CurrentMission.TargetLocation from FollowerStateTreeContext
 * Testing Mode: Fallback to searching actors with tag "Objective" (for EQS Testing Pawn)
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_ObjectiveLocation : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
