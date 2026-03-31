// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "TrainingSettings/Ray/RLlibTrainingSettings.h"

void FRLlibTrainingSettings::GenerateTrainingArgs(FScriptArgBuilder& ArgBuilder) const
{
	ArgBuilder.AddPositionalArgument(TEXT("rllib"));
	ArgBuilder.AddPositionalArgument(TEXT("train"));

	switch (this->Algorithm)
	{
		case (ERLlibTrainingAlgorithm::PPO):
			ArgBuilder.AddPositionalArgument(TEXT("PPO"));
			this->PPOSettings.GenerateTrainingArgs(ArgBuilder);
			break;
		case (ERLlibTrainingAlgorithm::IMPALA):
			ArgBuilder.AddPositionalArgument(TEXT("IMPALA"));
			this->IMPALASettings.GenerateTrainingArgs(ArgBuilder);
			break;
		default:
			ArgBuilder.AddPositionalArgument(TEXT("APPO"));
			this->APPOSettings.GenerateTrainingArgs(ArgBuilder);
			break;
	}

	ArgBuilder.AddIntArg(TEXT("training-settings.timesteps"), this->Timesteps);
	ArgBuilder.AddFloatArg(TEXT("training-settings.learning-rate"), this->LearningRate);
	ArgBuilder.AddIntArg(TEXT("training-settings.minibatch-size"), this->MinibatchSize);
	ArgBuilder.AddIntArg(TEXT("training-settings.train-batch-size-per-learner"), this->TrainBatchSizePerLearner);
	ArgBuilder.AddIntArg(TEXT("training-settings.num-sgd-iter"), this->NumSGDIter);
	ArgBuilder.AddFloatArg(TEXT("training-settings.gamma"), this->Gamma);
	
	this->CheckpointSettings.GenerateTrainingArgs(ArgBuilder);
	this->LoggingSettings.GenerateTrainingArgs(ArgBuilder);
	this->ResumeSettings.GenerateTrainingArgs(ArgBuilder);
	this->NetworkArchitectureSettings.GenerateTrainingArgs(ArgBuilder);
	this->ResourceSettings.GenerateTrainingArgs(ArgBuilder);
}

FRLlibTrainingSettings::~FRLlibTrainingSettings()
{

}