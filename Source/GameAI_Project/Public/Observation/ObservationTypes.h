#pragma once

#include "CoreMinimal.h"
#include "ObservationTypes.generated.h"

/**
 * CORTEX v10.1: Raycast Hit Types
 */
UENUM(BlueprintType)
enum class ERaycastHitType : uint8
{
    None        UMETA(DisplayName = "Nothing"),
    Wall        UMETA(DisplayName = "Wall/Obstacle"),
    Enemy       UMETA(DisplayName = "Enemy"),
    Ally        UMETA(DisplayName = "Ally"),
    Cover       UMETA(DisplayName = "Cover"),
    Other       UMETA(DisplayName = "Other Object")
};

/**
 * CORTEX v10.1: Enemy Perception Unit
 * Used as a sub-component of the World Model Input
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FEnemyObservation
{
    GENERATED_BODY()

    /** Distance to enemy (meters) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Distance = 9999.0f;

    /** Enemy health percentage (0-100) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Health = 100.0f;

    /** Relative angle from agent's forward vector (-180 to 180) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float RelativeAngle = 0.0f;

    /** Cached Actor Reference (Not used in NN Input, Logic only) */
    UPROPERTY(Transient)
    AActor* EnemyActor = nullptr;

    /** * Serializes to 3 floats for the Neural Network 
     * [0]: Normalized Distance (0-1)
     * [1]: Normalized Health (0-1)
     * [2]: Normalized Angle (0-1)
     */
    TArray<float> ToFeatureArray() const
    {
        TArray<float> Features;
        Features.Reserve(3);

        // Normalize Distance (Max perception range assumed 50m/5000u)
        Features.Add(FMath::Clamp(Distance / 5000.0f, 0.0f, 1.0f));
        Features.Add(FMath::Clamp(Health / 100.0f, 0.0f, 1.0f));
        Features.Add((RelativeAngle + 180.0f) / 360.0f);

        return Features;
    }
};