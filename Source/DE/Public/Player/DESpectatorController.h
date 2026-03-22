// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Types/DEClassTypes.h"
#include "DESpectatorController.generated.h"

class ADEAgent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnObservedEnvChanged, int32, NewEnvID);

/**
 * ADESpectatorController
 *
 * A player controller that lets a human observer cycle through AI agents using
 * keyboard buttons, viewing each agent through its third-person camera without
 * disrupting AI behavior (SetViewTargetWithBlend — no possession).
 *
 * Controls:
 *   Button 1 (key "1"):
 *     - Tap  → cycle through agents in class order: Strike → Vanguard → Support → Strike…
 *     - Hold → cycle to the next agent every CycleInterval seconds while held
 *   Button 2 (key "2"):
 *     - Disable camera mode, return to default spectator view
 *
 * Integration:
 *   Set ADESpectatorController as the PlayerControllerClass in the GameMode.
 *   Set ADESpectatorHUD as the HUDClass in the GameMode.
 */
UCLASS()
class DE_API ADESpectatorController : public APlayerController
{
	GENERATED_BODY()

public:
	ADESpectatorController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupInputComponent() override;
	virtual void GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;

	/** Returns the agent currently being observed, or null if camera is off */
	ADEAgent* GetObservedCharacter() const { return CurrentTarget; }

	/**
	 * Returns the EnvID of the currently observed agent, or -1 when no agent
	 * is being watched (camera disabled).  Blueprint score widgets placed near
	 * spawn areas can query this to decide whether to show or hide themselves.
	 */
	UFUNCTION(BlueprintPure, Category = "Camera|Environment")
	int32 GetObservedEnvID() const;

	/**
	 * Broadcast whenever the observed environment changes — fires with the new
	 * EnvID on agent switch, and with -1 when the camera is disabled.
	 * Bind this in spawn-area score widgets to avoid polling every frame.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Camera|Environment")
	FOnObservedEnvChanged OnObservedEnvChanged;

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

	/** Mouse sensitivity multiplier for orbit look (degrees per pixel) */
	UPROPERTY(EditAnywhere, Category = "Camera|Settings")
	float MouseSensitivity = 1.0f;

	/** Pitch clamp range for orbit look (degrees) */
	UPROPERTY(EditAnywhere, Category = "Camera|Settings")
	float PitchMin = -80.f;

	UPROPERTY(EditAnywhere, Category = "Camera|Settings")
	float PitchMax = 20.f;

	/** Base movement speed for free spectator pawn (UU/s) */
	UPROPERTY(EditAnywhere, Category = "Movement|Settings")
	float BaseSpectatorSpeed = 2000.f;

	/** Amount to add/subtract per mouse-wheel tick */
	UPROPERTY(EditAnywhere, Category = "Movement|Settings")
	float SpeedStep = 500.f;

	/** Minimum and maximum spectator movement speed */
	UPROPERTY(EditAnywhere, Category = "Movement|Settings")
	float MinSpeed = 200.f;

	UPROPERTY(EditAnywhere, Category = "Movement|Settings")
	float MaxSpeed = 10000.f;

private:
	// ---- Input Callbacks ----
	void OnCameraKeyPressed();
	void OnCameraKeyReleased();
	void OnDisableCameraPressed();
	void OnSpeedUp();
	void OnSpeedDown();

	void ApplySpectatorSpeed(float NewSpeed);

	// ---- Internal Helpers ----
	void SwitchToAgent(ADEAgent* Agent);

	/**
	 * Show/hide each agent's OverheadWidgetComponent based on EnvID.
	 * Pass the desired observed EnvID; -1 makes ALL widgets visible (camera off).
	 */
	void UpdateEnvironmentVisibility(int32 ObservedEnvID);
	void SwitchToNextClassAgent();
	void CycleToNextAgent();
	void DisableCamera();
	void CollectAgents();

	// ---- Timer Callbacks ----
	void OnHoldTimerFired();   // Fires HoldThreshold seconds after key press → begin cycling
	void OnCycleTimerFired();  // Fires every CycleInterval seconds while key is held → next agent

	// ---- State ----
	UPROPERTY()
	TArray<ADEAgent*> AllAgents;

	UPROPERTY()
	ADEAgent* CurrentTarget = nullptr;

	int32 CurrentAgentIndex      = -1;
	int32 CurrentClassPhase   = 0;   // 0=Strike, 1=Vanguard, 2=Support (tap cycle order)
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

	/** Tap-cycle order: Strike → Vanguard → Support */
	static constexpr EDEClassType ClassOrder[] = {
		EDEClassType::Strike,
		EDEClassType::Vanguard,
		EDEClassType::Support
	};

	FTimerHandle HoldTimerHandle;
	FTimerHandle CycleTimerHandle;

	/** Current spectator movement speed (modified by mouse wheel) */
	float CurrentSpectatorSpeed;
};
