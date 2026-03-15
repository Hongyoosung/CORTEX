// Copyright Epic Games, Inc. All Rights Reserved.

#include "EQS/DynamicEQSContext.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"

UDynamicEQSContext::UDynamicEQSContext()
{
}

void UDynamicEQSContext::ProvideContext(FEnvQueryInstance& QueryInstance,
                                        FEnvQueryContextData& ContextData) const
{
	AActor* QueryOwner = Cast<AActor>(QueryInstance.Owner.Get());
	if (QueryOwner)
	{
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, QueryOwner);
	}
}
