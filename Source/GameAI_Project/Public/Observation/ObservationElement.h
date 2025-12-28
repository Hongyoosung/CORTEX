#pragma once

#include "CoreMinimal.h"
#include "ObservationTypes.h"
#include "ObservationElement.generated.h"

/**
 * Enhanced observation structure for individual agents
 * 70 total features, fully normalized for neural network input
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FObservationElement
{
    GENERATED_BODY()

    //--------------------------------------------------------------------------
    // AGENT STATE (11 features)
    //--------------------------------------------------------------------------

    /** Agent position in world space */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Agent")
    FVector Position = FVector::ZeroVector;  // 3 features (X, Y, Z)

    /** Agent velocity */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Agent")
    FVector Velocity = FVector::ZeroVector;  // 3 features (VX, VY, VZ)

    /** Agent rotation */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Agent")
    FRotator Rotation = FRotator::ZeroRotator;  // 3 features (Pitch, Yaw, Roll)

    /** Health percentage (0-100) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Agent")
    float AgentHealth = 100.0f;  // 1 feature

    /** Shield/Armor percentage (0-100) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Agent")
    float Shield = 100.0f;  // 1 feature

    //--------------------------------------------------------------------------
    // COMBAT STATE (3 features)
    //--------------------------------------------------------------------------

    /** Weapon cooldown remaining (seconds) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Combat")
    float WeaponCooldown = 0.0f;  // 1 feature

    /** Current ammunition count/percentage */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Combat")
    float Ammunition = 100.0f;  // 1 feature

    /** Current weapon type ID */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Combat")
    int32 CurrentWeaponType = 0;  // 1 feature

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Combat")
    float DistanceToNearestEnemy = 99999.0f;

    //--------------------------------------------------------------------------
    // ENVIRONMENT PERCEPTION (32 features)
    //--------------------------------------------------------------------------

    /** Raycast distances (16 rays, 360° coverage), normalized by max range */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Perception")
    TArray<float> RaycastDistances;  // 16 features

    /** Raycast hit types (what each ray detected) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Perception")
    TArray<ERaycastHitType> RaycastHitTypes;  // 16 features (encoded as 0-7)

    //--------------------------------------------------------------------------
    // ENEMY INFORMATION (16 features)
    //--------------------------------------------------------------------------

    /** Number of visible enemies */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Enemies")
    int32 VisibleEnemyCount = 0;  // 1 feature

    /** Nearby enemies (up to 5, sorted by distance) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Enemies")
    TArray<FEnemyObservation> NearbyEnemies;  // 5×3 = 15 features

    //--------------------------------------------------------------------------
    // TACTICAL CONTEXT (5 features)
    //--------------------------------------------------------------------------

    /** Is cover available nearby? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Tactical")
    bool bHasCover = false;  // 1 feature (0 or 1)

    /** Distance to nearest cover (meters) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Tactical")
    float NearestCoverDistance = 9999.0f;  // 1 feature

    /** Direction to nearest cover (normalized 2D) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Tactical")
    FVector2D CoverDirection = FVector2D::ZeroVector;  // 2 features

    /** Current terrain type */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Tactical")
    ETerrainType CurrentTerrain = ETerrainType::Flat;  // 1 feature (encoded as 0-4)

    //--------------------------------------------------------------------------
    // TEMPORAL FEATURES (2 features)
    //--------------------------------------------------------------------------

    /** Time since last action (seconds) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Temporal")
    float TimeSinceLastAction = 0.0f;  // 1 feature

    /** Last action type ID */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Temporal")
    int32 LastActionType = -1;  // 1 feature


    //--------------------------------------------------------------------------
    // CONSTRUCTOR & UTILITY FUNCTIONS
    //--------------------------------------------------------------------------

    /**
     * Constructor with default initialization
     */
    FObservationElement()
    {
        // Initialize raycast arrays with default values
        RaycastDistances.Init(1.0f, 16);  // 16 rays, max distance = 1.0 (normalized)
        RaycastHitTypes.Init(ERaycastHitType::None, 16);

        // Initialize enemy array with empty observations
        NearbyEnemies.Init(FEnemyObservation(), 5);  // Track top 5 closest enemies
    }

    /** Convert observation to normalized feature vector (70 elements) */
    TArray<float> ToFeatureVector() const;

    /** Get feature count */
    static int32 GetFeatureCount() { return 70; }

    /** Reset to default values */
    void Reset();

    /** Initialize raycasts arrays with proper size */
    void InitializeRaycasts(int32 NumRays = 16)
    {
        RaycastDistances.Init(1.0f, NumRays);
        RaycastHitTypes.Init(ERaycastHitType::None, NumRays);
    }

    /** Calculate observation similarity (for MCTS tree reuse) */
    static float CalculateSimilarity(const FObservationElement& A, const FObservationElement& B);
};
