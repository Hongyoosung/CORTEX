// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Networking.h"
#include "Types/MocTypes.h"
#include "Observation/ObservationTypes.h"
#include "MocScholaBridge.generated.h"

class AMocCharacter;
class AMocGameMode;
class UMocRewardCalculator;

/**
 * Command from Python client
 */
USTRUCT()
struct FScholaCommand
{
	GENERATED_BODY()

	FString Command;
	int32 TeamID = 0;
	int32 AgentID = 0;

	// Action parameters
	int32 OptionType = 0;
	FVector TargetPosition = FVector::ZeroVector;
	float OptionDuration = 10.0f;
	TArray<float> EQSWeights;
};

/**
 * Response to Python client
 */
USTRUCT()
struct FScholaResponse
{
	GENERATED_BODY()

	FString Status = TEXT("success");
	FString Error;

	// Observation data
	TArray<float> Observation;

	// Reward data
	float Reward = 0.0f;
	bool bDone = false;

	// Additional info
	TMap<FString, FString> Info;
};

/**
 * Schola Bridge Server - UE5 side of Python-UE5 communication.
 *
 * Responsibilities:
 * - Listen for Python client connections (TCP socket)
 * - Process commands (reset, step, get_state, etc.)
 * - Execute actions in UE5 environment
 * - Return observations and rewards
 * - Manage episode lifecycle
 *
 * Protocol: JSON over TCP with length-prefixed messages
 * Port: Configurable (default 8888)
 *
 * Commands:
 * - reset: Start new episode
 * - step: Execute action and return (obs, reward, done, info)
 * - get_team_state: Get team coordination data
 * - get_capture_points: Get objective status
 * - disconnect: Close connection
 */
UCLASS()
class GAMEAI_PROJECT_API AMocScholaBridge : public AActor
{
	GENERATED_BODY()

public:
	AMocScholaBridge();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	/**
	 * Start listening for Python connections
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Schola")
	bool StartServer(int32 Port);

	/**
	 * Stop server and close connections
	 */
	UFUNCTION(BlueprintCallable, Category = "MOC|Schola")
	void StopServer();

	/**
	 * Check if server is running
	 */
	UFUNCTION(BlueprintPure, Category = "MOC|Schola")
	bool IsServerRunning() const { return bServerRunning; }

	/**
	 * Get connected clients count
	 */
	UFUNCTION(BlueprintPure, Category = "MOC|Schola")
	int32 GetConnectedClients() const { return ConnectedSockets.Num(); }

protected:
	/** Server port */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola")
	int32 ServerPort = 8888;

	/** Server running */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola")
	bool bServerRunning = false;

	/** Auto-start server on begin play */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola")
	bool bAutoStartServer = true;

	/** Max connections */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola")
	int32 MaxConnections = 5;

private:
	/** Listening socket */
	FSocket* ListenSocket = nullptr;

	/** Connected client sockets */
	TArray<FSocket*> ConnectedSockets;

	/** Socket subsystem */
	ISocketSubsystem* SocketSubsystem = nullptr;

	/** Game mode reference */
	UPROPERTY()
	AMocGameMode* GameMode;

	/** Current controlled agent per connection */
	TMap<FSocket*, AMocCharacter*> ControlledAgents;

	/**
	 * Accept pending connections
	 */
	void AcceptConnections();

	/**
	 * Process incoming messages from clients
	 */
	void ProcessClientMessages();

	/**
	 * Handle single command
	 */
	FScholaResponse HandleCommand(FSocket* ClientSocket, const FScholaCommand& Command);

	/**
	 * Reset environment
	 */
	FScholaResponse HandleReset(const FScholaCommand& Command);

	/**
	 * Execute step
	 */
	FScholaResponse HandleStep(FSocket* ClientSocket, const FScholaCommand& Command);

	/**
	 * Get team state
	 */
	FScholaResponse HandleGetTeamState(FSocket* ClientSocket);

	/**
	 * Get capture points status
	 */
	FScholaResponse HandleGetCapturePoints();

	/**
	 * Send response to client
	 */
	bool SendResponse(FSocket* ClientSocket, const FScholaResponse& Response);

	/**
	 * Receive command from client
	 */
	bool ReceiveCommand(FSocket* ClientSocket, FScholaCommand& OutCommand);

	/**
	 * Parse JSON command
	 */
	bool ParseCommandJSON(const FString& JSON, FScholaCommand& OutCommand);

	/**
	 * Serialize response to JSON
	 */
	FString SerializeResponseJSON(const FScholaResponse& Response);

	/**
	 * Close client connection
	 */
	void CloseClientConnection(FSocket* ClientSocket);
};
