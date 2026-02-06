// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EQSWeightParameters.generated.h"

/**
 * EQS Weight Parameters (8-dim output from RL policy)
 * These weights configure Environment Query System tests for spatial reasoning.
 * See v10.0Architecture.md Section 2.5 for detailed parameter descriptions.
 */
USTRUCT(BlueprintType)
struct FEQSWeightParameters
{
	GENERATED_BODY()

	/** Distance to enemy objective (approach enemy base) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float EnemyObjectiveProximity = 0.0f;

	/** Distance to ally objective (defend friendly base) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float AllyObjectiveProximity = 0.0f;

	/** Cover density (prioritize cover points) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float CoverDensity = 0.0f;

	/** Enemy visibility (maintain line-of-sight) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float EnemyVisibility = 0.0f;

	/** Ally proximity (stay near teammates) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float AllyProximity = 0.0f;

	/** Combat range (preferred engagement distance) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float CombatRange = 0.0f;

	/** Pickup proximity (collect health/ammo) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float PickupProximity = 0.0f;

	/** Height advantage (seek elevated positions) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS Weights")
	float HeightAdvantage = 0.0f;

	FEQSWeightParameters() = default;

	/** Convert to flat array for logging/debugging */
	TArray<float> ToArray() const
	{
		return {
			EnemyObjectiveProximity,
			AllyObjectiveProximity,
			CoverDensity,
			EnemyVisibility,
			AllyProximity,
			CombatRange,
			PickupProximity,
			HeightAdvantage
		};
	}

	/** Create from flat array (from ONNX output) */
	static FEQSWeightParameters FromArray(const TArray<float>& Weights)
	{
		check(Weights.Num() == 8);

		FEQSWeightParameters Params;
		Params.EnemyObjectiveProximity = Weights[0];
		Params.AllyObjectiveProximity = Weights[1];
		Params.CoverDensity = Weights[2];
		Params.EnemyVisibility = Weights[3];
		Params.AllyProximity = Weights[4];
		Params.CombatRange = Weights[5];
		Params.PickupProximity = Weights[6];
		Params.HeightAdvantage = Weights[7];

		return Params;
	}

	/** Clamp all weights to valid range [-1, 1] */
	void Clamp()
	{
		EnemyObjectiveProximity = FMath::Clamp(EnemyObjectiveProximity, -1.0f, 1.0f);
		AllyObjectiveProximity = FMath::Clamp(AllyObjectiveProximity, -1.0f, 1.0f);
		CoverDensity = FMath::Clamp(CoverDensity, -1.0f, 1.0f);
		EnemyVisibility = FMath::Clamp(EnemyVisibility, -1.0f, 1.0f);
		AllyProximity = FMath::Clamp(AllyProximity, -1.0f, 1.0f);
		CombatRange = FMath::Clamp(CombatRange, -1.0f, 1.0f);
		PickupProximity = FMath::Clamp(PickupProximity, -1.0f, 1.0f);
		HeightAdvantage = FMath::Clamp(HeightAdvantage, -1.0f, 1.0f);
	}
};

