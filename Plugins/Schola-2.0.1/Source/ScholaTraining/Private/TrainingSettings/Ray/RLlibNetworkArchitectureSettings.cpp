// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "TrainingSettings/Ray/RLlibNetworkArchitectureSettings.h"

void FRLlibNetworkArchSettings::GenerateTrainingArgs(FScriptArgBuilder& ArgBuilder) const
{
	FString ActivationString;
	switch (ActivationFunction)
	{
		case (ERLlibActivationFunctionEnum::TanH):
			ActivationString = TEXT("TanH");
			break;

		case (ERLlibActivationFunctionEnum::ReLU):
			ActivationString = TEXT("ReLU");
			break;

		default:
			ActivationString = TEXT("Sigmoid");
			break;
	}
	ArgBuilder.AddStringArg(TEXT("network-architecture-settings.activation"),ActivationString);
	ArgBuilder.AddIntArrayArg(TEXT("network-architecture-settings.fcnet-hiddens"), this->FCNetHiddens);
	
	if(this->bUseAttention)
	{
		ArgBuilder.AddFlag(TEXT("network-architecture-settings.use-attention"));
		ArgBuilder.AddIntArg(TEXT("network-architecture-settings.attention-dim"),this->AttentionDims);
	}
}

FRLlibNetworkArchSettings::~FRLlibNetworkArchSettings()
{
}