#include "Observation/ObservationElement.h"
#include "Core/ProfilingMacros.h"  // v6.0: Performance profiling

TArray<float> FObservationElement::ToFeatureVector() const
{
    SCOPE_CYCLE_COUNTER(STAT_ObservationBuild);  // v6.0: Profile observation building (target: <0.5ms)

    TArray<float> Features;
    Features.Reserve(68);  // v6.0: 68 features (64 base + 4 objective context)

    // ========================================
    // AGENT STATE (7 features) - v5.0 streamlined
    // Removed: rotation(3), shield(1)
    // ========================================

    // Position (3)
    Features.Add(Position.X / 10000.0f);  // Normalize by typical map size
    Features.Add(Position.Y / 10000.0f);
    Features.Add(Position.Z / 10000.0f);

    // Health (1) - already normalized [0, 1]
    Features.Add(FMath::Clamp(AgentHealth, 0.0f, 1.0f));

    // ========================================
    // COMBAT STATE (1 feature) - v5.0 streamlined
    // Removed: cooldown(1), ammo(1), weapon(1)
    // ========================================

    // Distance to nearest enemy (1) - already normalized [0, 1]
    Features.Add(FMath::Clamp(DistanceToNearestEnemy, 0.0f, 1.0f));

    // ========================================
    // ENVIRONMENT PERCEPTION (32 features)
    // ========================================

    // Raycast Distances (16) - Already normalized 0-1
    for (int32 i = 0; i < 16; ++i)
    {
        Features.Add(i < RaycastDistances.Num() ? RaycastDistances[i] : 1.0f);
    }

    // ========================================
    // ENEMY INFORMATION (16 features)
    // ========================================

    // Visible Enemy Count (1)
    Features.Add(FMath::Clamp(static_cast<float>(VisibleEnemyCount) / 10.0f, 0.0f, 1.0f));

    // Nearby Enemies (5 × 3 = 15)
    for (int32 i = 0; i < 5; ++i)
    {
        if (i < NearbyEnemies.Num())
        {
            TArray<float> EnemyFeatures = NearbyEnemies[i].ToFeatureArray();
            Features.Append(EnemyFeatures);
        }
        else
        {
            // Padding for missing enemies
            Features.Add(1.0f);  // Max distance
            Features.Add(1.0f);  // Full health (no threat)
            Features.Add(0.0f);  // No angle
        }
    }

    // ========================================
    // TACTICAL CONTEXT (4 features) - v5.0 streamlined
    // Removed: terrain(1)
    // ========================================

    Features.Add(bHasCover ? 1.0f : 0.0f);
    Features.Add(FMath::Clamp(NearestCoverDistance, 0.0f, 1.0f));  // Already normalized
    Features.Add(CoverDirection.X);  // Already normalized
    Features.Add(CoverDirection.Y);

    // ========================================
    // SUPPORT CONTEXT (1 features) - v5.0 NEW
    // For Support strategy head selection
    // ========================================
    Features.Add(FMath::Clamp(AllyDistance, 0.0f, 1.0f));


    check(Features.Num() == 68);
    return Features;
}

void FObservationElement::Reset()
{
    // Agent State (v5.0: 7 features)
    Position = FVector::ZeroVector;
    AgentHealth = 1.0f;  // Normalized [0, 1]

    // Combat State (v5.0: 1 feature)
    DistanceToNearestEnemy = 1.0f;  // Normalized [0, 1]

    // Environment Perception
    RaycastDistances.Init(1.0f, 16);

    // Enemy Information
    VisibleEnemyCount = 0;
    NearbyEnemies.Init(FEnemyObservation(), 5);

    // Tactical Context (v5.0: 4 features)
    bHasCover = false;
    NearestCoverDistance = 1.0f;  // Normalized [0, 1]
    CoverDirection = FVector2D::ZeroVector;

    // Support Context (v5.0: 1 features - NEW)
    AllyDistance = 0.0f;
}

float FObservationElement::CalculateSimilarity(
    const FObservationElement& A,
    const FObservationElement& B)
{
    // Weighted feature comparison (v5.0: all values already normalized [0, 1])
    float HealthDiff = FMath::Abs(A.AgentHealth - B.AgentHealth);
    float DistanceDiff = FMath::Abs(A.DistanceToNearestEnemy - B.DistanceToNearestEnemy);
    float EnemyDiff = FMath::Abs(A.VisibleEnemyCount - B.VisibleEnemyCount) / 10.0f;

    // Position similarity (still needs normalization)
    float PositionDiff = FVector::Dist(A.Position, B.Position) / 10000.0f;

    // Weighted average
    float WeightedDiff =
        0.25f * HealthDiff +
        0.2f * DistanceDiff +
        0.2f * EnemyDiff +
        0.2f * PositionDiff;

    // Convert difference to similarity (exponential decay)
    return FMath::Exp(-WeightedDiff * 5.0f);  // [0, 1], higher = more similar
}
