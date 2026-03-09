// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Types/DEEQSTypes.h"
#include "DESpectatorHUD.generated.h"

class ADESpectatorController;

class ADECharacter;

/**
 * ADESpectatorHUD
 *
 * Canvas-based HUD drawn while a spectator observes an agent via ADESpectatorController.
 *
 * Layout:
 *   Top-left    : Current strategy in large bold font (Assault=Red, Defend=Blue, Support=LightGreen)
 *   Bottom-left : Real-time EQS weight readout (6 named values)
 *   Top-left    : Health bar + percentage (below strategy text)
 *   Top-left    : Ammo bar + count (below health bar)
 *
 * No editor assets (Widget Blueprints) are required — everything is drawn with Canvas primitives.
 */
UCLASS()
class GAMEAI_PROJECT_API ADESpectatorHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	/** Draw the strategy name in large font at the top-left */
	void DrawStrategyLabel(const FString& StrategyName, const FLinearColor& Color, float& OutBottomY);

	/** Draw health and ammo bars below the strategy label */
	void DrawStatusBars(float HealthPct, int32 CurrentAmmo, int32 MaxAmmo, float TopY);

	/** Draw EQS weight values in the bottom-left corner */
	void DrawEQSWeights(const FDEEQSWeightParameters& Weights);

	/** Helper: draw a labelled progress bar */
	void DrawBar(const FString& Label, float Fraction, const FLinearColor& BarColor, float X, float Y, float Width, float Height);

	/** Helper: draw a labelled float row (used for EQS) */
	void DrawWeightRow(const FString& Label, float Value, float X, float Y);

	/** Get the currently observed DECharacter from the owning controller (null if none) */
	ADECharacter* GetObservedCharacter() const;
};
