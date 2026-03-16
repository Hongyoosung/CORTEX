// DEThrottledScholaSubsystem.h - Custom world subsystem with time-based decision throttling

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "GymConnectors/AbstractGymConnector.h"
#include "DEThrottledScholaSubsystem.generated.h"

/**
 * Time-Throttled Schola World Subsystem
 *
 * Drives a UAbstractGymConnector at a fixed decision rate, independent of FPS.
 * Replaces the old UScholaManagerSubsystem (removed in Schola 2.0.1).
 *
 * Architecture Integration:
 * - Aligns observation collection (2 Hz) with Squad Commander planning (2 Hz)
 * - Prevents wasted observations between centralized planning cycles
 *
 * Usage:
 * 1. Place an AGymConnectorManager actor in the level to configure the connector,
 *    OR set GymConnector directly via SetGymConnector().
 * 2. Configure DecisionInterval (default: 0.5s = 2 Hz).
 */
UCLASS(config = Game)
class GAMEAI_PROJECT_API UDEThrottledScholaSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Assign the gym connector to drive. Call before the first tick. */
	UFUNCTION(BlueprintCallable, Category = "Schola|Throttling")
	void SetGymConnector(UAbstractGymConnector* InConnector);

	/** Decision interval in seconds (v10.2: 0.5s = 2 Hz, aligned with Squad Commander). */
	UPROPERTY(Config, EditAnywhere, Category = "Schola|Throttling",
		meta = (ClampMin = "0.01", ClampMax = "10.0"))
	float DecisionInterval = 0.5f;

	/** Enable timing diagnostics (logs overhead every 100 decisions). */
	UPROPERTY(Config, EditAnywhere, Category = "Schola|Throttling")
	bool bEnableTimingDiagnostics = true;

	/** The gym connector driven by this subsystem. */
	UPROPERTY(BlueprintReadOnly, Category = "Schola")
	TObjectPtr<UAbstractGymConnector> GymConnector;

private:
	/** Last decision time (FPlatformTime::Seconds()). */
	double LastDecisionTime = 0.0;

	/** Initialization flag. */
	bool bThrottleInitialized = false;

	//========================================
	// TIMING DIAGNOSTICS
	//========================================

	double TotalCollectionOverhead = 0.0;
	int32 DecisionCount = 0;
	double MaxCollectionOverhead = 0.0;
};
