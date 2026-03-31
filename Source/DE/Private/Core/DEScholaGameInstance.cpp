// DEScholaGameInstance.cpp - Custom GameInstance implementation

#include "Core/DEScholaGameInstance.h"


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


bool UDEScholaGameInstance::StartCommunicationServer(int32 Port)
{


	return true;
}

void UDEScholaGameInstance::StopCommunicationServer()
{
	if (!bServerRunning)
	{
		return;
	}

	bServerRunning = false;
}
