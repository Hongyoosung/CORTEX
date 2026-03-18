// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "TrainingSettings/Ray/RLlibResourceSettings.h"


void FRLlibResourceSettings::GenerateTrainingArgs(FScriptArgBuilder& ArgBuilder) const
{
	if(this->bUseCustomNumberOfCPUs)
	{
		ArgBuilder.AddIntArg(TEXT("resource-settings.num-cpus"),this->NumCPUs);
	}
	ArgBuilder.AddIntArg(TEXT("resource-settings.num-gpus"),this->NumGPUs);
	ArgBuilder.AddIntArg(TEXT("resource-settings.num-cpus-for-main-process"),this->NumCPUsForMainProcess);

	ArgBuilder.AddIntArg(TEXT("resource-settings.num-learners"),this->NumLearners);
	ArgBuilder.AddIntArg(TEXT("resource-settings.num-gpus-per-learner"),this->NumGPUsPerLearner);
	ArgBuilder.AddIntArg(TEXT("resource-settings.num-cpus-per-learner"),this->NumCPUsPerLearner);
}

FRLlibResourceSettings::~FRLlibResourceSettings()
{
}
