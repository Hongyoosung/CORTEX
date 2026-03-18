// DEScholaGameInstance.h - Custom GameInstance for Schola training integration

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "DEScholaGameInstance.generated.h"

/**
 * Custom GameInstance that manages global Schola training infrastructure
 * Provides centralized access to CommunicationManager for gRPC server
 */
UCLASS()
class GAMEAI_PROJECT_API UDEScholaGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UDEScholaGameInstance();

	//~ Begin UGameInstance Interface
	virtual void Init() override;
	virtual void Shutdown() override;
	//~ End UGameInstance Interface


	/**
	 * Initialize the communication manager and start gRPC server
	 * @param Port - Server port (default: 50051)
	 * @return True if server started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Schola")
	bool StartCommunicationServer(int32 Port = 50051);

	/**
	 * Stop the gRPC server and cleanup
	 */
	UFUNCTION(BlueprintCallable, Category = "Schola")
	void StopCommunicationServer();

	/**
	 * Check if communication server is running
	 */
	UFUNCTION(BlueprintPure, Category = "Schola")
	bool IsCommunicationServerRunning() const { return bServerRunning; }

protected:

	/** Server running state */
	bool bServerRunning = false;

	/** Server port */
	int32 ServerPort = 50051;
};
