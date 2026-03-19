// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/DESpectatorHUD.h"
#include "Player/DESpectatorController.h"
#include "Characters/DECharacter.h"
#include "GAS/Abilities/DEGA_Attack.h"
#include "Types/DEStrategyTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Engine.h"

// ---------------------------------------------------------------------------
// DrawHUD — entry point called every frame
// ---------------------------------------------------------------------------

void ADESpectatorHUD::DrawHUD()
{
	Super::DrawHUD();

	ADECharacter* Agent = GetObservedCharacter();
	if (!Agent || !Canvas) return;

	// ---- Gather data ----
	const EDEStrategyType Strategy = Agent->GetCommandedStrategy();
	const FDEEQSWeightParameters Weights = Agent->GetEQSWeights();

	float HealthPct   = Agent->GetHealthPercentage();
	float ManaPct     = Agent->GetManaPercentage();
	int32 CurrentAmmo = 0;
	int32 MaxAmmo     = 0;

	if (UDEGA_Attack* Attack = Agent->GetAttackAbility())
	{
		CurrentAmmo = Attack->GetCurrentAmmo();
		const float AmmoPct = Attack->GetAmmoPercentage();
		MaxAmmo = (AmmoPct > 0.0f) ? FMath::RoundToInt(static_cast<float>(CurrentAmmo) / AmmoPct) : 0;
	}

	// ---- Strategy label (top-left, large) ----
	FString StrategyName;
	FLinearColor StrategyColor;

	switch (Strategy)
	{
	case EDEStrategyType::Assault:
		StrategyName  = TEXT("ASSAULT");
		StrategyColor = FLinearColor(1.0f, 0.15f, 0.05f, 1.0f);
		break;
	case EDEStrategyType::Defend:
		StrategyName  = TEXT("DEFEND");
		StrategyColor = FLinearColor(0.1f, 0.4f, 1.0f, 1.0f);
		break;
	case EDEStrategyType::Support:
		StrategyName  = TEXT("SUPPORT");
		StrategyColor = FLinearColor(0.45f, 1.0f, 0.45f, 1.0f);
		break;
	default:
		StrategyName  = TEXT("UNKNOWN");
		StrategyColor = FLinearColor::White;
		break;
	}

	float StatusTopY = 0.0f;
	DrawStrategyLabel(StrategyName, StrategyColor, StatusTopY);
	DrawStatusBars(HealthPct, CurrentAmmo, MaxAmmo, ManaPct, StatusTopY);
	DrawEQSWeights(Weights);
}

// ---------------------------------------------------------------------------
// Strategy Label
// ---------------------------------------------------------------------------

void ADESpectatorHUD::DrawStrategyLabel(const FString& StrategyName, const FLinearColor& Color, float& OutBottomY)
{
	const float MarginX = 24.0f;
	const float MarginY = 24.0f;
	const float FontScale = 3.0f;

	FCanvasTextItem TextItem(
		FVector2D(MarginX, MarginY),
		FText::FromString(StrategyName),
		GEngine->GetLargeFont(),
		Color
	);
	TextItem.Scale    = FVector2D(FontScale, FontScale);
	TextItem.bOutlined = true;
	TextItem.OutlineColor = FLinearColor::Black;
	Canvas->DrawItem(TextItem);

	float TextW = 0.0f, TextH = 0.0f;
	GetTextSize(StrategyName, TextW, TextH, GEngine->GetLargeFont(), FontScale);
	OutBottomY = MarginY + TextH + 8.0f;
}

// ---------------------------------------------------------------------------
// Health & Ammo Bars
// ---------------------------------------------------------------------------

void ADESpectatorHUD::DrawStatusBars(float HealthPct, int32 CurrentAmmo, int32 MaxAmmo, float ManaPct, float TopY)
{
	const float MarginX    = 24.0f;
	const float BarWidth   = 220.0f;
	const float BarHeight  = 18.0f;
	const float RowSpacing = 28.0f;

	const FString HealthLabel = FString::Printf(TEXT("HP  %d%%"), FMath::RoundToInt(HealthPct * 100.0f));
	DrawBar(HealthLabel, HealthPct, FLinearColor(0.05f, 0.85f, 0.1f, 0.9f), MarginX, TopY, BarWidth, BarHeight);

	const float AmmoPct = (MaxAmmo > 0) ? FMath::Clamp(static_cast<float>(CurrentAmmo) / static_cast<float>(MaxAmmo), 0.0f, 1.0f) : 0.0f;
	const FString AmmoLabel = FString::Printf(TEXT("AMM %d / %d"), CurrentAmmo, MaxAmmo);
	DrawBar(AmmoLabel, AmmoPct, FLinearColor(0.9f, 0.75f, 0.1f, 0.9f), MarginX, TopY + RowSpacing, BarWidth, BarHeight);

	const FString ManaLabel = FString::Printf(TEXT("MP  %d%%"), FMath::RoundToInt(ManaPct * 100.0f));
	DrawBar(ManaLabel, ManaPct, FLinearColor(0.2f, 0.5f, 1.0f, 0.9f), MarginX, TopY + RowSpacing * 2.0f, BarWidth, BarHeight);
}

