// MocTacticalObserver.h - MOC v10.2 Executor Agent Observer for Schola Training

#pragma once

#include "CoreMinimal.h"
#include "Observers/AbstractObservers.h"
#include "Types/MocTypes.h"
#include "MocTacticalObserver.generated.h"

class AMocCharacter;
class AMocTrainer;
class UScholaMocAgent;

/**
 * MOC v10.2 Tactical Observer (Schola-Integrated)
 *
 * Purpose:
 * Collects observations for executor agent RL policy training in v10.2 architecture.
 * Extends Schola's UBoxObserver for proper integration with Schola training pipeline.
 *
 * v10.2 Architecture Context:
 * - Layer 1 (Squad Commander): Uses FTeamState (60-dim) for centralized MCTS
 * - Layer 2 (Executor Agents): Use this observer for RL Policy → EQS Weights
 *
 * Observation Space (51-dim):
 * - Base State (48-dim): Agent's local observation (FObservation)
 *   • Self  (7): Position/7500(3), Health(1), Velocity/600(3)
 *   • Allies(16): 4 agents × [RelPos/8000(3), Health(1)]  — always known
 *   • Enemies(20): 5 agents × [RelPos/8000_if_visible(3), Visible(1)]  — LoS only
 *   • Map   (5): PointStatus×5 (+1=friendly / 0=neutral / -1=enemy)
 *   Ally/enemy positions are agent-relative (offset from self) so the same
 *   tactical geometry produces identical features anywhere on the map.
 *
 * - Commanded Strategy (3-dim): One-hot encoding of EStrategyType
 *   • [1,0,0] = Assault
 *   • [0,1,0] = Defend
 *   • [0,0,1] = Support
 *
 * Usage:
 * 1. Add to AMocTrainer.Observers array (Blueprint or C++)
 * 2. Schola automatically calls CollectObservations() during training
 * 3. Observation sent to Python (RLlib) via gRPC
 * 4. Policy returns EQS weights (7-dim) for spatial reasoning
 *
 * Integration:
 * - Owner: AMocTrainer (Schola trainer)
 * - Data Source: AMocCharacter (via trainer reference)
 * - Output: FBoxPoint with 51 continuous values
 */
UCLASS(BlueprintType, EditInlineNew, meta = (DisplayName = "MOC Tactical Observer v10.2"))
class GAMEAI_PROJECT_API UMocTacticalObserver : public UBoxObserver
{
	GENERATED_BODY()

public:
	UMocTacticalObserver();

	//========================================
	// UBoxObserver Interface
	//========================================

	/**
	 * Define observation space bounds
	 * @return FBoxSpace with 51 dimensions, normalized ranges
	 */
	virtual FBoxSpace GetObservationSpace() const override;

	/**
	 * Collect current observation from character
	 * @param OutObservations - FBoxPoint to fill with 51-dim observation vector
	 */
	virtual void CollectObservations(FBoxPoint& OutObservations) override;

	/**
	 * Initialize observer (called by Schola on setup)
	 */
	virtual void InitializeObserver() override;

	/**
	 * Reset observer state (called on episode reset)
	 */
	virtual void ResetObserver() override;

	//========================================
	// MOC-Specific Interface
	//========================================

	/**
	 * Get the controlled character (via trainer reference)
	 * @return AMocCharacter being trained, or nullptr if not found
	 */
	UFUNCTION(BlueprintPure, Category = "MOC|Observer")
	AMocCharacter* GetControlledCharacter() const;

	/**
	 * Validate observation vector (check for NaN, inf, range violations)
	 * @param Observation - Vector to validate
	 * @return true if observation is valid
	 */
	UFUNCTION(BlueprintPure, Category = "MOC|Observer|Debug")
	bool ValidateObservation(const TArray<float>& Observation) const;

#if WITH_EDITOR
	/**
	 * Set debug observations (Schola editor utility)
	 */
	virtual void SetDebugObservations(TPoint& Temp) override;
#endif

protected:
	//========================================
	// Helper Functions
	//========================================

	/**
	 * Gather 48-dim base observation from character
	 * @return FObservation with normalized values
	 */
	FObservation GatherBaseObservation() const;

	/**
	 * Convert commanded strategy to one-hot encoding
	 * @param Strategy - EStrategyType to encode
	 * @return [3-dim] one-hot vector
	 */
	TArray<float> EncodeStrategyOneHot(EStrategyType Strategy) const;

private:
	//========================================
	// Configuration
	//========================================

	/** Enable debug logging (logs every N observations) */
	UPROPERTY(EditAnywhere, Category = "MOC|Observer|Debug")
	bool bEnableDebugLogging = false;

	/** Debug log frequency (log every N calls) */
	UPROPERTY(EditAnywhere, Category = "MOC|Observer|Debug", meta = (EditCondition = "bEnableDebugLogging"))
	int32 DebugLogFrequency = 100;

	//========================================
	// Cached Data
	//========================================

	/** Cached observation space (built in constructor) */
	FBoxSpace CachedObservationSpace;

	/** Cached trainer reference (set in InitializeObserver, training mode only) */
	UPROPERTY()
	AMocTrainer* CachedTrainer;

	/** Cached character reference (set in InitializeObserver, inference mode fallback) */
	UPROPERTY()
	AMocCharacter* CachedCharacter;

	/** Observation call counter (for debug logging) */
	int32 ObservationCallCount = 0;

#if WITH_EDITORONLY_DATA
	/** Last collected observation (for editor inspection) */
	UPROPERTY(VisibleInstanceOnly, Category = "MOC|Observer|Debug")
	TArray<float> DebugLastObservation;
#endif
};
