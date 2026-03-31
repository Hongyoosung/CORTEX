// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/DESpectatorHUD.h"
#include "Player/DESpectatorController.h"
#include "Characters/DEAgent.h"
#include "GAS/Abilities/DEGA_Attack.h"
#include "Types/DEClassTypes.h"
#include "UI/DESpectatorOverlayWidget.h"
#include "Blueprint/UserWidget.h"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ADESpectatorHUD::BeginPlay()
{
	Super::BeginPlay();

	if (OverlayWidgetClass)
	{
		OverlayWidget = CreateWidget<UDESpectatorOverlayWidget>(GetWorld(), OverlayWidgetClass);
		if (OverlayWidget)
		{
			OverlayWidget->AddToViewport();
		}
	}
}

// ---------------------------------------------------------------------------
// DrawHUD — push live data to the overlay widget each frame
// ---------------------------------------------------------------------------

void ADESpectatorHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!OverlayWidget) return;

	ADEAgent* Agent = GetObservedCharacter();
	if (!Agent)
	{
		OverlayWidget->SetVisibility(ESlateVisibility::Hidden);
		return;
	}

	OverlayWidget->SetVisibility(ESlateVisibility::HitTestInvisible);

	// ── Identity ────────────────────────────────────────────────────────────

	const EDEClassType Class = Agent->GetCommandedClass();
	FString ClassName;
	FLinearColor ClassColor;

	switch (Class)
	{
	case EDEClassType::Strike:
		ClassName  = TEXT("STRIKE");
		ClassColor = FLinearColor(1.0f, 0.15f, 0.05f, 1.0f);
		break;
	case EDEClassType::Vanguard:
		ClassName  = TEXT("VANGUARD");
		ClassColor = FLinearColor(0.1f, 0.4f, 1.0f, 1.0f);
		break;
	case EDEClassType::Support:
		ClassName  = TEXT("SUPPORT");
		ClassColor = FLinearColor(0.45f, 1.0f, 0.45f, 1.0f);
		break;
	default:
		ClassName  = TEXT("UNKNOWN");
		ClassColor = FLinearColor::White;
		break;
	}

	const int32 TeamID = Agent->Execute_GetTeamID(Agent);
	FString TeamName;
	FLinearColor TeamLabelColor;
	switch (TeamID)
	{
	case 0:
		TeamName       = TEXT("RED");
		TeamLabelColor = FLinearColor(1.0f, 0.15f, 0.05f, 1.0f);
		break;
	case 1:
		TeamName       = TEXT("BLUE");
		TeamLabelColor = FLinearColor(0.1f, 0.4f, 1.0f, 1.0f);
		break;
	default:
		TeamName       = FString::Printf(TEXT("TEAM %d"), TeamID);
		TeamLabelColor = Agent->TeamColor.A > 0.0f ? Agent->TeamColor : FLinearColor::White;
		break;
	}

	OverlayWidget->UpdateAgentInfo(TeamName, TeamLabelColor, ClassName, ClassColor);

	// ── Status bars ─────────────────────────────────────────────────────────

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

	OverlayWidget->UpdateStatusBars(HealthPct, CurrentAmmo, MaxAmmo, ManaPct);

	// ── EQS weights ─────────────────────────────────────────────────────────

	OverlayWidget->UpdateEQSWeights(Agent->GetEQSWeights());
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

ADEAgent* ADESpectatorHUD::GetObservedCharacter() const
{
	if (ADESpectatorController* SC = Cast<ADESpectatorController>(PlayerOwner))
	{
		return SC->GetObservedCharacter();
	}
	return nullptr;
}
