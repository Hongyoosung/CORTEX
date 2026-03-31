#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/DEClassTypes.h"
#include "Data/DEClassData.h"
#include "GameFramework/Character.h"
#include "DETeamData.generated.h"


/**
 * UDETeamData - Team-Level Configuration Data Asset
 *
 * Purpose:
 * Centralizes team identity, fallback visuals, and per-class role data.
 * Per-class configuration (mesh/anim/stats) lives in UDEClassData assets
 * referenced here, making each class independently browsable and reusable.
 *
 * Usage:
 * 1. Create Asset: Right-click → Miscellaneous → Data Asset → DETeamData
 * 2. Set team identity (name, color, default character class, fallback mesh/anim).
 * 3. Assign a UDEClassData asset to each class slot.
 * 4. Assign to DEMatchManager::TeamConfigs.
 *
 * Fallback Chain (per field):
 *   UDEClassData field (non-zero/non-null)
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
	 * Default character class to spawn when the assigned class's
	 * UDEClassData::CharacterClass is null.
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

	/** Fallback skeletal mesh when a class's UDEClassData::SkeletalMesh is null */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual Appearance")
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

	/** Fallback animation Blueprint when a class's UDEClassData::AnimationBlueprint is null */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual Appearance")
	TSubclassOf<UAnimInstance> AnimationBlueprint = nullptr;


	//========================================
	// Per-Class Data Assets
	//========================================

	/** Configuration applied when an agent is assigned the Strike class. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Data")
	TObjectPtr<UDEClassData> StrikeData;

	/** Configuration applied when an agent is assigned the Vanguard class. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Data")
	TObjectPtr<UDEClassData> VanguardData;

	/** Configuration applied when an agent is assigned the Support class. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Class Data")
	TObjectPtr<UDEClassData> SupportData;

	/**
	 * Returns the UDEClassData for the given class type, or null if unset.
	 * Callers should apply team-level fallbacks for any null field on the returned asset.
	 */
	UDEClassData* GetClassData(EDEClassType Class) const
	{
		switch (Class)
		{
		case EDEClassType::Vanguard:  return VanguardData.Get();
		case EDEClassType::Support: return SupportData.Get();
		default:                       return StrikeData.Get();
		}
	}

	/**
	 * Resolve the character class to spawn for the given class.
	 * Returns the class-specific class if set; falls back to the team-level default.
	 */
	TSubclassOf<ACharacter> ResolveCharacterClass(EDEClassType Class) const
	{
		if (UDEClassData* SD = GetClassData(Class))
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
