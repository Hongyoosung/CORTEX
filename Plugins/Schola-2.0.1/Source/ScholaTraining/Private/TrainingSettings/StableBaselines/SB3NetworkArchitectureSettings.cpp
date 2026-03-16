// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "TrainingSettings/StableBaselines/SB3NetworkArchitectureSettings.h"

void FSB3NetworkArchSettings::GenerateTrainingArgs( FScriptArgBuilder& ArgBuilder) const
{

	FString ActivationString;
	switch (ActivationFunction)
	{
		case (ESB3ActivationFunctionEnum::TanH):
			ActivationString = TEXT("tanh");
			break;

		case (ESB3ActivationFunctionEnum::ReLU):
			ActivationString = TEXT("relu");
			break;

		default:
			ActivationString = TEXT("sigmoid");
			break;
	}
	ArgBuilder.AddStringArg(TEXT("activation"), ActivationString);
	ArgBuilder.AddIntArrayArg(TEXT("critic-parameters"), this->CriticParameters);
	ArgBuilder.AddIntArrayArg(TEXT("policy-parameters"), this->PolicyParameters);
}


FSB3NetworkArchSettings::~FSB3NetworkArchSettings()
{

}