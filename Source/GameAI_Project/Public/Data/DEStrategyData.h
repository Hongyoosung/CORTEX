#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DEStrategyData.generated.h"

class UDEAbilityData;

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
	// Ability Data Override
	//========================================

	/**
	 * Per-strategy ability configuration (attack stats, heal stats, mana).
	 * When set, fully replaces the character's base UDEAbilityData for this strategy.
	 * null = use the character's default AbilityData (set on BP_Agent).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TObjectPtr<UDEAbilityData> AbilityDataOverride = nullptr;


};
