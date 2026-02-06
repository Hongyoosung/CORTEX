// Copyright Epic Games, Inc. All Rights Reserved.

#include "Schola/Trainers/MocScholaBridge.h"
#include "Core/MocGameMode.h"
#include "Characters/MocCharacter.h"
#include "Rewards/MocRewardCalculator.h"
#include "Kismet/GameplayStatics.h"
#include "JsonObjectConverter.h"

AMocScholaBridge::AMocScholaBridge()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AMocScholaBridge::BeginPlay()
{
	Super::BeginPlay();

	GameMode = Cast<AMocGameMode>(UGameplayStatics::GetGameMode(GetWorld()));

	if (bAutoStartServer)
	{
		StartServer(ServerPort);
	}
}

void AMocScholaBridge::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopServer();
	Super::EndPlay(EndPlayReason);
}

void AMocScholaBridge::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bServerRunning)
	{
		AcceptConnections();
		ProcessClientMessages();
	}
}

bool AMocScholaBridge::StartServer(int32 Port)
{
	if (bServerRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("MocScholaBridge: Server already running"));
		return false;
	}

	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("MocScholaBridge: Failed to get socket subsystem"));
		return false;
	}

	// Create listening socket
	ListenSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MocScholaServer"), false);
	if (!ListenSocket)
	{
		UE_LOG(LogTemp, Error, TEXT("MocScholaBridge: Failed to create listen socket"));
		return false;
	}

	// Bind to port
	TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	Addr->SetAnyAddress();
	Addr->SetPort(Port);

	if (!ListenSocket->Bind(*Addr))
	{
		UE_LOG(LogTemp, Error, TEXT("MocScholaBridge: Failed to bind to port %d"), Port);
		return false;
	}

	// Listen
	if (!ListenSocket->Listen(MaxConnections))
	{
		UE_LOG(LogTemp, Error, TEXT("MocScholaBridge: Failed to listen"));
		return false;
	}

	bServerRunning = true;
	ServerPort = Port;

	UE_LOG(LogTemp, Log, TEXT("MocScholaBridge: Server started on port %d"), Port);
	return true;
}

void AMocScholaBridge::StopServer()
{
	if (!bServerRunning)
	{
		return;
	}

	// Close all client connections
	for (FSocket* ClientSocket : ConnectedSockets)
	{
		CloseClientConnection(ClientSocket);
	}
	ConnectedSockets.Empty();
	ControlledAgents.Empty();

	// Close listen socket
	if (ListenSocket)
	{
		ListenSocket->Close();
		SocketSubsystem->DestroySocket(ListenSocket);
		ListenSocket = nullptr;
	}

	bServerRunning = false;
	UE_LOG(LogTemp, Log, TEXT("MocScholaBridge: Server stopped"));
}

void AMocScholaBridge::AcceptConnections()
{
	if (!ListenSocket)
	{
		return;
	}

	bool bHasPendingConnection = false;
	if (ListenSocket->HasPendingConnection(bHasPendingConnection) && bHasPendingConnection)
	{
		TSharedRef<FInternetAddr> RemoteAddr = SocketSubsystem->CreateInternetAddr();
		FSocket* ClientSocket = ListenSocket->Accept(*RemoteAddr, TEXT("MocScholaClient"));

		if (ClientSocket)
		{
			ConnectedSockets.Add(ClientSocket);
			UE_LOG(LogTemp, Log, TEXT("MocScholaBridge: Client connected from %s"), *RemoteAddr->ToString(true));
		}
	}
}

void AMocScholaBridge::ProcessClientMessages()
{
	for (int32 i = ConnectedSockets.Num() - 1; i >= 0; --i)
	{
		FSocket* ClientSocket = ConnectedSockets[i];

		uint32 PendingDataSize = 0;
		if (!ClientSocket->HasPendingData(PendingDataSize))
		{
			continue;
		}

		FScholaCommand Command;
		if (ReceiveCommand(ClientSocket, Command))
		{
			FScholaResponse Response = HandleCommand(ClientSocket, Command);
			SendResponse(ClientSocket, Response);
		}
		else
		{
			// Connection error - close
			CloseClientConnection(ClientSocket);
			ConnectedSockets.RemoveAt(i);
		}
	}
}

