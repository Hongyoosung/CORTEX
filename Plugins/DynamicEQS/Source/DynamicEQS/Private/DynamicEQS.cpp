// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicEQS.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FDynamicEQSModule"

void FDynamicEQSModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("DynamicEQS: Plugin started — dynamic EQS weight system ready."));
}

void FDynamicEQSModule::ShutdownModule()
{
	UE_LOG(LogTemp, Log, TEXT("DynamicEQS: Plugin shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDynamicEQSModule, DynamicEQS)
