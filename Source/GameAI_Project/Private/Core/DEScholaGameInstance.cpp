// DEScholaGameInstance.cpp - Custom GameInstance implementation

#include "Core/DEScholaGameInstance.h"
#include "Communicator/CommunicationManager.h"

UDEScholaGameInstance::UDEScholaGameInstance()
{
	// CommunicationManager will be created on-demand in GetCommunicationManager()
}

void UDEScholaGameInstance::Init()
{
	Super::Init();

	UE_LOG(LogTemp, Log, TEXT("[DEScholaGameInstance] Initialized"));
}

void UDEScholaGameInstance::Shutdown()
{
	// Stop server before shutdown
	StopCommunicationServer();

	Super::Shutdown();

	UE_LOG(LogTemp, Log, TEXT("[DEScholaGameInstance] Shutdown complete"));
}

UCommunicationManager* UDEScholaGameInstance::GetCommunicationManager()
{
	// Create CommunicationManager if it doesn't exist
	if (!CommunicationManager)
	{
		// Create new instance
		CommunicationManager = NewObject<UCommunicationManager>(this, UCommunicationManager::StaticClass());

		if (!CommunicationManager)
		{
			UE_LOG(LogTemp, Error, TEXT("[DEScholaGameInstance] Failed to create CommunicationManager!"));
			UE_LOG(LogTemp, Error, TEXT("[DEScholaGameInstance] Make sure Schola plugin is enabled in .uproject"));
			return nullptr;
		}

		UE_LOG(LogTemp, Log, TEXT("[DEScholaGameInstance] CommunicationManager created"));
	}

	return CommunicationManager;
}

bool UDEScholaGameInstance::StartCommunicationServer(int32 Port)
{
	if (bServerRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEScholaGameInstance] Server already running on port %d"), ServerPort);
		return true;
	}

	// Get or create CommunicationManager
	UCommunicationManager* ComManager = GetCommunicationManager();
	if (!ComManager)
	{
		return false;
	}

	// Store port
	ServerPort = Port;

	// Override command line to set port (CommunicationManager reads from -ScholaPort)
	FString CommandLineOverride = FCommandLine::Get();
	if (!CommandLineOverride.Contains(TEXT("ScholaPort")))
	{
		CommandLineOverride += FString::Printf(TEXT(" -ScholaPort=%d"), Port);
		FCommandLine::Set(*CommandLineOverride);
		UE_LOG(LogTemp, Log, TEXT("[DEScholaGameInstance] Set command line port to %d"), Port);
	}

	// Initialize CommunicationManager (sets up gRPC server)
	ComManager->Initialize();

	// Start gRPC backends (launches server on configured port)
	bool bSuccess = ComManager->StartBackends();
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("[DEScholaGameInstance] Failed to start gRPC server on port %d"), Port);
		return false;
	}

	bServerRunning = true;
	UE_LOG(LogTemp, Warning, TEXT("[DEScholaGameInstance] ✓ gRPC server started on port %d"), Port);
	UE_LOG(LogTemp, Warning, TEXT("[DEScholaGameInstance] ✓ Ready for Python RLlib connection"));

	return true;
}

void UDEScholaGameInstance::StopCommunicationServer()
{
	if (!bServerRunning)
	{
		return;
	}

	if (CommunicationManager)
	{
		CommunicationManager->ShutdownServer();
		UE_LOG(LogTemp, Log, TEXT("[DEScholaGameInstance] gRPC server stopped"));
	}

	bServerRunning = false;
}
