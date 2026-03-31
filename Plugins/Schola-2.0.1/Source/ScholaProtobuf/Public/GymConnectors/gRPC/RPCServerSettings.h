// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "TrainingUtils/ArgBuilder.h"
#include "RPCServerSettings.generated.h"
/**
 * @brief A struct to hold settings relating to external communication (e.g. sockets)
 */
USTRUCT(BlueprintType)
struct SCHOLAPROTOBUF_API FRPCServerSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Communicator Settings")
	FString Address = FString("127.0.0.1");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Communicator Settings")
	int Port = 8000;

	void GetTrainingArgs(FScriptArgBuilder& ArgBuilder) const
	{
		ArgBuilder.AddIntArg("protocol.port",this->Port);
		ArgBuilder.AddStringArg("protocol.url", this->Address);
	}
	
};