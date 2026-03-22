// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/DEClassTypes.h"
#include "DEObservationTypes.generated.h"

// ============================================================
// Entity-Centric V2 constants (NNE fixed-shape padding limits)
// ============================================================
static constexpr int32 DE_MAX_ALLIES  = 8;
static constexpr int32 DE_MAX_ENEMIES = 8;
static constexpr int32 DE_MAX_BASES   = 8;

// Token dims: Self=7, Ally=9 (pos+hp+hp_delta+alive+class_onehot), Enemy=8 (pos+hp+visible+conf+class_onehot), Base=7
static constexpr int32 DE_SELF_DIM    = 7;
static constexpr int32 DE_ALLY_DIM    = 9;   // [rel_pos(3), health, hp_delta, alive, is_strike, is_vanguard, is_support]
static constexpr int32 DE_ENEMY_DIM   = 8;   // [rel_pos(3), health, visible, is_strike, is_vanguard, is_support]
static constexpr int32 DE_BASE_DIM    = 7;

// Class one-hot dim: Strike=0, Vanguard=1, Support=2
static constexpr int32 DE_STRATEGY_DIM = 3;

// Total padded flat size: 7 + 8*9 + 8*8 + 8*7 + 8+8+8 + 3 = 226
static constexpr int32 DE_OBS_V2_DIM  = DE_SELF_DIM
                                       + DE_MAX_ALLIES  * DE_ALLY_DIM
                                       + DE_MAX_ENEMIES * DE_ENEMY_DIM
                                       + DE_MAX_BASES   * DE_BASE_DIM
                                       + DE_MAX_ALLIES + DE_MAX_ENEMIES + DE_MAX_BASES
                                       + DE_STRATEGY_DIM;

/**
 * Lightweight per-step agent state snapshot used exclusively by the reward subsystem.
 * Stores raw world-space values — no normalization, no NN serialisation.
 *
 * Replaces the removed FDEObservation (v1) which bundled reward data with NN features.
 * The NN observation path now uses FDEObservationV2 / ToFlatArray() exclusively.
 */
USTRUCT()
struct DE_API FDEAgentSnapshot
{
	GENERATED_BODY()

	/** Agent world position */
	FVector Position = FVector::ZeroVector;

	/** Agent health [0.0-1.0] */
	float Health = 1.0f;

	/** True if the agent was alive when the snapshot was taken */
	bool bIsAlive = true;

	/** Ally world positions (world-space, up to 4 slots) */
	TArray<FVector> AllyPositions;

	/** Ally health values [0.0-1.0] */
	TArray<float> AllyHealths;

	/** Enemy world positions (ZeroVector when not visible) */
	TArray<FVector> EnemyPositions;

	/** Enemy line-of-sight flags */
	TArray<bool> EnemyVisible;

	/** Per-capture-point ownership: +1=friendly, 0=neutral, -1=enemy */
	TArray<float> CapturePointStatuses;

	FDEAgentSnapshot()
	{
		AllyPositions.Init(FVector::ZeroVector, 4);
		AllyHealths.Init(1.0f, 4);
		EnemyPositions.Init(FVector::ZeroVector, 5);
		EnemyVisible.Init(false, 5);
		CapturePointStatuses.Init(0.0f, 5);
	}
};


// ============================================================
// Phase 1 — Entity-Centric Observation V2
// ============================================================

/**
 * Single entity token — flat float array of fixed dimension per entity type.
 * Ally=9-dim, Enemy=8-dim, Base=7-dim.
 */
USTRUCT(BlueprintType)
struct DE_API FDEEntityToken
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "EntityToken")
	TArray<float> Features;

	FDEEntityToken() = default;
	explicit FDEEntityToken(std::initializer_list<float> Values) : Features(Values) {}
};

