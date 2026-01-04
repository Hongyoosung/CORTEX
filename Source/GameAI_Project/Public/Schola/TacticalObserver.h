// TacticalObserver.h - Schola observer for 64-feature streamlined observation (v5.0)

#pragma once

#include "CoreMinimal.h"
#include "Observers/AbstractObservers.h"
#include "TacticalObserver.generated.h"

class UFollowerAgentComponent;

/**
 * Schola observer for v5.0 Multi-Head Architecture (64 streamlined features)
 * Used for live RL training via gRPC connection to Python/RLlib.
 *
 * v5.0 STREAMLINED OBSERVATION (64 features total):
 * - Agent State (7): position(3), velocity(3), health(1)
 * - Combat (1): enemy_distance(1)
 * - Perception (32): raycast_distances(16), raycast_hit_types(16)
 * - Enemy Info (16): enemy_count(1), nearby_enemies(15)
 * - Tactical (4): has_cover(1), cover_distance(1), cover_direction(2)
 * - Support Context (4): ally_needs_help(1), ally_health(1), ally_distance(1), ally_direction(1)
 *
 * REMOVED in v5.0 (-14 features from v4.0):
 * - Rotation (3) - engine handles auto-aim
 * - Shield (1) - not implemented
 * - Ammo (1) - infinite ammo assumed (team-level only in MCTS)
 * - Cooldown (1) - handled by engine
 * - Weapon type (1) - single weapon type
 * - Terrain type (1) - covered by raycasts
 * - Objective embedding (4) - replaced by strategy head selection
 * - Unused padding (2)
 *
 * Note: Strategy selection is handled by multi-head network, not as input feature.
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
