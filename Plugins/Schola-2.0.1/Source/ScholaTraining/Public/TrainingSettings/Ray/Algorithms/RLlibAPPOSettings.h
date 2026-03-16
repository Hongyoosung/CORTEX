// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "TrainingUtils/ArgBuilder.h"
#include "TrainingSettings/TrainingSettings.h"
#include "RLlibAPPOSettings.generated.h"

/**
 * @brief A struct to hold Asynchronous Proximal Policy Optimization(APPO) settings for an RLLib training script
 * @note This is a partial implementation of the APPO settings, and is not exhaustive
 */
USTRUCT(BlueprintType)
struct SCHOLATRAINING_API FRLlibAPPOSettings : public FTrainingSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "APPO Settings")
    bool bVTrace = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "APPO Settings")
    float VTraceClipRhoThreshold = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "APPO Settings")
    float VTraceClipPGRhoThreshold = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "APPO Settings")
    float GAELambda = 0.95;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "APPO Settings")
    float ClipParam = 0.2;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "APPO Settings")
    bool bUseGAE = true;

	void GenerateTrainingArgs( FScriptArgBuilder& ArgBuilder) const;

	virtual ~FRLlibAPPOSettings();
};
