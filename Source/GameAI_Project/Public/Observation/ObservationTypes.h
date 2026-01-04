#pragma once

#include "CoreMinimal.h"
#include "ObservationTypes.generated.h"

/**
 * Types of objects detected by raycasts
 */
UENUM(BlueprintType)
enum class ERaycastHitType : uint8
{
    None        UMETA(DisplayName = "Nothing"),
    Wall        UMETA(DisplayName = "Wall/Obstacle"),
    Enemy       UMETA(DisplayName = "Enemy"),
    Ally        UMETA(DisplayName = "Ally"),
    Cover       UMETA(DisplayName = "Cover"),
    HealthPack  UMETA(DisplayName = "Health Pack"),
    Weapon      UMETA(DisplayName = "Weapon"),
    Other       UMETA(DisplayName = "Other Object")
};

/**
 * Information about a single nearby enemy
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

    /** Enemy actor reference (optional) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    AActor* EnemyActor = nullptr;

    /** Convert to normalized feature array (3 elements) */
    TArray<float> ToFeatureArray() const
    {
        TArray<float> Features;
        Features.Reserve(3);

        // Distance (normalized by max expected range of 5000 units = 50 meters)
        Features.Add(FMath::Clamp(Distance / 5000.0f, 0.0f, 1.0f));

        // Health (normalized to 0-1)
        Features.Add(Health / 100.0f);

        // Relative angle (normalized from -180/180 to 0/1)
        Features.Add((RelativeAngle + 180.0f) / 360.0f);

        return Features;
    }
};
