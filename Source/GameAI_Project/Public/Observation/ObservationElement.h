#pragma once

#include "CoreMinimal.h"
#include "ObservationTypes.h"
#include "ObservationElement.generated.h"

/**
 * CORTEX v10.1 Observation State
 * * The primary input for the Learned World Model and Value Network.
 * Total Feature Count: 56 Floats
 * * Structure:
 * - Agent Physics & State (9)
 * - Combat Awareness (1)
 * - Raycast Perception (16)
 * - Ally Support Context (5)
 * - Enemy Info (15)
 * - Tactical/Cover (4)
 * - Objectives (6)
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FObservationElement
{
    GENERATED_BODY()

    //--------------------------------------------------------------------------
    // 1. AGENT PHYSICS & STATE (7 features)
    // Essential for World Model prediction (State t -> State t+1)
    //--------------------------------------------------------------------------

    /** World Position (Normalized relative to map bounds usually, or local) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Agent")
    FVector Position = FVector::ZeroVector; // 3 floats

    /** Agent Velocity (Normalized by MaxSpeed) - NEW for v10.1 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Agent")
    FVector Velocity = FVector::ZeroVector; // 3 floats

    /** Health percentage (0-1) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Agent")
    float AgentHealth = 1.0f; // 1 float


    //--------------------------------------------------------------------------
    // 2. COMBAT AWARENESS (1 feature)
    //--------------------------------------------------------------------------

    /** Distance to nearest enemy (Normalized) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Combat")
    float DistanceToNearestEnemy = 1.0f;

    //--------------------------------------------------------------------------
    // 3. PERCEPTION (16 features)
    //--------------------------------------------------------------------------

    /** Lidar-like raycasts (Normalized Distance) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Perception")
    TArray<float> RaycastDistances; 

    //--------------------------------------------------------------------------
    // 4. SUPPORT CONTEXT (5 features)
    //--------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Support")
    bool bAllyNeedsHelp = false; // 1 float

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Support")
    float AllyHealth = 1.0f; // 1 float

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Support")
    float AllyDistance = 1.0f; // 1 float

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Support")
    FVector2D AllyDirection = FVector2D::ZeroVector; // 2 floats

    //--------------------------------------------------------------------------
    // 5. ENEMY INFO (15 features)
    // Top 5 enemies sorted by threat/distance
    //--------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Enemies")
    TArray<FEnemyObservation> NearbyEnemies; // 5 * 3 = 15 floats

    //--------------------------------------------------------------------------
    // 6. TACTICAL / COVER (4 features)
    //--------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Tactical")
    bool bHasCover = false; // 1 float

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Tactical")
    float NearestCoverDistance = 1.0f; // 1 float

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Tactical")
    FVector2D CoverDirection = FVector2D::ZeroVector; // 2 floats

    //--------------------------------------------------------------------------
    // 7. OBJECTIVES (6 features)
    //--------------------------------------------------------------------------

    /** Friendly Objective (Capture point/Flag) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Objective")
    float FriendlyObjDistance = 1.0f; // 1 float

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Objective")
    FVector2D FriendlyObjDirection = FVector2D::ZeroVector; // 2 floats

    /** Hostile Objective */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Objective")
    float HostileObjDistance = 1.0f; // 1 float

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CORTEX|Objective")
    FVector2D HostileObjDirection = FVector2D::ZeroVector; // 2 floats

    //--------------------------------------------------------------------------
    // UTILITIES
    //--------------------------------------------------------------------------

    FObservationElement()
    {
        RaycastDistances.Init(1.0f, 16);
        NearbyEnemies.Init(FEnemyObservation(), 5);
    }

    /**
     * Serializes the entire struct into a flat array for the Neural Network (ONNX).
     * Guaranteed size: 54
     */
    TArray<float> ToFeatureVector() const
    {
        TArray<float> Features;
        Features.Reserve(56);

        // 1. Agent (7)
        Features.Add(Position.X); Features.Add(Position.Y); Features.Add(Position.Z);
        Features.Add(Velocity.X); Features.Add(Velocity.Y); Features.Add(Velocity.Z);
        Features.Add(AgentHealth);


        // 2. Combat (1)
        Features.Add(DistanceToNearestEnemy);

        // 3. Perception (16)
        for (float Ray : RaycastDistances) Features.Add(Ray);

        // 4. Support (5)
        Features.Add(bAllyNeedsHelp ? 1.0f : 0.0f);
        Features.Add(AllyHealth);
        Features.Add(AllyDistance);
        Features.Add(AllyDirection.X); Features.Add(AllyDirection.Y);

        // 5. Enemies (15)
        for (const FEnemyObservation& Enemy : NearbyEnemies)
        {
            Features.Append(Enemy.ToFeatureArray());
        }

        // 6. Tactical (4)
        Features.Add(bHasCover ? 1.0f : 0.0f);
        Features.Add(NearestCoverDistance);
        Features.Add(CoverDirection.X); Features.Add(CoverDirection.Y);

        // 7. Objectives (6)
        Features.Add(FriendlyObjDistance);
        Features.Add(FriendlyObjDirection.X); Features.Add(FriendlyObjDirection.Y);
        Features.Add(HostileObjDistance);
        Features.Add(HostileObjDirection.X); Features.Add(HostileObjDirection.Y);

        return Features;
    }

    /** Returns the fixed input size for CORTEX v10.1 models */
    static int32 GetFeatureCount() { return 56; }
};