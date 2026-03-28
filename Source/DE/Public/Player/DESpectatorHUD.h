// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DESpectatorHUD.generated.h"

class ADESpectatorController;
class ADEAgent;
class UDESpectatorOverlayWidget;

/**
 * ADESpectatorHUD
 *
 * Widget-based HUD displayed while a spectator observes an agent via ADESpectatorController.
 * All display is delegated to UDESpectatorOverlayWidget — no Canvas primitives are drawn.
 *
 * Setup:
 *  1. Create a Widget Blueprint subclass of UDESpectatorOverlayWidget.
 *  2. Assign it to OverlayWidgetClass (on this HUD's Blueprint subclass or in the editor).
 *  3. The widget is created and added to the viewport automatically in BeginPlay.
 */
UCLASS()
class DE_API ADESpectatorHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	/** Widget Blueprint class to instantiate as the spectator overlay */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DESpectatorHUD|UI")
	TSubclassOf<UDESpectatorOverlayWidget> OverlayWidgetClass;

private:
	/** Live widget instance added to the viewport in BeginPlay */
	UPROPERTY()
	TObjectPtr<UDESpectatorOverlayWidget> OverlayWidget;

	/** Get the currently observed DEAgent from the owning controller (null if none) */
	ADEAgent* GetObservedCharacter() const;
};
