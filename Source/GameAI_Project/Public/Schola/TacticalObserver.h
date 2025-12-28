// TacticalObserver.h - Schola observer for 70-feature tactical observation (v4.0)

#pragma once

#include "CoreMinimal.h"
#include "Observers/AbstractObservers.h"
#include "TacticalObserver.generated.h"

class UFollowerAgentComponent;

/**
 * Schola observer that exposes 74-feature tactical observation for RL training (v4.0 Simplified).
 * Used for live training via gRPC connection to Python/RLlib.
 *
 * Total: 74 features = 70 tactical + 4 objective embedding
 *
 * Tactical Features (70 total):
 * - Agent State: Position, Velocity, Rotation, Health, Shield (11 features) [Stamina removed]
 * - Combat State: WeaponCooldown, Ammunition, WeaponType (3 features)
 * - Environment: RaycastDistances, RaycastHitTypes (32 features)
 * - Enemies: VisibleEnemyCount, NearbyEnemies (16 features)
 * - Tactical: Cover info, Terrain (5 features)
 * - Temporal: TimeSinceLastAction, LastActionType (2 features)
 * - Combat Proximity: DistanceToNearestEnemy (1 feature)
 *
 * Objective Embedding (4 features, one-hot, v4.0 simplified):
 * - [70]: Assault, [71]: Defend, [72]: Support, [73]: Retreat
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tactical Observer"))
class GAMEAI_PROJECT_API UTacticalObserver : public UBoxObserver
{
	GENERATED_BODY()

public:
	UTacticalObserver();

	// UBoxObserver interface
	virtual FBoxSpace GetObservationSpace() const override;
	virtual void CollectObservations(FBoxPoint& OutObservations) override;
	virtual void InitializeObserver() override;
	virtual void ResetObserver() override;

	/** The follower agent component to observe */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observer")
	UFollowerAgentComponent* FollowerAgent = nullptr;

	/** Auto-find follower agent on owner actor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Observer")
	bool bAutoFindFollower = true;

protected:
	/** Cached observation space */
	FBoxSpace CachedObservationSpace;

	/** Find follower agent component */
	UFollowerAgentComponent* FindFollowerAgent() const;
};
