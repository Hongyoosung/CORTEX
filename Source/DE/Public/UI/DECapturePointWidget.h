// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DECapturePointWidget.generated.h"

class UTextBlock;
class UProgressBar;

/**
 * UDECapturePointWidget
 *
 * Screen-space widget attached to ADECapturePoint's widget component.
 * Displays the point name, current owner, and capture progress.
 *
 * Usage:
 *  - Create a Widget Blueprint subclass of this class in the editor.
 *  - Add widgets named: PointNameText, OwnerText, CaptureProgressBar,
 *    CaptureProgressText — they will be auto-bound.
 *  - Assign the WBP class to ADECapturePoint::CaptureWidgetClass.
 */
UCLASS(Abstract, Blueprintable)
class DE_API UDECapturePointWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ── Bind these in your Widget Blueprint ──────────────────────────────────

	/** Display name of the capture point (e.g. "Point C - Center Plaza") */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> PointNameText;

	/** Current owner: "NEUTRAL", "TEAM 0", or "TEAM 1" */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> OwnerText;

	/** Capture progress bar (0–1) filled in the capturing team's color */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UProgressBar> CaptureProgressBar;

	/** Capture progress percentage text (e.g. "75%") */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	TObjectPtr<UTextBlock> CaptureProgressText;

	// ── Called by ADECapturePoint ─────────────────────────────────────────────

	/**
	 * Refresh all widget elements.
	 * @param PointName    Human-readable point name
	 * @param OwnerTeamID  Current owner (-1 = neutral)
	 * @param Progress     Capture progress in [0, 1]
	 * @param TeamColor    Color of the owner/capturing team (white if neutral)
	 */
	UFUNCTION(BlueprintCallable, Category = "DECapturePointWidget")
	void UpdateWidget(const FString& PointName, int32 OwnerTeamID, float Progress, FLinearColor TeamColor);
};
