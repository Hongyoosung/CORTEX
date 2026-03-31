// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/DEEQSTypes.h"
#include "DESpectatorOverlayWidget.generated.h"

class UTextBlock;
class UProgressBar;

/**
 * UDESpectatorOverlayWidget
 *
 * Full-screen HUD overlay displayed while a spectator observes an agent.
 * Replaces the legacy Canvas-based drawing in ADESpectatorHUD.
 *
 * Usage:
 *  - Create a Widget Blueprint subclass of this class in the editor.
 *  - Bind widgets by matching these exact names in your WBP layout.
 *  - Assign the WBP class to ADESpectatorHUD::OverlayWidgetClass.
 *
 * All UPROPERTY members use BindWidgetOptional so the WBP will not fail
 * to compile if some elements are omitted.
 */
UCLASS(Abstract, Blueprintable)
class DE_API UDESpectatorOverlayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ── Identity (top-left) ───────────────────────────────────────────────────

	/** Team name displayed in team color (e.g. "RED") */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> TeamNameText;

	/** Agent class name in class color (e.g. "VANGUARD") */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> ClassNameText;

	// ── Status bars ───────────────────────────────────────────────────────────

	/** Health progress bar (0–1) */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar;

	/** Health percentage label (e.g. "HP 87%") */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> HealthText;

	/** Ammo progress bar (0–1) */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UProgressBar> AmmoBar;

	/** Ammo count label (e.g. "AMM 24 / 30") */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> AmmoText;

	/** Mana progress bar (0–1) */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UProgressBar> ManaBar;

	/** Mana percentage label (e.g. "MP 50%") */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> ManaText;

	// ── EQS Weights (bottom-left) ─────────────────────────────────────────────

	/** Enemy objective proximity weight */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> EQS_EnemyObjective;

	/** Ally objective proximity weight */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> EQS_AllyObjective;

	/** Cover density weight */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> EQS_Cover;

	/** Enemy visibility weight */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> EQS_Visibility;

	/** Ally proximity weight */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> EQS_AllyProximity;

	/** Combat range weight */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> EQS_CombatRange;

	/** Assigned base proximity weight */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> EQS_AssignedBase;

	// ── Called by ADESpectatorHUD each frame ─────────────────────────────────

	/** Update team name and class label */
	UFUNCTION(BlueprintCallable, Category = "DESpectatorOverlayWidget")
	void UpdateAgentInfo(const FString& TeamName, FLinearColor TeamColor,
	                     const FString& ClassName, FLinearColor ClassColor);

	/** Update health, ammo, and mana bars */
	UFUNCTION(BlueprintCallable, Category = "DESpectatorOverlayWidget")
	void UpdateStatusBars(float HealthPct, int32 CurrentAmmo, int32 MaxAmmo, float ManaPct);

	/** Update all 7 EQS weight readouts */
	UFUNCTION(BlueprintCallable, Category = "DESpectatorOverlayWidget")
	void UpdateEQSWeights(const FDEEQSWeightParameters& Weights);
};
