#include "Schola/DEGymConnectorManager.h"

ADEGymConnectorManager::ADEGymConnectorManager()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ADEGymConnectorManager::Tick(float DeltaTime)
{
    // Call AActor::Tick directly — bypasses AGymConnectorManager::Tick
    // which would call Connector->Step() every frame.
    AActor::Tick(DeltaTime);

    if (!Connector) return;

    // Phase 1: Poll for connection start every frame (Step is non-blocking when NotStarted).
    if (Connector->IsNotStarted())
    {
        Connector->Step(); // internally calls CheckForStart(); no gRPC block here    
        return;
    }

    // Phase 2: Once running, throttle to StepInterval.
    if (Connector->IsRunning())
    {
        StepAccumulator += DeltaTime;
        if (StepAccumulator >= StepInterval)
        {
            StepAccumulator -= StepInterval;
            Connector->Step(); // blocks ~10ms waiting for Python action
        }
    }
}

void ADEGymConnectorManager::BeginPlay()
{
    Super::BeginPlay();
}
