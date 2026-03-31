// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/DECapturePointWidget.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"

void UDECapturePointWidget::UpdateWidget(const FString& PointName, int32 OwnerTeamID, float Progress, FLinearColor TeamColor)
{
	if (PointNameText)
	{
		PointNameText->SetText(FText::FromString(PointName));
	}

	if (OwnerText)
	{
		const FString OwnerStr = (OwnerTeamID == -1)
			? TEXT("NEUTRAL")
			: FString::Printf(TEXT("TEAM %d"), OwnerTeamID);
		OwnerText->SetText(FText::FromString(OwnerStr));
		OwnerText->SetColorAndOpacity(FSlateColor(OwnerTeamID == -1 ? FLinearColor::White : TeamColor));
	}

	if (CaptureProgressBar)
	{
		CaptureProgressBar->SetPercent(FMath::Clamp(Progress, 0.0f, 1.0f));
		CaptureProgressBar->SetFillColorAndOpacity(TeamColor);
	}

	if (CaptureProgressText)
	{
		CaptureProgressText->SetText(
			FText::FromString(FString::Printf(TEXT("%.0f%%"), FMath::Clamp(Progress, 0.0f, 1.0f) * 100.0f)));
	}
}
