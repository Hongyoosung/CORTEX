#include "Observation/ObservationElement.h"

TArray<float> FObservationElement::ToFeatureVector() const
{
    TArray<float> Features;
    Features.Reserve(70);

    // ========================================
    // AGENT STATE (11 features)
    // ========================================

    // Position (3)
    Features.Add(Position.X / 10000.0f);  // Normalize by typical map size
    Features.Add(Position.Y / 10000.0f);
    Features.Add(Position.Z / 10000.0f);

    // Velocity (3)
    Features.Add(FMath::Clamp(Velocity.X / 1000.0f, -1.0f, 1.0f));
    Features.Add(FMath::Clamp(Velocity.Y / 1000.0f, -1.0f, 1.0f));
    Features.Add(FMath::Clamp(Velocity.Z / 1000.0f, -1.0f, 1.0f));

    // Rotation (3) - Convert to normalized values
    Features.Add(Rotation.Pitch / 180.0f);  // [-1, 1]
    Features.Add(Rotation.Yaw / 180.0f);
    Features.Add(Rotation.Roll / 180.0f);

    // Health, Shield (2)
    Features.Add(AgentHealth / 100.0f);  // [0, 1]
    Features.Add(Shield / 100.0f);

    // ========================================
    // COMBAT STATE (3 features)
    // ========================================

    Features.Add(FMath::Clamp(WeaponCooldown / 5.0f, 0.0f, 1.0f));  // Max 5s cooldown
    Features.Add(Ammunition / 100.0f);
    Features.Add(static_cast<float>(CurrentWeaponType) / 10.0f);  // Max 10 weapon types

    // ========================================
    // ENVIRONMENT PERCEPTION (32 features)
    // ========================================

    // Raycast Distances (16) - Already normalized 0-1
    for (int32 i = 0; i < 16; ++i)
    {
        Features.Add(i < RaycastDistances.Num() ? RaycastDistances[i] : 1.0f);
    }

    // Raycast Hit Types (16)
    for (int32 i = 0; i < 16; ++i)
    {
        ERaycastHitType HitType = (i < RaycastHitTypes.Num())
            ? RaycastHitTypes[i]
            : ERaycastHitType::None;
        Features.Add(static_cast<float>(HitType) / 7.0f);  // 8 enum values (0-7)
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
    // TACTICAL CONTEXT (5 features)
    // ========================================

    Features.Add(bHasCover ? 1.0f : 0.0f);
    Features.Add(FMath::Clamp(NearestCoverDistance / 5000.0f, 0.0f, 1.0f));  // Max 50m
    Features.Add(CoverDirection.X);  // Already normalized
    Features.Add(CoverDirection.Y);
    Features.Add(static_cast<float>(CurrentTerrain) / 4.0f);  // 5 enum values (0-4)

    // ========================================
    // TEMPORAL FEATURES (2 features)
    // ========================================

    Features.Add(FMath::Clamp(TimeSinceLastAction / 10.0f, 0.0f, 1.0f));  // Max 10s
    Features.Add(static_cast<float>(FMath::Max(LastActionType, 0)) / 20.0f);  // Max 20 actions

    // ========================================
    // COMBAT PROXIMITY (1 feature)
    // ========================================

    Features.Add(FMath::Clamp(DistanceToNearestEnemy / 10000.0f, 0.0f, 1.0f));  // Max 100m

    check(Features.Num() == 70);
    return Features;
}

void FObservationElement::Reset()
{
    // Agent State
    Position = FVector::ZeroVector;
    Velocity = FVector::ZeroVector;
    Rotation = FRotator::ZeroRotator;
    AgentHealth = 100.0f;
    Shield = 0.0f;

    // Combat State
    WeaponCooldown = 0.0f;
    Ammunition = 0;
    CurrentWeaponType = 0;

    // Environment Perception
    RaycastDistances.Init(1.0f, 16);
    RaycastHitTypes.Init(ERaycastHitType::None, 16);

    // Enemy Information
    VisibleEnemyCount = 0;
    NearbyEnemies.Init(FEnemyObservation(), 5);

    // Tactical Context
    bHasCover = false;
    NearestCoverDistance = 0.0f;
    CoverDirection = FVector2D::ZeroVector;
    CurrentTerrain = ETerrainType::Unknown;

    // Temporal Features
    TimeSinceLastAction = 0.0f;
    LastActionType = 0;

    // Combat Proximity
    DistanceToNearestEnemy = 99999.0f;
}

float FObservationElement::CalculateSimilarity(
    const FObservationElement& A,
    const FObservationElement& B)
{
    // Weighted feature comparison
    float HealthDiff = FMath::Abs(A.AgentHealth - B.AgentHealth) / 100.0f;
    float DistanceDiff = FMath::Abs(A.DistanceToNearestEnemy - B.DistanceToNearestEnemy) / 10000.0f;
    float EnemyDiff = FMath::Abs(A.VisibleEnemyCount - B.VisibleEnemyCount) / 10.0f;

    // Position similarity
    float PositionDiff = FVector::Dist(A.Position, B.Position) / 10000.0f;

    // Weighted average
    float WeightedDiff =
        0.3f * HealthDiff +
        0.25f * DistanceDiff +
        0.25f * EnemyDiff +
        0.2f * PositionDiff;

    // Convert difference to similarity (exponential decay)
    return FMath::Exp(-WeightedDiff * 5.0f);  // [0, 1], higher = more similar
}
