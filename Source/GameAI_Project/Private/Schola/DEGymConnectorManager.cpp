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
        // Clamp DeltaTime to prevent burst-stepping after editor is hidden/minimized.
        // When UE5 is backgrounded it throttles ticks but DeltaTime reflects real
        // wall-clock elapsed time, causing the accumulator to spike on resume.
        const float ClampedDelta = FMath::Min(DeltaTime, StepInterval);
        StepAccumulator += ClampedDelta;
        if (StepAccumulator >= StepInterval)
        {
            StepAccumulator = 0.0f;
            Connector->Step(); // blocks ~10ms waiting for Python action
        }
    }
}

void ADEGymConnectorManager::BeginPlay()
{
    Super::BeginPlay();
}
