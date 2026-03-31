// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"

#include "DynamicEQSContext.generated.h"

/**
 * UDynamicEQSContext
 * Provides the owning actor (querier) as the EQS query context location/actor.
 * Attach this as the context in your EQS query template to let the executor
 * use its owner as the reference point.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Dynamic EQS Querier Context"))
class DYNAMICEQS_API UDynamicEQSContext : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	UDynamicEQSContext();

	/** Provides the querier actor as the single context actor. */
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance,
	                            FEnvQueryContextData& ContextData) const override;
};
