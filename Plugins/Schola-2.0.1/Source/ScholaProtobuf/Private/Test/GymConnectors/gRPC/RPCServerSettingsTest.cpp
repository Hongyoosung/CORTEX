// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "TrainingUtils/ArgBuilder.h"
#include "GymConnectors/gRPC/RPCServerSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPCServerSettingsTest, "Schola.Training.Settings.RPCSettings.Server Settings Test", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRPCServerSettingsTest::RunTest(const FString& Parameters)
{
	FScriptArgBuilder ArgBuilder;
	FRPCServerSettings ServerSettings;
	ServerSettings.Port = 8000;
	ServerSettings.GetTrainingArgs(ArgBuilder);
	FString GeneratedArgs = ArgBuilder.Build();

	TestTrue(TEXT("RPC Server args should contain --protocol.port \"8000\""),
		GeneratedArgs.Contains(TEXT("--protocol.port \"8000\"")));
	TestTrue(TEXT("RPC Server args should contain --protocol.url 127.0.0.1"),
		GeneratedArgs.Contains(TEXT("--protocol.url 127.0.0.1")));

	return true;
}