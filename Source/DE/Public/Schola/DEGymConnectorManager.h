#pragma once

#include "CoreMinimal.h"
#include "TrainingUtils/GymConnectorManager.h"
#include "DEGymConnectorManager.generated.h"

/**
* Throttled GymConnectorManager that calls Connector->Step() at a fixed interval     
* instead of every frame. Prevents the gRPC blocking loop from consuming the
* game thread at 60+ Hz.
*
* Usage: reparent BP_GymConnectorManager to this class.
*/
UCLASS(Blueprintable)
class DE_API ADEGymConnectorManager : public AGymConnectorManager
{
    GENERATED_BODY()

public:
    ADEGymConnectorManager();

    virtual void Tick(float DeltaTime) override;

    /** Step interval in seconds. Default: 0.5s = 2 Hz. */
    UPROPERTY(EditAnywhere, Category = "Schola|Throttling",
        meta = (ClampMin = "0.01", ClampMax = "10.0"))
    float StepInterval = 0.3f;

private:
    virtual void BeginPlay() override;

    float StepAccumulator = 0.0f;
};