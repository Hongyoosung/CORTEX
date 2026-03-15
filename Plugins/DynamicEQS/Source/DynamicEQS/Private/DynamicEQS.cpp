// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicEQS.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogDynamicEQS);

#define LOCTEXT_NAMESPACE "FDynamicEQSModule"

void FDynamicEQSModule::StartupModule()
{
	UE_LOG(LogDynamicEQS, Log, TEXT("Plugin started — dynamic EQS weight system ready."));
}

void FDynamicEQSModule::ShutdownModule()
{
	UE_LOG(LogDynamicEQS, Log, TEXT("Plugin shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDynamicEQSModule, DynamicEQS)
