// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "TrainingSettings/TrainingSettings.h"
#include "TrainingUtils/ArgBuilder.h"
#include "RLlibNetworkArchitectureSettings.generated.h"

/**
 * @brief An enumeration of activation functions neural networks
 */
UENUM(BlueprintType)
enum class ERLlibActivationFunctionEnum : uint8
{
	ReLU	UMETA(DisplayName = "ReLU"),
	Sigmoid UMETA(DisplayName = "Sigmoid"),
	TanH	UMETA(DisplayName = "TanH")
};

/**
 * @brief A struct to hold network architecture settings for an RLlib training script
 */
USTRUCT(BlueprintType)
struct SCHOLATRAINING_API FRLlibNetworkArchSettings : public FTrainingSettings
{
	GENERATED_BODY()

public:
	/** The activation function to use in the neural network */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Architecture Settings")
	ERLlibActivationFunctionEnum ActivationFunction = ERLlibActivationFunctionEnum::ReLU;

	/** The number and width of hidden layers in the neural network */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 1), Category = "Architecture Settings")
	TArray<int> FCNetHiddens = { 512, 512 };

	/** The size of each minibatch */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 1), Category = "Architecture Settings")
	int MinibatchSize = 256;

	/** Whether to use attention in the model */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Architecture Settings")
	bool bUseAttention = false;

	/** The dimension of the attention layer */
	UPROPERTY(EditAnywhere, meta = (EditCondition = "bUseAttention"), Category = "Architecture Settings")
	int AttentionDims = 64;

	void GenerateTrainingArgs( FScriptArgBuilder& ArgBuilder) const;

	virtual ~FRLlibNetworkArchSettings();
};
