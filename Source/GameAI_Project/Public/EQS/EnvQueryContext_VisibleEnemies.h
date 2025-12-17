#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvQueryContext_VisibleEnemies.generated.h"

/**
 * EQS Context that provides visible enemy locations for tactical positioning
 * Used by tactical queries (RetreatCover, FlankLeft/Right, etc.) to avoid/flank enemies
 *
 * Integration: Reads VisibleEnemies array from FollowerStateTreeContext
 *
 * Note: This differs from EnvQueryContext_CoverEnemies which uses TeamLeader's known enemies.
 * VisibleEnemies is the individual agent's current perception state, updated each tick.
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_VisibleEnemies : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
