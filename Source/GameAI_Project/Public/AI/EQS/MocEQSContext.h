// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "MocEQSContext.generated.h"

/**
 * EQS Context: Provides querier (agent) information
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_MocQuerier : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

/**
 * EQS Context: Provides enemy positions visible to team
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_MocEnemies : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

/**
 * EQS Context: Provides ally positions
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_MocAllies : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

/**
 * EQS Context: Provides capture point positions
 */
UCLASS()
class GAMEAI_PROJECT_API UEnvQueryContext_MocCapturePoints : public UEnvQueryContext
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
class GAMEAI_PROJECT_API UEnvQueryContext_MocEnemyObjective : public UEnvQueryContext
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
class GAMEAI_PROJECT_API UEnvQueryContext_MocAllyObjective : public UEnvQueryContext
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
class GAMEAI_PROJECT_API UEnvQueryContext_MocCoverPoints : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