/**
 * Entity-Centric Observation V2.
 *
 * Variable-length sets of typed entity tokens. Serialised to a fixed-size
 * padded flat array (194-dim) for NNE / ONNX compatibility.
 *
 * Self (7):      [pos_x/7500, pos_y/7500, pos_z/1000, health, vel_x/600, vel_y/600, vel_z/600]
 * Ally (9):      [rel_pos_x/8000, rel_pos_y/8000, rel_pos_z/8000, health, hp_delta, alive, is_strike, is_vanguard, is_support]
 * Enemy (8):     [rel_pos_x/8000, rel_pos_y/8000, rel_pos_z/8000, health, visible, is_strike, is_vanguard, is_support]
 * Base (7):      [rel_pos_x/15000, rel_pos_y/15000, rel_pos_z/1000, ownership,
 *                 capture_progress, is_assigned_target, strategic_value]
 * Class (3):  one-hot [strike, vanguard, support]
 */
USTRUCT(BlueprintType)
struct DE_API FDEObservationV2
{
	GENERATED_BODY()

	/** Self state token (7-dim) */
	UPROPERTY(BlueprintReadWrite, Category = "ObservationV2|Self")
	FDEEntityToken SelfToken;

	/** Variable ally tokens (up to DE_MAX_ALLIES) */
	UPROPERTY(BlueprintReadWrite, Category = "ObservationV2|Allies")
	TArray<FDEEntityToken> AllyTokens;

	/** Variable enemy tokens (up to DE_MAX_ENEMIES) */
	UPROPERTY(BlueprintReadWrite, Category = "ObservationV2|Enemies")
	TArray<FDEEntityToken> EnemyTokens;

	/** Variable base tokens (up to DE_MAX_BASES) */
	UPROPERTY(BlueprintReadWrite, Category = "ObservationV2|Bases")
	TArray<FDEEntityToken> BaseTokens;

	/** Class commanded by the Squad Commander (Strike/Vanguard/Support) */
	UPROPERTY(BlueprintReadWrite, Category = "ObservationV2|Class")
	EDEClassType CommandedClass = EDEClassType::Strike;

	/**
	 * Serialise to padded flat array (226-dim) for NNE/ONNX inference.
	 *
	 * Layout (total = 226):
	 *  [0..6]     Self (7)
	 *  [7..78]    Allies  padded to 8 × 9  (72) — includes hp_delta + ally class one-hot
	 *  [79..142]  Enemies padded to 8 × 8  (64) — includes enemy health + class one-hot
	 *  [143..198] Bases   padded to 8 × 7  (56)
	 *  [199..206] Ally mask  (8) — 0=present, 1=padding
	 *  [207..214] Enemy mask (8)
	 *  [215..222] Base mask  (8)
	 *  [223..225] Class one-hot [strike, vanguard, support] (3)
	 */
	TArray<float> ToFlatArray() const
	{
		TArray<float> Out;
		Out.Reserve(DE_OBS_V2_DIM);

		// Self (7)
		for (float F : SelfToken.Features) Out.Add(F);
		for (int32 i = SelfToken.Features.Num(); i < DE_SELF_DIM; ++i) Out.Add(0.0f);

		// Allies (8 × 5)
		TArray<float> AllyMask;
		AllyMask.Reserve(DE_MAX_ALLIES);
		for (int32 i = 0; i < DE_MAX_ALLIES; ++i)
		{
			if (i < AllyTokens.Num())
			{
				for (float F : AllyTokens[i].Features) Out.Add(F);
				for (int32 j = AllyTokens[i].Features.Num(); j < DE_ALLY_DIM; ++j) Out.Add(0.0f);
				AllyMask.Add(0.0f); // present
			}
			else
			{
				for (int32 j = 0; j < DE_ALLY_DIM; ++j) Out.Add(0.0f);
				AllyMask.Add(1.0f); // padding
			}
		}

		// Enemies (8 × 5)
		TArray<float> EnemyMask;
		EnemyMask.Reserve(DE_MAX_ENEMIES);
		for (int32 i = 0; i < DE_MAX_ENEMIES; ++i)
		{
			if (i < EnemyTokens.Num())
			{
				for (float F : EnemyTokens[i].Features) Out.Add(F);
				for (int32 j = EnemyTokens[i].Features.Num(); j < DE_ENEMY_DIM; ++j) Out.Add(0.0f);
				EnemyMask.Add(0.0f);
			}
			else
			{
				for (int32 j = 0; j < DE_ENEMY_DIM; ++j) Out.Add(0.0f);
				EnemyMask.Add(1.0f);
			}
		}

		// Bases (8 × 7)
		TArray<float> BaseMask;
		BaseMask.Reserve(DE_MAX_BASES);
		for (int32 i = 0; i < DE_MAX_BASES; ++i)
		{
			if (i < BaseTokens.Num())
			{
				for (float F : BaseTokens[i].Features) Out.Add(F);
				for (int32 j = BaseTokens[i].Features.Num(); j < DE_BASE_DIM; ++j) Out.Add(0.0f);
				BaseMask.Add(0.0f);
			}
			else
			{
				for (int32 j = 0; j < DE_BASE_DIM; ++j) Out.Add(0.0f);
				BaseMask.Add(1.0f);
			}
		}

		// Masks (8+8+8)
		Out.Append(AllyMask);
		Out.Append(EnemyMask);
		Out.Append(BaseMask);

		// Class one-hot (3): [strike, vanguard, support]
		Out.Add(CommandedClass == EDEClassType::Strike ? 1.0f : 0.0f);
		Out.Add(CommandedClass == EDEClassType::Vanguard  ? 1.0f : 0.0f);
		Out.Add(CommandedClass == EDEClassType::Support ? 1.0f : 0.0f);

		ensure(Out.Num() == DE_OBS_V2_DIM);
		return Out;
	}

