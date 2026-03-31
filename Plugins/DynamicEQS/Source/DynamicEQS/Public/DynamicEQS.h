// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

/** Dedicated log category for all DynamicEQS plugin code. */
DYNAMICEQS_API DECLARE_LOG_CATEGORY_EXTERN(LogDynamicEQS, Log, All);

// Main module header - forward-declare key plugin types for convenience
class UDynamicEQSExecutor;
class UDynamicEQSAgentComponent;
class ADynamicEQSEnvironmentActor;
struct FDynamicEQSWeightParameters;

class DYNAMICEQS_API FDynamicEQSModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Singleton accessor */
	static FDynamicEQSModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FDynamicEQSModule>("DynamicEQS");
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("DynamicEQS");
	}
};
