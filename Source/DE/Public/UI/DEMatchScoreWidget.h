// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DEMatchScoreWidget.generated.h"

class UTextBlock;

/**
 * UDEMatchScoreWidget
 *
 * Screen-space widget attached to ADEMatchManager's widget component.
 * Displays team names (in team color) and live scores for both teams.
 *
 * Usage:
 *  - Create a Widget Blueprint subclass of this class in the editor.
 *  - Add UTextBlock widgets named exactly: Team0NameText, Team0ScoreText,
 *    Team1NameText, Team1ScoreText — they will be auto-bound.
 *  - Assign the WBP class to ADEMatchManager::ScoreWidgetClass.
 */
UCLASS(Abstract, Blueprintable)
class DE_API UDEMatchScoreWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ── Bind these in your Widget Blueprint ──────────────────────────────────

	/** Team 0 (Red) name label */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> Team0NameText;

	/** Team 0 (Red) score label */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> Team0ScoreText;

	/** Team 1 (Blue) name label */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> Team1NameText;

	/** Team 1 (Blue) score label */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> Team1ScoreText;

	// ── Called by ADEMatchManager ─────────────────────────────────────────────

	/** Set display name and color for a team (call once during setup) */
	UFUNCTION(BlueprintCallable, Category = "DEMatchScoreWidget")
	void SetTeamInfo(int32 TeamID, const FString& TeamName, FLinearColor TeamColor);

	/** Update the score display for a team */
	UFUNCTION(BlueprintCallable, Category = "DEMatchScoreWidget")
	void UpdateScore(int32 TeamID, int32 NewScore);
};