	/**
	 * Returns named per-entity-type arrays for gRPC / dict observation protocols.
	 * Keys: "self", "allies", "enemies", "bases", "ally_mask", "enemy_mask", "base_mask"
	 */
	TMap<FString, TArray<float>> ToDict() const
	{
		TMap<FString, TArray<float>> Dict;

		// Self
		TArray<float> SelfArr = SelfToken.Features;
		SelfArr.SetNum(DE_SELF_DIM, EAllowShrinking::No);
		Dict.Add(TEXT("self"), SelfArr);

		// Ally flat + mask
		TArray<float> AllyFlat, AllyMask;
		for (int32 i = 0; i < DE_MAX_ALLIES; ++i)
		{
			if (i < AllyTokens.Num())
			{
				TArray<float> Tok = AllyTokens[i].Features;
				Tok.SetNum(DE_ALLY_DIM, EAllowShrinking::No);
				AllyFlat.Append(Tok);
				AllyMask.Add(0.0f);
			}
			else
			{
				for (int32 j = 0; j < DE_ALLY_DIM; ++j) AllyFlat.Add(0.0f);
				AllyMask.Add(1.0f);
			}
		}
		Dict.Add(TEXT("allies"),    AllyFlat);
		Dict.Add(TEXT("ally_mask"), AllyMask);

		// Enemy flat + mask
		TArray<float> EnemyFlat, EnemyMask;
		for (int32 i = 0; i < DE_MAX_ENEMIES; ++i)
		{
			if (i < EnemyTokens.Num())
			{
				TArray<float> Tok = EnemyTokens[i].Features;
				Tok.SetNum(DE_ENEMY_DIM, EAllowShrinking::No);
				EnemyFlat.Append(Tok);
				EnemyMask.Add(0.0f);
			}
			else
			{
				for (int32 j = 0; j < DE_ENEMY_DIM; ++j) EnemyFlat.Add(0.0f);
				EnemyMask.Add(1.0f);
			}
		}
		Dict.Add(TEXT("enemies"),    EnemyFlat);
		Dict.Add(TEXT("enemy_mask"), EnemyMask);

		// Base flat + mask
		TArray<float> BaseFlat, BaseMask;
		for (int32 i = 0; i < DE_MAX_BASES; ++i)
		{
			if (i < BaseTokens.Num())
			{
				TArray<float> Tok = BaseTokens[i].Features;
				Tok.SetNum(DE_BASE_DIM, EAllowShrinking::No);
				BaseFlat.Append(Tok);
				BaseMask.Add(0.0f);
			}
			else
			{
				for (int32 j = 0; j < DE_BASE_DIM; ++j) BaseFlat.Add(0.0f);
				BaseMask.Add(1.0f);
			}
		}
		Dict.Add(TEXT("bases"),    BaseFlat);
		Dict.Add(TEXT("base_mask"), BaseMask);

		return Dict;
	}
};
