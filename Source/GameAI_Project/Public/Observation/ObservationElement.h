#pragma once

#include "CoreMinimal.h"
#include "ObservationTypes.h"
#include "RL/RLTypes.h"  // v6.0: For FObjectiveContext
#include "ObservationElement.generated.h"

/**
 * Enhanced observation structure for individual agents (v6.0)
 * 68 total features, fully normalized for neural network input
 *
 * v6.0 Changes (from v5.0):
 * - Added: Objective context(4) - type, distance, direction to assigned objective
 * - Total features: 64 base + 4 objective context = 68
 *
 * v5.0 Changes (from v4.0):
 * - Removed: rotation(3), shield(1), cooldown(1), ammo(1), weapon(1), terrain(1), temporal(2)
 * - Added: Support context(4) via FAllyContext
 * - Engine handles auto-aim, infinite ammo assumed, single weapon type
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FObservationElement
{
    GENERATED_BODY()

    //--------------------------------------------------------------------------
    // AGENT STATE (7 features) - v5.0 streamlined
    //--------------------------------------------------------------------------

    /** Agent position in world space */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Agent")
    FVector Position = FVector::ZeroVector;  // 3 features (X, Y, Z)

    /** Agent velocity */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Agent")
    FVector Velocity = FVector::ZeroVector;  // 3 features (VX, VY, VZ)

    /** Health percentage (0-1 normalized) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Agent")
    float AgentHealth = 1.0f;  // 1 feature

    //--------------------------------------------------------------------------
    // COMBAT STATE (1 feature) - v5.0 streamlined
    //--------------------------------------------------------------------------

    /** Distance to nearest enemy (normalized) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Combat")
    float DistanceToNearestEnemy = 1.0f;  // 1 feature (normalized by max range)

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
    // TACTICAL CONTEXT (4 features) - v5.0 streamlined (removed terrain)
    //--------------------------------------------------------------------------

    /** Is cover available nearby? */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Tactical")
    bool bHasCover = false;  // 1 feature (0 or 1)

    /** Distance to nearest cover (normalized by max range) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Tactical")
    float NearestCoverDistance = 1.0f;  // 1 feature

    /** Direction to nearest cover (normalized 2D) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Tactical")
    FVector2D CoverDirection = FVector2D::ZeroVector;  // 2 features

    //--------------------------------------------------------------------------
    // SUPPORT CONTEXT (4 features) - v5.0 NEW for Support strategy
    //--------------------------------------------------------------------------

    /** Whether any ally needs immediate help (health < 50% or surrounded) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Support")
    bool bAllyNeedsHelp = false;  // 1 feature

    /** Normalized health of the ally most in need [0, 1] */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Support")
    float AllyHealth = 1.0f;  // 1 feature

    /** Distance to ally in need (normalized by max range) [0, 1] */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Support")
    float AllyDistance = 0.0f;  // 1 feature

    /** Direction to ally (normalized angle [-1, 1]) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Support")
    float AllyDirectionAngle = 0.0f;  // 1 feature

    //--------------------------------------------------------------------------
    // OBJECTIVE CONTEXT (4 features) - v6.0 NEW
    //--------------------------------------------------------------------------

    /** Objective context from MCTS assignment (v6.0) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Objective")
    FObjectiveContext ObjectiveContext;  // 4 features (type, distance, direction)

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

    /** Convert observation to normalized feature vector (68 elements) - v6.0 */
    TArray<float> ToFeatureVector() const;

    /** Get feature count (v6.0: 68 features = 64 base + 4 objective context) */
    static int32 GetFeatureCount() { return 68; }

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
