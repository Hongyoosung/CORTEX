// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/DEStrategyTypes.h"
#include "DEObservationTypes.generated.h"

// ============================================================
// Entity-Centric V2 constants (NNE fixed-shape padding limits)
// ============================================================
static constexpr int32 DE_MAX_ALLIES  = 8;
static constexpr int32 DE_MAX_ENEMIES = 8;
static constexpr int32 DE_MAX_BASES   = 8;

// Token dims: Self=7, Ally=5, Enemy=5, Base=7
static constexpr int32 DE_SELF_DIM    = 7;
static constexpr int32 DE_ALLY_DIM    = 5;
static constexpr int32 DE_ENEMY_DIM   = 5;
static constexpr int32 DE_BASE_DIM    = 7;

// Total padded flat size: 7 + 8*5 + 8*5 + 8*7 + 8+8+8 = 167
static constexpr int32 DE_OBS_V2_DIM  = DE_SELF_DIM
                                       + DE_MAX_ALLIES  * DE_ALLY_DIM
                                       + DE_MAX_ENEMIES * DE_ENEMY_DIM
                                       + DE_MAX_BASES   * DE_BASE_DIM
                                       + DE_MAX_ALLIES + DE_MAX_ENEMIES + DE_MAX_BASES;

/**
 * Individual Agent Observation (48-dim base)
 *
 * Layout:
 *   Self      (7):  pos/7500(3), health(1), vel/600(3)
 *   Allies   (16):  4 × [rel_pos/8000(3), health(1)]
 *   Enemies  (20):  5 × [rel_pos/8000_if_visible(3), visible(1)]
 *   Map       (5):  capture_point_status×5 (+1=friendly, 0=neutral, -1=enemy)
 *
 * Note: WeaponCooldown is kept in struct for gameplay but excluded from ToArray().
 *
 * Positions are agent-relative (ally/enemy pos minus self pos) so the same
 * tactical geometry always produces the same feature values regardless of
 * map location.  Self position is environment-relative and map-normalized,
 * ensuring consistent features across parallel environments.
 *
 * CurrentStrategy and bIsAlive are kept in the struct for reward computation
 * but are NOT included in ToArray() — strategy is appended as a one-hot by
 * DETacticalObserver.
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FDEObservation
{
	GENERATED_BODY()

	//========================================
	// Self State
	//========================================

	/** Agent world position (used in ToArray + reward computation) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	FVector Position = FVector::ZeroVector;

	/** Environment origin — subtracted from Position in ToArray() so that
	 *  self-position features are environment-relative, not absolute world coords.
	 *  Must be set by the observation gatherer (DETrainer / DETacticalObserver). */
	FVector EnvironmentOrigin = FVector::ZeroVector;

	/** Agent health [0.0-1.0] */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	float Health = 1.0f;

	/** Agent velocity (cm/s) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	FVector Velocity = FVector::ZeroVector;

	/** Weapon cooldown progress [0.0-1.0] */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	float WeaponCooldown = 0.0f;

	/** Current commanded strategy — kept for reward caching, NOT in ToArray */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	EDEStrategyType CurrentStrategy = EDEStrategyType::Assault;

	/** Alive flag — kept for death reward computation, NOT in ToArray */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Self")
	bool bIsAlive = true;

	//========================================
	// Allies State (4 agents × 4-dim = 16-dim)
	//========================================

	/** Ally world positions (relative offset computed in ToArray) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Allies")
	TArray<FVector> AllyPositions;

	/** Ally health values [0.0-1.0] */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Allies")
	TArray<float> AllyHealths;

	//========================================
	// Enemies State (5 agents × 4-dim = 20-dim)
	//========================================

	/** Enemy world positions — set to ZeroVector when not visible */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Enemies")
	TArray<FVector> EnemyPositions;

	/** Enemy line-of-sight flags */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Enemies")
	TArray<bool> EnemyVisible;

	//========================================
	// Map State (5-dim)
	//========================================

	/** Per-point ownership relative to this agent's team (5-dim)
	 *  +1.0 = owned by my team, 0.0 = neutral, -1.0 = owned by enemy
	 *  Order: PointA, PointB, PointC, PointD, PointE */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Map")
	TArray<float> CapturePointStatuses;

	/** World positions of the 5 capture points (Phase 0 addition) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Map")
	TArray<FVector> CapturePointPositions;

	/** Capture progress per point [0.0–1.0] (Phase 0 addition) */
	UPROPERTY(BlueprintReadWrite, Category = "Observation|Map")
	TArray<float> CaptureProgress;

	//========================================
	// Utility Methods
	//========================================

	/**
	 * Convert to flat float array for neural network input (48-dim).
	 * Ally and enemy positions are expressed relative to self position
	 * and normalized by vision range (8000 cm) so spatial relationships
	 * are map-position invariant.
	 */
	TArray<float> ToArray() const
	{
		TArray<float> Result;
		Result.Reserve(48);

		// Self state (7-dim) — WeaponCooldown excluded (not useful for NN)
		// Position: environment-relative, map-normalized (150m map = 15000cm half-width = 7500)
		const FVector RelativePos = Position - EnvironmentOrigin;
		Result.Add(RelativePos.X / 7500.0f);
		Result.Add(RelativePos.Y / 7500.0f);
		Result.Add(RelativePos.Z / 1000.0f);
		Result.Add(Health);
		// Velocity: normalized by max walk speed (600 cm/s)
		Result.Add(Velocity.X / 600.0f);
		Result.Add(Velocity.Y / 600.0f);
		Result.Add(Velocity.Z / 600.0f);
		// CurrentStrategy and bIsAlive intentionally excluded:
		//   Strategy → appended as one-hot by DETacticalObserver
		//   bIsAlive  → always 1 when a real observation is gathered

		// Allies (16-dim: 4 agents × 4)
		// Relative position normalized by vision range (8000 cm)
		for (int32 i = 0; i < 4; ++i)
		{
			if (i < AllyPositions.Num())
			{
				const FVector RelPos = (AllyPositions[i] - Position) / 8000.0f;
				Result.Add(RelPos.X);
				Result.Add(RelPos.Y);
				Result.Add(RelPos.Z);
			}
			else
			{
				Result.Add(0.0f);
				Result.Add(0.0f);
				Result.Add(0.0f);
			}
			Result.Add(i < AllyHealths.Num() ? AllyHealths[i] : 0.0f);
		}

		// Enemies (20-dim: 5 agents × 4)
		// Position is relative and only exposed when the enemy is visible;
		// non-visible slots output (0,0,0) to avoid stale absolute coordinates.
		for (int32 i = 0; i < 5; ++i)
		{
			const bool bVis = (i < EnemyVisible.Num()) && EnemyVisible[i];
			if (bVis && i < EnemyPositions.Num())
			{
				const FVector RelPos = (EnemyPositions[i] - Position) / 8000.0f;
				Result.Add(RelPos.X);
				Result.Add(RelPos.Y);
				Result.Add(RelPos.Z);
			}
			else
			{
				Result.Add(0.0f);
				Result.Add(0.0f);
				Result.Add(0.0f);
			}
			Result.Add(bVis ? 1.0f : 0.0f);
		}

		// Map state (5-dim): per-point ownership
		for (int32 i = 0; i < 5; ++i)
		{
			Result.Add(i < CapturePointStatuses.Num() ? CapturePointStatuses[i] : 0.0f);
		}

		ensure(Result.Num() == 48);
		return Result;
	}

	/** Default constructor */
	FDEObservation()
	{
		AllyPositions.Init(FVector::ZeroVector, 4);
		AllyHealths.Init(1.0f, 4);
		EnemyPositions.Init(FVector::ZeroVector, 5);
		EnemyVisible.Init(false, 5);
		CapturePointStatuses.Init(0.0f, 5);
		CapturePointPositions.Init(FVector::ZeroVector, 5);
		CaptureProgress.Init(0.0f, 5);
	}
};


