// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DEEQSTypes.generated.h"

/**
 * EQS Weight Parameters (6-dim output from RL policy)
 * These weights configure Environment Query System tests for spatial reasoning.
 * See v10.2 Architecture.md Section 2.5 for detailed parameter descriptions.
 */
USTRUCT(BlueprintType)
struct FDEEQSWeightParameters
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


	FDEEQSWeightParameters() = default;

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
		};
	}

	/** Create from flat array (from ONNX output) */
	static FDEEQSWeightParameters FromArray(const TArray<float>& Weights)
	{
		check(Weights.Num() == 6);

		FDEEQSWeightParameters Params;
		Params.EnemyObjectiveProximity = Weights[0];
		Params.AllyObjectiveProximity = Weights[1];
		Params.CoverDensity = Weights[2];
		Params.EnemyVisibility = Weights[3];
		Params.AllyProximity = Weights[4];
		Params.CombatRange = Weights[5];

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
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("E_Obj:%.2f, A_Obj:%.2f, Cover:%.2f, Vis:%.2f, Ally:%.2f, Rng:%.2f"),
			EnemyObjectiveProximity,
			AllyObjectiveProximity,
			CoverDensity,
			EnemyVisibility,
			AllyProximity,
			CombatRange);
	}
};

