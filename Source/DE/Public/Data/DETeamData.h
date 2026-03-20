#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/DEStrategyTypes.h"
#include "Data/DEStrategyData.h"
#include "GameFramework/Character.h"
#include "DETeamData.generated.h"


/**
 * UDETeamData - Team-Level Configuration Data Asset
 *
 * Purpose:
 * Centralizes team identity, fallback visuals, and per-strategy role data.
 * Per-strategy configuration (mesh/anim/stats) lives in UDEStrategyData assets
 * referenced here, making each strategy independently browsable and reusable.
 *
 * Usage:
 * 1. Create Asset: Right-click → Miscellaneous → Data Asset → DETeamData
 * 2. Set team identity (name, color, default character class, fallback mesh/anim).
 * 3. Assign a UDEStrategyData asset to each strategy slot.
 * 4. Assign to DEMatchManager::TeamConfigs.
 *
 * Fallback Chain (per field):
 *   UDEStrategyData field (non-zero/non-null)
 *     → UDETeamData team-level field
 *       → Blueprint / AbilityData default
 */
UCLASS(BlueprintType)
class DE_API UDETeamData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	//========================================
	// Team Identity
	//========================================

	/**
	 * Default character class to spawn when the assigned strategy's
	 * UDEStrategyData::CharacterClass is null.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent")
	TSubclassOf<ACharacter> CharacterClass;

	/** Team display name (e.g., "Red Team", "Blue Team") */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Team Identity")
	FString TeamName = TEXT("Team");

	/** Team primary color (used for VFX, UI, debugging) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Team Identity")
	FLinearColor TeamColor = FLinearColor::White;


	//========================================
	// Visual Appearance (Team-Level Fallbacks)
	//========================================

	/** Fallback skeletal mesh when a strategy's UDEStrategyData::SkeletalMesh is null */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual Appearance")
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

	/** Fallback animation Blueprint when a strategy's UDEStrategyData::AnimationBlueprint is null */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual Appearance")
	TSubclassOf<UAnimInstance> AnimationBlueprint = nullptr;


	//========================================
	// Per-Strategy Data Assets
	//========================================

	/** Configuration applied when an agent is assigned the Assault strategy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy Data")
	TObjectPtr<UDEStrategyData> AssaultData;

	/** Configuration applied when an agent is assigned the Defend strategy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy Data")
	TObjectPtr<UDEStrategyData> DefendData;

	/** Configuration applied when an agent is assigned the Support strategy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strategy Data")
	TObjectPtr<UDEStrategyData> SupportData;

	/**
	 * Returns the UDEStrategyData for the given strategy type, or null if unset.
	 * Callers should apply team-level fallbacks for any null field on the returned asset.
	 */
	UDEStrategyData* GetStrategyData(EDEStrategyType Strategy) const
	{
		switch (Strategy)
		{
		case EDEStrategyType::Defend:  return DefendData.Get();
		case EDEStrategyType::Support: return SupportData.Get();
		default:                       return AssaultData.Get();
		}
	}

	/**
	 * Resolve the character class to spawn for the given strategy.
	 * Returns the strategy-specific class if set; falls back to the team-level default.
	 */
	TSubclassOf<ACharacter> ResolveCharacterClass(EDEStrategyType Strategy) const
	{
		if (UDEStrategyData* SD = GetStrategyData(Strategy))
		{
			if (SD->CharacterClass) { return SD->CharacterClass; }
		}
		return CharacterClass;
	}


	//========================================
	// VFX & Audio (Future Expansion)
	//========================================

	/** Spawn VFX (particle system spawned when agent spawns) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<UParticleSystem> SpawnVFX = nullptr;

	/** Death VFX (particle system spawned when agent dies) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<UParticleSystem> DeathVFX = nullptr;

	/** Spawn sound (audio cue played when agent spawns) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<USoundBase> SpawnSound = nullptr;


	//========================================
	// Helper Functions
	//========================================

	/** Get human-readable description for debugging */
	UFUNCTION(BlueprintPure, Category = "Team Data")
	FString GetDescription() const
	{
		return FString::Printf(TEXT("%s (Mesh: %s, AnimBP: %s)"),
			*TeamName,
			SkeletalMesh ? *SkeletalMesh->GetName() : TEXT("None"),
			AnimationBlueprint ? *AnimationBlueprint->GetName() : TEXT("None"));
	}

	/** Check if team data is valid (requires a fallback mesh and character class) */
	UFUNCTION(BlueprintPure, Category = "Team Data")
	bool IsValid() const
	{
		return SkeletalMesh != nullptr && CharacterClass != nullptr;
	}


#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);

		if (SkeletalMesh == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("TeamData '%s': SkeletalMesh is not set!"), *GetName());
		}

		if (AnimationBlueprint == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("TeamData '%s': AnimationBlueprint is not set!"), *GetName());
		}
	}
#endif
};
