// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "TrainingSettings/Ray/RLlibCheckpointSettings.h"

void FRLlibCheckpointSettings::GenerateTrainingArgs(FScriptArgBuilder& ArgBuilder) const
{

	if (this->bSaveFinalModel)
	{
		ArgBuilder.AddFlag(TEXT("checkpoint-settings.save-final-policy"));
		ArgBuilder.AddFlag(TEXT("checkpoint-settings.export-onnx"), this->bExportToONNX);
	}

	if (this->bEnableCheckpoints)
	{
		ArgBuilder.AddFlag(TEXT("checkpoint-settings.enable-checkpoints"));
		ArgBuilder.AddIntArg(TEXT("checkpoint-settings.save-freq"), this->SaveFreq);
	}

	ArgBuilder.AddConditionalStringArg(TEXT("checkpoint-settings.checkpoint-dir"), this->CheckpointDir.Path, !this->CheckpointDir.Path.IsEmpty());
}

FRLlibCheckpointSettings::~FRLlibCheckpointSettings()
{

}