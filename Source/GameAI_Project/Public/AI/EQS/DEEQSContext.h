// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "DEEQSContext.generated.h"

/**
 * EQS Context: Provides querier (agent) information
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_DEQuerier : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

/**
 * EQS Context: Provides enemy positions visible to team
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_DEEnemies : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

/**
 * EQS Context: Provides ally positions
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_DEAllies : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

/**
 * EQS Context: Provides capture point positions
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_DECapturePoints : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

/**
 * EQS Context: Provides enemy team's objective/base location
 * Used for Weight [0] - EnemyObjectiveProximity
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_DEEnemyObjective : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

/**
 * EQS Context: Provides friendly team's objective/base location
 * Used for Weight [1] - AllyObjectiveProximity
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_DEAllyObjective : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

/**
 * EQS Context: Provides cover point locations
 * Used for Weight [2] - CoverDensity
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_DECoverPoints : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

/**
 * EQS Context: Provides the agent's squad-assigned base location.
 * Reads AssignedBaseIndex from the agent's Blackboard and returns the
 * world position of the corresponding capture point.
 * Used for Weight [6] - AssignedBaseProximity (Phase 3 — entity-centric refactor).
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_DEAssignedBase : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
