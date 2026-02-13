
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TeamData.generated.h"

/**
 * UTeamData - Data Asset for Agent Appearance Configuration
 *
 * Purpose:
 * Centralizes all visual and animation configuration for agents in a reusable data asset.
 * Separates appearance data from logic, making it easy to create variants (e.g., different
 * faction styles, seasonal skins, etc.) without code changes.
 *
 * Usage:
 * 1. Create Data Asset: Right-click → Miscellaneous → Data Asset → TeamData
 * 2. Configure appearance: Set skeletal mesh, animation blueprint, materials, etc.
 * 3. Assign to TeamManager: Set in RedTeamAppearance or BlueTeamAppearance
 *
 * Example Assets:
 * - DA_RedTeamAppearance: Red team configuration (red materials, aggressive animations)
 * - DA_BlueTeamAppearance: Blue team configuration (blue materials, tactical animations)
 * - DA_EliteAppearance: Special appearance for elite agents
 *
 * MOC v10.2: Team appearance configuration is now data-driven
 */
UCLASS(BlueprintType)
class GAMEAI_PROJECT_API UTeamData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	//========================================
	// Team Identity
	//========================================

	/** Team display name (e.g., "Red Team", "Blue Team") */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Team Identity")
	FString TeamName = TEXT("Team");

	/** Team primary color (used for VFX, UI, debugging) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Team Identity")
	FLinearColor TeamColor = FLinearColor::White;


	//========================================
	// Visual Appearance
	//========================================

	/** Agent skeletal mesh (body geometry) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual Appearance")
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

	/** Animation Blueprint class (controls agent animations) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual Appearance")
	TSubclassOf<UAnimInstance> AnimationBlueprint = nullptr;


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
	UFUNCTION(BlueprintPure, Category = "Agent Appearance")
	FString GetDescription() const
	{
		return FString::Printf(TEXT("%s (Mesh: %s, AnimBP: %s)"),
			*TeamName,
			SkeletalMesh ? *SkeletalMesh->GetName() : TEXT("None"),
			AnimationBlueprint ? *AnimationBlueprint->GetName() : TEXT("None"));
	}

	/** Check if appearance data is valid */
	UFUNCTION(BlueprintPure, Category = "Agent Appearance")
	bool IsValid() const
	{
		return SkeletalMesh != nullptr;
	}



#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);

		// Validate data in editor
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
