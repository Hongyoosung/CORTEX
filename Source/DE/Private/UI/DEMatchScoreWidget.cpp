// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/DEMatchScoreWidget.h"
#include "Components/TextBlock.h"

void UDEMatchScoreWidget::SetTeamInfo(int32 TeamID, const FString& TeamName, FLinearColor TeamColor)
{
	UTextBlock* NameText = (TeamID == 0) ? Team0NameText.Get() : Team1NameText.Get();
	if (NameText)
	{
		NameText->SetText(FText::FromString(TeamName));
		NameText->SetColorAndOpacity(FSlateColor(TeamColor));
	}
}

void UDEMatchScoreWidget::UpdateScore(int32 TeamID, int32 NewScore)
{
	UTextBlock* ScoreText = (TeamID == 0) ? Team0ScoreText.Get() : Team1ScoreText.Get();
	if (ScoreText)
	{
		ScoreText->SetText(FText::AsNumber(NewScore));
	}
}
