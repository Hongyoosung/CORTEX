// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "TrainingSettings/StableBaselines/SB3TrainingSettings.h"
#include "TrainingUtils/ArgBuilder.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSB3TrainingSettingsTest, "Schola.GymConnectors.Settings.StableBaselines.SB3TrainingSettings Test", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSB3TrainingSettingsTest::RunTest(const FString& Parameters)
{
	FSB3TrainingSettings SB3Settings;

	SB3Settings.Timesteps = 3000;
	SB3Settings.bDisplayProgressBar = true;
	SB3Settings.Algorithm = ESB3TrainingAlgorithm::PPO;

	FScriptArgBuilder ArgBuilder;
	SB3Settings.GenerateTrainingArgs(ArgBuilder);
	FString GeneratedArgs = ArgBuilder.Build();

	TestTrue(TEXT("SB3 args should contain --timesteps \"3000\""),
		GeneratedArgs.Contains(TEXT("--timesteps \"3000\"")));
	TestTrue(TEXT("SB3 args should contain --pbar flag"),
		GeneratedArgs.Contains(TEXT("--pbar")));
	TestTrue(TEXT("SB3 args should contain PPO algorithm"),
		GeneratedArgs.Contains(TEXT("PPO")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPythonGroundTruthSB3TBTest, "Schola.GymConnectors.PythonGroundTruth SB3 with Tensorboard Test", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPythonGroundTruthSB3TBTest::RunTest(const FString& Parameters)
{
	FSB3TrainingSettings SB3Settings;
	SB3Settings.Timesteps = 3000;
	SB3Settings.LoggingSettings.bSaveTBLogs = true;
	SB3Settings.LoggingSettings.LogDir.Path = FString("./logs");
	SB3Settings.LoggingSettings.LogFreq = 10;

	FScriptArgBuilder ArgBuilder;
	SB3Settings.GenerateTrainingArgs(ArgBuilder);
	FString GeneratedArgs = ArgBuilder.Build();

	// Verify tensorboard arguments that Python expects
	TestTrue(TEXT("Should contain --enable-tensorboard when bSaveTBLogs is true"),
		GeneratedArgs.Contains(TEXT("--enable-tensorboard")));
	TestTrue(TEXT("Should contain --log-dir for tensorboard logs"),
		GeneratedArgs.Contains(TEXT("--log-dir")));
	TestTrue(TEXT("Should contain --log-freq for logging frequency"),
		GeneratedArgs.Contains(TEXT("--log-freq \"10\"")));

	return true;
}

