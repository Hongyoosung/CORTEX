// TacticalObserver.h - Schola observer for 64-feature streamlined observation (v5.0)

#pragma once

#include "CoreMinimal.h"
#include "Observers/AbstractObservers.h"
#include "TacticalObserver.generated.h"

class AFollowerCharacter;

/**
 * Schola observer for v6.0 Single-Head Architecture (56 features)
 * Used for live RL training via gRPC connection to Python/RLlib.
 *
 * OBSERVATION WITH STRATEGIC CONTEXT (52 base + 4, 56 features total):
 *
 *
 * Note: Strategy selection is now handled by RL policy based on observation + Strategic context.
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

	void SetFollowerAgent(AActor* OwnerAgent);



private:
	TObjectPtr<AFollowerCharacter> FollowerAgent = nullptr;

	/** Cached observation space */
	FBoxSpace CachedObservationSpace;
};
