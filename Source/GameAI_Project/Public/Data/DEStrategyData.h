#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DEStrategyData.generated.h"

class UAnimMontage;

/**
 * UDEStrategyData - Per-Strategy Configuration Data Asset
 *
 * Purpose:
 * Encapsulates all visual and stat configuration for a single strategy role
 * (Assault, Defend, or Support) as a standalone, reusable data asset.
 *
 * Fields left at their default (nullptr / 0) fall back to the team-level
 * setting in UDETeamData. This means Red Assault and Blue Assault can share
 * the same UDEStrategyData while only differing in team-level mesh/color.
 *
 * Usage:
 * 1. Create Asset: Right-click → Miscellaneous → Data Asset → DEStrategyData
 * 2. Configure per-strategy stats and optional visual overrides.
 * 3. Assign to UDETeamData::AssaultData / DefendData / SupportData.
 */
UCLASS(BlueprintType)
class GAMEAI_PROJECT_API UDEStrategyData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	//========================================
	// Spawn Override
	//========================================

	/**
	 * Character Blueprint class to spawn for this strategy.
	 * null = use UDETeamData::CharacterClass (team-level default).
	 * Set this only when a strategy needs a completely different Blueprint
	 * (e.g. different component layout or ability set).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TSubclassOf<ACharacter> CharacterClass = nullptr;


	//========================================
	// Visual Overrides
	//========================================

	/** Skeletal mesh for this strategy (null = use UDETeamData::SkeletalMesh) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

	/** Animation Blueprint for this strategy (null = use UDETeamData::AnimationBlueprint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TSubclassOf<UAnimInstance> AnimationBlueprint = nullptr;


	//========================================
	// Stat Overrides (0 = use blueprint/ability default)
	//========================================

	/** Max health override in HP (0 = blueprint default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "0"))
	float MaxHealth = 0.0f;

	/** Max walk speed override in cm/s (0 = blueprint default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "0"))
	float MaxWalkSpeed = 0.0f;


	//========================================
	// Attack Overrides (Assault / Defend)
	//========================================

	/** Minimum attack range override in cm (0 = AbilityData default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float MinAttackRange = 0.0f;

	/** Maximum attack range override in cm (0 = AbilityData default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float MaxAttackRange = 0.0f;

	/** Damage per hit override (0 = AbilityData default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float Damage = 0.0f;

	/** Attack speed override in shots/sec (0 = AbilityData default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float AttackSpeed = 0.0f;


	//========================================
	// Heal Overrides (Support)
	//========================================

	/** Maximum heal range override in cm (0 = AbilityData default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0"))
	float MaxHealRange = 0.0f;


	//========================================
	// Animation Montages
	//========================================

	/** Montage played when this unit fires (null = no montage) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> AttackMontage = nullptr;

	/** Montage played when this unit heals (null = no montage) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> HealMontage = nullptr;
};
