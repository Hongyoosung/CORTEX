// TacticalObserver.h - Schola observer for 64-feature streamlined observation (v5.0)

#pragma once

#include "CoreMinimal.h"
#include "Observers/AbstractObservers.h"
#include "TacticalObserver.generated.h"

class UFollowerAgentComponent;

/**
 * Schola observer for v6.0 Single-Head Architecture (68 features)
 * Used for live RL training via gRPC connection to Python/RLlib.
 *
 * v6.0 OBSERVATION WITH OBJECTIVE CONTEXT (68 features total):
 * - Agent State (7): position(3), velocity(3), health(1)
 * - Combat (1): enemy_distance(1)
 * - Perception (32): raycast_distances(16), raycast_hit_types(16)
 * - Enemy Info (16): enemy_count(1), nearby_enemies(15)
 * - Tactical (4): has_cover(1), cover_distance(1), cover_direction(2)
 * - Support Context (4): ally_needs_help(1), ally_health(1), ally_distance(1), ally_direction(1)
 * - Objective Context (4): type(1), distance(1), direction(2) - NEW in v6.0
 *
 * v6.0 CHANGES (from v5.0):
 * - Added: Objective context (4) - informs RL about MCTS-assigned objective
 * - Removed: Strategy index (1) - strategy now selected by single-head RL policy
 * - Total: 64 → 68 features
 *
 * Note: Strategy selection is now handled by RL policy based on observation + objective context.
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