void ADESpectatorHUD::DrawBar(const FString& Label, float Fraction, const FLinearColor& BarColor, float X, float Y, float Width, float Height)
{
	FCanvasTileItem BgTile(FVector2D(X, Y), FVector2D(Width, Height), FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	BgTile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(BgTile);

	const float FillW = FMath::Clamp(Fraction, 0.0f, 1.0f) * Width;
	if (FillW > 0.0f)
	{
		FCanvasTileItem FillTile(FVector2D(X, Y), FVector2D(FillW, Height), BarColor);
		FillTile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(FillTile);
	}

	FCanvasTextItem TextItem(
		FVector2D(X + 4.0f, Y + 1.0f),
		FText::FromString(Label),
		GEngine->GetSmallFont(),
		FLinearColor::White
	);
	TextItem.Scale     = FVector2D(1.1f, 1.1f);
	TextItem.bOutlined  = true;
	TextItem.OutlineColor = FLinearColor::Black;
	Canvas->DrawItem(TextItem);
}

// ---------------------------------------------------------------------------
// EQS Weights (bottom-left)
// ---------------------------------------------------------------------------

void ADESpectatorHUD::DrawEQSWeights(const FDEEQSWeightParameters& Weights)
{
	const float MarginX   = 24.0f;
	const float MarginY   = 24.0f;
	const float RowHeight = 20.0f;

	const int32 NumRows = 7;
	const float StartY = Canvas->SizeY - MarginY - NumRows * RowHeight - 20.0f;

	FCanvasTextItem Header(
		FVector2D(MarginX, StartY),
		FText::FromString(TEXT("-- EQS Weights --")),
		GEngine->GetSmallFont(),
		FLinearColor(0.7f, 0.7f, 0.7f, 1.0f)
	);
	Header.Scale = FVector2D(1.0f, 1.0f);
	Canvas->DrawItem(Header);

	DrawWeightRow(TEXT("EnemyObj"), Weights.EnemyObjectiveProximity, MarginX, StartY + RowHeight * 1.0f);
	DrawWeightRow(TEXT("AllyObj "), Weights.AllyObjectiveProximity,  MarginX, StartY + RowHeight * 2.0f);
	DrawWeightRow(TEXT("Cover   "), Weights.CoverDensity,            MarginX, StartY + RowHeight * 3.0f);
	DrawWeightRow(TEXT("Visibil "), Weights.EnemyVisibility,         MarginX, StartY + RowHeight * 4.0f);
	DrawWeightRow(TEXT("AllyProx"), Weights.AllyProximity,           MarginX, StartY + RowHeight * 5.0f);
	DrawWeightRow(TEXT("Range   "), Weights.CombatRange,             MarginX, StartY + RowHeight * 6.0f);
	DrawWeightRow(TEXT("AssBase "), Weights.AssignedBaseProximity,   MarginX, StartY + RowHeight * 7.0f);
}

void ADESpectatorHUD::DrawWeightRow(const FString& Label, float Value, float X, float Y)
{
	FLinearColor TextColor;
	if (Value > 0.05f)       TextColor = FLinearColor(0.3f, 1.0f, 0.3f, 1.0f);
	else if (Value < -0.05f) TextColor = FLinearColor(1.0f, 0.35f, 0.35f, 1.0f);
	else                     TextColor = FLinearColor(0.75f, 0.75f, 0.75f, 1.0f);

	const FString Row = FString::Printf(TEXT("%s  %+.3f"), *Label, Value);

	FCanvasTextItem TextItem(
		FVector2D(X, Y),
		FText::FromString(Row),
		GEngine->GetSmallFont(),
		TextColor
	);
	TextItem.Scale     = FVector2D(1.05f, 1.05f);
	TextItem.bOutlined  = true;
	TextItem.OutlineColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.8f);
	Canvas->DrawItem(TextItem);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

ADECharacter* ADESpectatorHUD::GetObservedCharacter() const
{
	if (ADESpectatorController* SC = Cast<ADESpectatorController>(PlayerOwner))
	{
		return SC->GetObservedCharacter();
	}
	return nullptr;
}
