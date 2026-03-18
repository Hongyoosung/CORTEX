// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "TrainingSettings/Ray/RLlibTrainingSettings.h"
#include "TrainingUtils/ArgBuilder.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLlibTrainingSettingsTest, "Schola.GymConnectors.Settings.Ray.RLlibTrainingSettings Test", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRLlibTrainingSettingsTest::RunTest(const FString& Parameters)
{
	FRLlibTrainingSettings RLlibSettings;

	RLlibSettings.Timesteps = 5000;
	RLlibSettings.LearningRate = 0.001f;
	RLlibSettings.MinibatchSize = 256;
	RLlibSettings.Gamma = 0.95f;
	RLlibSettings.Algorithm = ERLlibTrainingAlgorithm::PPO;

	FScriptArgBuilder ArgBuilder;
	RLlibSettings.GenerateTrainingArgs(ArgBuilder);
	FString GeneratedArgs = ArgBuilder.Build();

	TestTrue(TEXT("RLlib args should contain --training-settings.timesteps \"5000\""),
		GeneratedArgs.Contains(TEXT("--training-settings.timesteps \"5000\"")));
	TestTrue(TEXT("RLlib args should contain --training-settings.learning-rate \"0.001\""),
		GeneratedArgs.Contains(TEXT("--training-settings.learning-rate \"0.001\"")));
	TestTrue(TEXT("RLlib args should contain --training-settings.minibatch-size \"256\""),
		GeneratedArgs.Contains(TEXT("--training-settings.minibatch-size \"256\"")));
	TestTrue(TEXT("RLlib args should contain --training-settings.gamma \"0.95\""),
		GeneratedArgs.Contains(TEXT("--training-settings.gamma \"0.95\"")));
	TestTrue(TEXT("RLlib args should contain PPO algorithm"),
		GeneratedArgs.Contains(TEXT("PPO")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPythonGroundTruthRLlibTBTest, "Schola.GymConnectors.PythonGroundTruth RLlib with Tensorboard Test", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPythonGroundTruthRLlibTBTest::RunTest(const FString& Parameters)
{
	FRLlibTrainingSettings RLlibSettings;
	RLlibSettings.Timesteps = 3000;
	RLlibSettings.LearningRate = 0.0003f;
	RLlibSettings.LoggingSettings.EnvLoggingVerbosity = 0;
	RLlibSettings.LoggingSettings.TrainerLoggingVerbosity = 1;

	FScriptArgBuilder ArgBuilder;
	RLlibSettings.GenerateTrainingArgs(ArgBuilder);
	FString GeneratedArgs = ArgBuilder.Build();

	// Verify key arguments that Python expects
	TestTrue(TEXT("Should contain logging-settings.schola-verbosity for Python compatibility"),
		GeneratedArgs.Contains(TEXT("--logging-settings.schola-verbosity \"0\"")));
	TestTrue(TEXT("Should contain logging-settings.rllib-verbosity for Python compatibility"),
		GeneratedArgs.Contains(TEXT("--logging-settings.rllib-verbosity \"1\"")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPythonGroundTruthNetworkArchTest, "Schola.GymConnectors.PythonGroundTruth Network Architecture Test", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPythonGroundTruthNetworkArchTest::RunTest(const FString& Parameters)
{
	FRLlibTrainingSettings RLlibSettings;
	RLlibSettings.NetworkArchitectureSettings.FCNetHiddens = { 256, 256 };
	RLlibSettings.NetworkArchitectureSettings.bUseAttention = true;
	RLlibSettings.NetworkArchitectureSettings.AttentionDims = 64;

	FScriptArgBuilder ArgBuilder;
	RLlibSettings.GenerateTrainingArgs(ArgBuilder);
	FString GeneratedArgs = ArgBuilder.Build();

	// Verify network architecture arguments
	TestTrue(TEXT("Should contain --network-architecture-settings.fcnet-hiddens for network architecture"),
		GeneratedArgs.Contains(TEXT("--network-architecture-settings.fcnet-hiddens")));
	TestTrue(TEXT("Should contain --network-architecture-settings.use-attention when enabled"),
		GeneratedArgs.Contains(TEXT("--network-architecture-settings.use-attention")));
	TestTrue(TEXT("Should contain --network-architecture-settings.attention-dim \"64\" when attention is used"),
		GeneratedArgs.Contains(TEXT("--network-architecture-settings.attention-dim \"64\"")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConditionalFlagsTest, "Schola.GymConnectors.ConditionalFlags Test", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConditionalFlagsTest::RunTest(const FString& Parameters)
{
	FRLlibTrainingSettings RLlibSettings;

	RLlibSettings.CheckpointSettings.bEnableCheckpoints = true;
	RLlibSettings.CheckpointSettings.SaveFreq = 2000;
	RLlibSettings.CheckpointSettings.bSaveFinalModel = true;
	RLlibSettings.CheckpointSettings.bExportToONNX = true;

	FScriptArgBuilder ArgBuilder;
	RLlibSettings.GenerateTrainingArgs(ArgBuilder);
	FString GeneratedArgs = ArgBuilder.Build();

	TestTrue(TEXT("Should contain --checkpoint-settings.enable-checkpoints when bEnableCheckpoints is true"),
		GeneratedArgs.Contains(TEXT("--checkpoint-settings.enable-checkpoints")));
	TestTrue(TEXT("Should contain --checkpoint-settings.save-freq \"2000\""),
		GeneratedArgs.Contains(TEXT("--checkpoint-settings.save-freq \"2000\"")));
	TestTrue(TEXT("Should contain --checkpoint-settings.save-final-policy when bSaveFinalModel is true"),
		GeneratedArgs.Contains(TEXT("--checkpoint-settings.save-final-policy")));
	TestTrue(TEXT("Should contain --checkpoint-settings.export-onnx when bExportToONNX is true"),
		GeneratedArgs.Contains(TEXT("--checkpoint-settings.export-onnx")));

	UE_LOG(LogTemp, Display, TEXT("Conditional Flags Test - Generated Args: %s"), *GeneratedArgs);

	return true;
}