FScholaResponse AMocScholaBridge::HandleCommand(FSocket* ClientSocket, const FScholaCommand& Command)
{
	if (Command.Command == TEXT("reset"))
	{
		return HandleReset(Command);
	}
	else if (Command.Command == TEXT("step"))
	{
		return HandleStep(ClientSocket, Command);
	}
	else if (Command.Command == TEXT("get_team_state"))
	{
		return HandleGetTeamState(ClientSocket);
	}
	else if (Command.Command == TEXT("get_capture_points"))
	{
		return HandleGetCapturePoints();
	}
	else if (Command.Command == TEXT("disconnect"))
	{
		CloseClientConnection(ClientSocket);
		FScholaResponse Response;
		Response.Status = TEXT("success");
		return Response;
	}

	FScholaResponse ErrorResponse;
	ErrorResponse.Status = TEXT("error");
	ErrorResponse.Error = FString::Printf(TEXT("Unknown command: %s"), *Command.Command);
	return ErrorResponse;
}

FScholaResponse AMocScholaBridge::HandleReset(const FScholaCommand& Command)
{
	FScholaResponse Response;

	if (!GameMode)
	{
		Response.Status = TEXT("error");
		Response.Error = TEXT("Game mode not found");
		return Response;
	}

	// Reset game
	//GameMode->ResetGame();

	// Get initial observation
	// TODO: Implement proper observation collection
	Response.Observation.Init(0.0f, 52); // Placeholder

	Response.Status = TEXT("success");
	UE_LOG(LogTemp, Log, TEXT("MocScholaBridge: Environment reset"));

	return Response;
}

FScholaResponse AMocScholaBridge::HandleStep(FSocket* ClientSocket, const FScholaCommand& Command)
{
	FScholaResponse Response;

	// Get controlled agent
	AMocCharacter** AgentPtr = ControlledAgents.Find(ClientSocket);
	if (!AgentPtr || !*AgentPtr)
	{
		Response.Status = TEXT("error");
		Response.Error = TEXT("No agent assigned to this connection");
		return Response;
	}

	AMocCharacter* Agent = *AgentPtr;

	// Execute action
	// TODO: Implement action execution via EQS system

	// Get reward
	UMocRewardCalculator* RewardCalc = Agent->FindComponentByClass<UMocRewardCalculator>();
	if (RewardCalc)
	{
		Response.Reward = RewardCalc->GetCumulativeReward();
	}

	// Get next observation
	// TODO: Implement observation collection
	Response.Observation.Init(0.0f, 52); // Placeholder

	// Check if done
	Response.bDone = false; // TODO: Check episode termination

	Response.Status = TEXT("success");

	return Response;
}

FScholaResponse AMocScholaBridge::HandleGetTeamState(FSocket* ClientSocket)
{
	FScholaResponse Response;

	// TODO: Implement team state collection

	Response.Status = TEXT("success");
	return Response;
}

FScholaResponse AMocScholaBridge::HandleGetCapturePoints()
{
	FScholaResponse Response;

	// TODO: Implement capture point status collection

	Response.Status = TEXT("success");
	return Response;
}

bool AMocScholaBridge::SendResponse(FSocket* ClientSocket, const FScholaResponse& Response)
{
	FString JSON = SerializeResponseJSON(Response);
	TArray<uint8> Data;
	FTCHARToUTF8 Converter(*JSON);
	Data.Append((uint8*)Converter.Get(), Converter.Length());

	// Send length prefix (4 bytes)
	int32 Length = Data.Num();
	TArray<uint8> LengthBytes;
	LengthBytes.Add((Length >> 0) & 0xFF);
	LengthBytes.Add((Length >> 8) & 0xFF);
	LengthBytes.Add((Length >> 16) & 0xFF);
	LengthBytes.Add((Length >> 24) & 0xFF);

	int32 BytesSent = 0;
	if (!ClientSocket->Send(LengthBytes.GetData(), 4, BytesSent) || BytesSent != 4)
	{
		return false;
	}

	// Send data
	BytesSent = 0;
	return ClientSocket->Send(Data.GetData(), Data.Num(), BytesSent) && BytesSent == Data.Num();
}

