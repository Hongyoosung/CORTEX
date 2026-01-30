#include "Observation/ObservationElement.h"
#include "Core/ProfilingMacros.h"  // v6.0: Performance profiling

TArray<float> FObservationElement::ToFeatureVector(bool bIncludeStrategy) const
{
    SCOPE_CYCLE_COUNTER(STAT_ObservationBuild);  // v6.0: Profile observation building (target: <0.5ms)

    TArray<float> Features;
    Features.Reserve(52);  // v9.0: 52 base features (46 + 6 objective context, strategy context added by network)

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
    // SUPPORT CONTEXT (5 features) - v8.0: Full ally context for Support strategy
    // Critical for Support head to learn differentiated tactical parameters
    // ========================================

    // Ally needs help flag (1)
    Features.Add(bAllyNeedsHelp ? 1.0f : 0.0f);

    // Ally health (1) - already normalized [0, 1]
    Features.Add(FMath::Clamp(AllyHealth, 0.0f, 1.0f));

    // Ally distance (1) - already normalized [0, 1]
    Features.Add(FMath::Clamp(AllyDistance, 0.0f, 1.0f));

    // Ally direction (2) - normalized 2D vector
    Features.Add(static_cast<float>(AllyDirection.X));
    Features.Add(static_cast<float>(AllyDirection.Y));

    // ========================================
    // OBJECTIVE CONTEXT (6 features) - v9.0
    // Enables strategy-specific reward functions without explicit objective assignment
    // ========================================

    // Friendly objective distance (1) - already normalized [0, 1]
    Features.Add(FMath::Clamp(FriendlyObjectiveDistance, 0.0f, 1.0f));

    // Friendly objective direction (2) - normalized 2D vector
    Features.Add(static_cast<float>(FriendlyObjectiveDirection.X));
    Features.Add(static_cast<float>(FriendlyObjectiveDirection.Y));

    // Hostile objective distance (1) - already normalized [0, 1]
    Features.Add(FMath::Clamp(HostileObjectiveDistance, 0.0f, 1.0f));

    // Hostile objective direction (2) - normalized 2D vector
    Features.Add(static_cast<float>(HostileObjectiveDirection.X));
    Features.Add(static_cast<float>(HostileObjectiveDirection.Y));

    if (bIncludeStrategy)
    {
        for (int32 i = 0; i < 4; i++)
        {
            if (i == AssignedStrategyIndex)
            {
                Features.Add(1.0f);
            }
            else
            {
                Features.Add(0.0f);
            }
        }
    }

	UE_LOG(LogTemp, Warning, TEXT("✅ [Observation] Built feature vector with %d features."), Features.Num());

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

    // Support Context (v8.0: 5 features - Full ally context)
    bAllyNeedsHelp = false;
    AllyHealth = 1.0f;
    AllyDistance = 0.0f;
    AllyDirection = FVector2D::ZeroVector;

    // Objective Context (v9.0: 6 features)
    FriendlyObjectiveDistance = 1.0f;
    FriendlyObjectiveDirection = FVector2D::ZeroVector;
    HostileObjectiveDistance = 1.0f;
    HostileObjectiveDirection = FVector2D::ZeroVector;
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
