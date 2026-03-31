// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/DESpectatorOverlayWidget.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"

// ── Helpers ──────────────────────────────────────────────────────────────────

static void SetWeightText(UTextBlock* Block, const FString& Label, float Value)
{
	if (!Block) return;

	FLinearColor Color;
	if      (Value >  0.05f) Color = FLinearColor(0.3f, 1.0f, 0.3f, 1.0f);
	else if (Value < -0.05f) Color = FLinearColor(1.0f, 0.35f, 0.35f, 1.0f);
	else                     Color = FLinearColor(0.75f, 0.75f, 0.75f, 1.0f);

	Block->SetText(FText::FromString(FString::Printf(TEXT("%s  %+.3f"), *Label, Value)));
	Block->SetColorAndOpacity(FSlateColor(Color));
}

// ── Public API ────────────────────────────────────────────────────────────────

void UDESpectatorOverlayWidget::UpdateAgentInfo(const FString& TeamName, FLinearColor TeamColor,
                                                  const FString& ClassName, FLinearColor ClassColor)
{
	if (TeamNameText)
	{
		TeamNameText->SetText(FText::FromString(TeamName));
		TeamNameText->SetColorAndOpacity(FSlateColor(TeamColor));
	}
	if (ClassNameText)
	{
		ClassNameText->SetText(FText::FromString(ClassName));
		ClassNameText->SetColorAndOpacity(FSlateColor(ClassColor));
	}
}

void UDESpectatorOverlayWidget::UpdateStatusBars(float HealthPct, int32 CurrentAmmo, int32 MaxAmmo, float ManaPct)
{
	if (HealthBar)  HealthBar->SetPercent(HealthPct);
	if (HealthText) HealthText->SetText(FText::FromString(
		FString::Printf(TEXT("HP %d%%"), FMath::RoundToInt(HealthPct * 100.0f))));

	const float AmmoPct = (MaxAmmo > 0)
		? FMath::Clamp(static_cast<float>(CurrentAmmo) / static_cast<float>(MaxAmmo), 0.0f, 1.0f)
		: 0.0f;
	if (AmmoBar)  AmmoBar->SetPercent(AmmoPct);
	if (AmmoText) AmmoText->SetText(FText::FromString(
		FString::Printf(TEXT("AMM %d / %d"), CurrentAmmo, MaxAmmo)));

	if (ManaBar)  ManaBar->SetPercent(ManaPct);
	if (ManaText) ManaText->SetText(FText::FromString(
		FString::Printf(TEXT("MP %d%%"), FMath::RoundToInt(ManaPct * 100.0f))));
}

void UDESpectatorOverlayWidget::UpdateEQSWeights(const FDEEQSWeightParameters& Weights)
{
	SetWeightText(EQS_EnemyObjective, TEXT("EnemyObj"), Weights.EnemyObjectiveProximity);
	SetWeightText(EQS_AllyObjective,  TEXT("AllyObj "), Weights.AllyObjectiveProximity);
	SetWeightText(EQS_Cover,          TEXT("Cover   "), Weights.CoverDensity);
	SetWeightText(EQS_Visibility,     TEXT("Visibil "), Weights.EnemyVisibility);
	SetWeightText(EQS_AllyProximity,  TEXT("AllyProx"), Weights.AllyProximity);
	SetWeightText(EQS_CombatRange,    TEXT("Range   "), Weights.CombatRange);
	SetWeightText(EQS_AssignedBase,   TEXT("AssBase "), Weights.AssignedBaseProximity);
}
