#pragma once

#include "CoreMinimal.h"
#include "ObservationTypes.h"
#include "RL/RLTypes.h"  
#include "ObservationElement.generated.h"

/**
 * Enhanced observation structure for individual agents (v6.0)
 * 42 total features, fully normalized for neural network input
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FObservationElement
{
    GENERATED_BODY()

    //--------------------------------------------------------------------------
    // AGENT STATE (4 features)
    //--------------------------------------------------------------------------

    /** Agent position in world space */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Agent")
    FVector Position = FVector::ZeroVector;  // 3 features (X, Y, Z)

    /** Health percentage (0-1 normalized) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Agent")
    float AgentHealth = 1.0f;  // 1 feature

    //--------------------------------------------------------------------------
    // COMBAT STATE (1 feature)
    //--------------------------------------------------------------------------

    /** Distance to nearest enemy (normalized) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Combat")
    float DistanceToNearestEnemy = 1.0f;  // 1 feature (normalized by max range)

    //--------------------------------------------------------------------------
    // ENVIRONMENT PERCEPTION (16 features)
    //--------------------------------------------------------------------------

    /** Raycast distances (16 rays, 360° coverage), normalized by max range */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Perception")
    TArray<float> RaycastDistances;  // 16 features


    //--------------------------------------------------------------------------
    // SUPPORT CONTEXT (4 features) - v8.0: Full ally context for Support strategy
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

    /** Direction to ally (normalized 2D vector) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Support")
    FVector2D AllyDirection = FVector2D::ZeroVector;  // 2 features (X, Y)


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
    // TACTICAL CONTEXT (4 features)
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
    // OBJECTIVE CONTEXT (6 features) - v9.0
    //--------------------------------------------------------------------------

    /** Normalized distance to friendly objective [0, 1] */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Objective")
    float FriendlyObjectiveDistance = 1.0f;  // 1 feature

    /** Normalized 2D direction to friendly objective */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Objective")
    FVector2D FriendlyObjectiveDirection = FVector2D::ZeroVector;  // 2 features

    /** Normalized distance to hostile objective [0, 1] */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Objective")
    float HostileObjectiveDistance = 1.0f;  // 1 feature

    /** Normalized 2D direction to hostile objective */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observation|Objective")
    FVector2D HostileObjectiveDirection = FVector2D::ZeroVector;  // 2 features

    UPROPERTY(Transient)
    int32 AssignedStrategyIndex = 0;

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

        // Initialize enemy array with empty observations
        NearbyEnemies.Init(FEnemyObservation(), 5);  // Track top 5 closest enemies
    }

    /** Convert observation to normalized feature vector (52 elements) - v9.0 */
    TArray<float> ToFeatureVector(bool bIncludeStrategy = true) const;

    /** Get feature count (v9.0: 52 base features = 46 + 6 objective context, strategy context added by network) and + 4 strategy index*/
    static int32 GetFeatureCount() { return 56; }

    /** Reset to default values */
    void Reset();

    /** Initialize raycasts arrays with proper size */
    void InitializeRaycasts(int32 NumRays = 16)
    {
        RaycastDistances.Init(1.0f, NumRays);
    }

    /** Calculate observation similarity (for MCTS tree reuse) */
    static float CalculateSimilarity(const FObservationElement& A, const FObservationElement& B);
};
