// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Types/DEStrategyTypes.h"
#include "DESpectatorController.generated.h"

class ADECharacter;

/**
 * ADESpectatorController
 *
 * A player controller that lets a human observer cycle through AI agents using
 * keyboard buttons, viewing each agent through its third-person camera without
 * disrupting AI behavior (SetViewTargetWithBlend — no possession).
 *
 * Controls:
 *   Button 1 (key "1"):
 *     - Tap  → cycle through agents in strategy order: Assault → Defend → Support → Assault…
 *     - Hold → cycle to the next agent every CycleInterval seconds while held
 *   Button 2 (key "2"):
 *     - Disable camera mode, return to default spectator view
 *
 * Integration:
 *   Set ADESpectatorController as the PlayerControllerClass in the GameMode.
 *   Set ADESpectatorHUD as the HUDClass in the GameMode.
 */
UCLASS()
class GAMEAI_PROJECT_API ADESpectatorController : public APlayerController
{
	GENERATED_BODY()

public:
	ADESpectatorController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupInputComponent() override;
	virtual void GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;

	/** Returns the agent currently being observed, or null if camera is off */
	ADECharacter* GetObservedCharacter() const { return CurrentTarget; }

	/** Time after key press that hold-cycling begins (seconds) */
	UPROPERTY(EditAnywhere, Category = "Camera|Settings")
	float HoldThreshold = 0.5f;

	/** Interval between automatic agent switches while key is held (seconds) */
	UPROPERTY(EditAnywhere, Category = "Camera|Settings")
	float CycleInterval = 0.6f;

	/** Blend time when switching view targets (seconds) */
	UPROPERTY(EditAnywhere, Category = "Camera|Settings")
	float ViewBlendTime = 0.3f;

	/** Offset from agent position to place the fixed camera (relative to agent's forward at switch time) */
	UPROPERTY(EditAnywhere, Category = "Camera|Settings")
	FVector CameraOffset = FVector(400.f, 0.f, 200.f);

private:
	// ---- Input Callbacks ----
	void OnCameraKeyPressed();
	void OnCameraKeyReleased();
	void OnDisableCameraPressed();

	// ---- Internal Helpers ----
	void SwitchToAgent(ADECharacter* Agent);
	void SwitchToNextStrategyAgent();
	void CycleToNextAgent();
	void DisableCamera();
	void CollectAgents();

	// ---- Timer Callbacks ----
	void OnHoldTimerFired();   // Fires HoldThreshold seconds after key press → begin cycling
	void OnCycleTimerFired();  // Fires every CycleInterval seconds while key is held → next agent

	// ---- State ----
	UPROPERTY()
	TArray<ADECharacter*> AllAgents;

	UPROPERTY()
	ADECharacter* CurrentTarget = nullptr;

	int32 CurrentAgentIndex      = -1;
	int32 CurrentStrategyPhase   = 0;   // 0=Assault, 1=Defend, 2=Support (tap cycle order)
	bool bCameraActive    = false;
	bool bIsKeyHeld       = false;
	bool bIsCycling       = false;

	// ---- Fixed Camera ----
	/** World-space rotation locked at the moment the agent is selected (faces enemy base) */
	FRotator FixedCameraRotation = FRotator::ZeroRotator;

	// ---- Blend Between Agents ----
	FVector BlendFromLocation  = FVector::ZeroVector;
	FRotator BlendFromRotation = FRotator::ZeroRotator;
	float BlendElapsed = 0.f;
	bool bIsBlending   = false;

	/** Tap-cycle order: Assault → Defend → Support */
	static constexpr EDEStrategyType StrategyOrder[] = {
		EDEStrategyType::Assault,
		EDEStrategyType::Defend,
		EDEStrategyType::Support
	};

	FTimerHandle HoldTimerHandle;
	FTimerHandle CycleTimerHandle;
};