bool AMocScholaBridge::ReceiveCommand(FSocket* ClientSocket, FScholaCommand& OutCommand)
{
	// Receive length (4 bytes)
	TArray<uint8> LengthBytes;
	LengthBytes.SetNumUninitialized(4);

	int32 BytesRead = 0;
	if (!ClientSocket->Recv(LengthBytes.GetData(), 4, BytesRead) || BytesRead != 4)
	{
		return false;
	}

	int32 Length = LengthBytes[0] | (LengthBytes[1] << 8) | (LengthBytes[2] << 16) | (LengthBytes[3] << 24);

	// Receive data
	TArray<uint8> Data;
	Data.SetNumUninitialized(Length);

	BytesRead = 0;
	if (!ClientSocket->Recv(Data.GetData(), Length, BytesRead) || BytesRead != Length)
	{
		return false;
	}

	// Parse JSON
	FUTF8ToTCHAR Converter((ANSICHAR*)Data.GetData(), Data.Num());
	FString JSON(Converter.Length(), Converter.Get());

	return ParseCommandJSON(JSON, OutCommand);
}

bool AMocScholaBridge::ParseCommandJSON(const FString& JSON, FScholaCommand& OutCommand)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JSON);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	OutCommand.Command = JsonObject->GetStringField(TEXT("command"));

	if (JsonObject->HasField(TEXT("team_id")))
	{
		OutCommand.TeamID = JsonObject->GetIntegerField(TEXT("team_id"));
	}

	if (JsonObject->HasField(TEXT("agent_id")))
	{
		OutCommand.AgentID = JsonObject->GetIntegerField(TEXT("agent_id"));
	}

	if (JsonObject->HasField(TEXT("action")))
	{
		TSharedPtr<FJsonObject> ActionObj = JsonObject->GetObjectField(TEXT("action"));

		OutCommand.OptionType = ActionObj->GetIntegerField(TEXT("option_type"));
		OutCommand.OptionDuration = ActionObj->GetNumberField(TEXT("option_duration"));

		const TArray<TSharedPtr<FJsonValue>>* TargetArray;
		if (ActionObj->TryGetArrayField(TEXT("target_position"), TargetArray))
		{
			if (TargetArray->Num() >= 3)
			{
				OutCommand.TargetPosition.X = (*TargetArray)[0]->AsNumber();
				OutCommand.TargetPosition.Y = (*TargetArray)[1]->AsNumber();
				OutCommand.TargetPosition.Z = (*TargetArray)[2]->AsNumber();
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* WeightsArray;
		if (ActionObj->TryGetArrayField(TEXT("eqs_weights"), WeightsArray))
		{
			for (const TSharedPtr<FJsonValue>& Value : *WeightsArray)
			{
				OutCommand.EQSWeights.Add(Value->AsNumber());
			}
		}
	}

	return true;
}

FString AMocScholaBridge::SerializeResponseJSON(const FScholaResponse& Response)
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());

	JsonObject->SetStringField(TEXT("status"), Response.Status);

	if (!Response.Error.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("error"), Response.Error);
	}

	if (Response.Observation.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ObsArray;
		for (float Value : Response.Observation)
		{
			ObsArray.Add(MakeShareable(new FJsonValueNumber(Value)));
		}
		JsonObject->SetArrayField(TEXT("observation"), ObsArray);
	}

	JsonObject->SetNumberField(TEXT("reward"), Response.Reward);
	JsonObject->SetBoolField(TEXT("done"), Response.bDone);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	return OutputString;
}

void AMocScholaBridge::CloseClientConnection(FSocket* ClientSocket)
{
	if (ClientSocket)
	{
		ClientSocket->Close();
		SocketSubsystem->DestroySocket(ClientSocket);
		ControlledAgents.Remove(ClientSocket);
	}
}