// ============================================================
// Phase 1 — Entity-Centric Observation V2
// ============================================================

/**
 * Single entity token — flat float array of fixed dimension per entity type.
 * Ally=5-dim, Enemy=5-dim, Base=7-dim (see plan Section 2.2).
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FDEEntityToken
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
 * padded flat array (167-dim) for NNE / ONNX compatibility.
 *
 * Self (7):  [pos_x/7500, pos_y/7500, pos_z/1000, health, vel_x/600, vel_y/600, vel_z/600]
 * Ally (5):  [rel_pos_x/8000, rel_pos_y/8000, rel_pos_z/8000, health, alive]
 * Enemy (5): [rel_pos_x/8000, rel_pos_y/8000, rel_pos_z/8000, visible, confidence]
 * Base (7):  [rel_pos_x/15000, rel_pos_y/15000, rel_pos_z/1000, ownership,
 *             capture_progress, is_assigned_target, strategic_value]
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FDEObservationV2
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

	/**
	 * Serialise to padded flat array (167-dim) for NNE/ONNX inference.
	 *
	 * Layout (total = 167):
	 *  [0..6]   Self (7)
	 *  [7..46]  Allies  padded to 8 × 5  (40)
	 *  [47..86] Enemies padded to 8 × 5  (40)
	 *  [87..142] Bases  padded to 8 × 7  (56)
	 *  [143..150] Ally mask  (8) — 0=present, 1=padding
	 *  [151..158] Enemy mask (8)
	 *  [159..166] Base mask  (8)
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
