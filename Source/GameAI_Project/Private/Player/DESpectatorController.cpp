// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/DESpectatorController.h"
#include "Characters/DECharacter.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Math/RandomStream.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/FloatingPawnMovement.h"

ADESpectatorController::ADESpectatorController()
{
	bAutoManageActiveCameraTarget = false;
	PrimaryActorTick.bCanEverTick = true;
	CurrentSpectatorSpeed = BaseSpectatorSpeed;
}

void ADESpectatorController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = false;
	CurrentSpectatorSpeed = BaseSpectatorSpeed;
	ApplySpectatorSpeed(CurrentSpectatorSpeed);
}

void ADESpectatorController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsBlending)
	{
		BlendElapsed += DeltaTime;
		if (BlendElapsed >= ViewBlendTime)
		{
			bIsBlending = false;
		}
	}

	// Mouse orbit while watching an agent.
	// GetInputMouseDelta reads raw viewport hardware deltas — reliable with both Legacy and Enhanced Input.
	if (bCameraActive && IsValid(CurrentTarget))
	{
		float DeltaX = 0.f, DeltaY = 0.f;
		GetInputMouseDelta(DeltaX, DeltaY);

		if (!FMath::IsNearlyZero(DeltaX) || !FMath::IsNearlyZero(DeltaY))
		{
			FixedCameraRotation.Yaw  += DeltaX * MouseSensitivity;
			FixedCameraRotation.Pitch = FMath::Clamp(
				FixedCameraRotation.Pitch - DeltaY * MouseSensitivity,  // subtract: mouse up = camera up
				PitchMin, PitchMax);
		}
	}
}

void ADESpectatorController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent) return;

	InputComponent->BindKey(EKeys::One, IE_Pressed,  this, &ADESpectatorController::OnCameraKeyPressed);
	InputComponent->BindKey(EKeys::One, IE_Released, this, &ADESpectatorController::OnCameraKeyReleased);
	InputComponent->BindKey(EKeys::Two, IE_Pressed,  this, &ADESpectatorController::OnDisableCameraPressed);

	// Mouse wheel — adjust spectator movement speed
	InputComponent->BindKey(EKeys::MouseScrollUp,   IE_Pressed, this, &ADESpectatorController::OnSpeedUp);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ADESpectatorController::OnSpeedDown);
}

// ---------------------------------------------------------------------------
// Input Callbacks
// ---------------------------------------------------------------------------

void ADESpectatorController::OnCameraKeyPressed()
{
	bIsKeyHeld = true;

	// Collect / refresh the agent list every press
	CollectAgents();

	if (AllAgents.IsEmpty()) return;

	// Immediately switch to the next agent in strategy order (Assault → Defend → Support)
	SwitchToNextStrategyAgent();

	// Start hold-detection timer — if still held after HoldThreshold, begin cycling
	GetWorldTimerManager().SetTimer(
		HoldTimerHandle,
		this,
		&ADESpectatorController::OnHoldTimerFired,
		HoldThreshold,
		false
	);
}

void ADESpectatorController::OnCameraKeyReleased()
{
	bIsKeyHeld = false;

	// Cancel hold-detection timer (key was released before hold threshold)
	GetWorldTimerManager().ClearTimer(HoldTimerHandle);

	// Stop cycling
	if (bIsCycling)
	{
		GetWorldTimerManager().ClearTimer(CycleTimerHandle);
		bIsCycling = false;
	}
}

void ADESpectatorController::OnDisableCameraPressed()
{
	DisableCamera();
}

// ---------------------------------------------------------------------------
// Timer Callbacks
// ---------------------------------------------------------------------------

void ADESpectatorController::OnHoldTimerFired()
{
	if (!bIsKeyHeld) return;

	// Enter cycling mode: switch to next agent every CycleInterval seconds
	bIsCycling = true;
	GetWorldTimerManager().SetTimer(
		CycleTimerHandle,
		this,
		&ADESpectatorController::OnCycleTimerFired,
		CycleInterval,
		true   // looping
	);
}

void ADESpectatorController::OnCycleTimerFired()
{
	if (!bIsKeyHeld)
	{
		GetWorldTimerManager().ClearTimer(CycleTimerHandle);
		bIsCycling = false;
		return;
	}
	CycleToNextAgent();
}

// ---------------------------------------------------------------------------
// Speed Control
// ---------------------------------------------------------------------------

void ADESpectatorController::ApplySpectatorSpeed(float NewSpeed)
{
	if (APawn* MyPawn = GetPawn())
	{
		if (UFloatingPawnMovement* Move = MyPawn->FindComponentByClass<UFloatingPawnMovement>())
		{
			Move->MaxSpeed = NewSpeed;
		}
	}
}

void ADESpectatorController::OnSpeedUp()
{
	CurrentSpectatorSpeed = FMath::Clamp(CurrentSpectatorSpeed + SpeedStep, MinSpeed, MaxSpeed);
	ApplySpectatorSpeed(CurrentSpectatorSpeed);
}

void ADESpectatorController::OnSpeedDown()
{
	CurrentSpectatorSpeed = FMath::Clamp(CurrentSpectatorSpeed - SpeedStep, MinSpeed, MaxSpeed);
	ApplySpectatorSpeed(CurrentSpectatorSpeed);
}

// ---------------------------------------------------------------------------
// Agent Management
// ---------------------------------------------------------------------------

void ADESpectatorController::CollectAgents()
{
	AllAgents.Empty();
	if (UWorld* World = GetWorld())
	{
		for (ADECharacter* Char : TActorRange<ADECharacter>(World))
		{
			if (IsValid(Char) && Char->IsAlive())
			{
				AllAgents.Add(Char);
			}
		}
	}
}

void ADESpectatorController::SwitchToNextStrategyAgent()
{
	if (AllAgents.IsEmpty()) return;

	// Search all agents (both teams) for the first alive agent matching the current strategy phase.
	// Cycles through all three phases before giving up.
	ADECharacter* Found = nullptr;
	for (int32 i = 0; i < 3 && !Found; ++i)
	{
		const EDEStrategyType TargetStrategy = StrategyOrder[CurrentStrategyPhase];
		CurrentStrategyPhase = (CurrentStrategyPhase + 1) % 3;

		for (ADECharacter* Agent : AllAgents)
		{
			if (IsValid(Agent) && Agent->IsAlive() && Agent->GetCommandedStrategy() == TargetStrategy)
			{
				Found = Agent;
				break;
			}
		}
	}

	if (Found)
	{
		SwitchToAgent(Found);
	}
	else
	{
		// Fallback: all agents are dead or unclassified — pick first alive agent
		SwitchToAgent(AllAgents[0]);
	}
}

void ADESpectatorController::CycleToNextAgent()
{
	if (AllAgents.IsEmpty()) return;

	// Advance past dead agents, wrapping around the full list
	const int32 StartIndex = CurrentAgentIndex;
	do
	{
		CurrentAgentIndex = (CurrentAgentIndex + 1) % AllAgents.Num();
	}
	while (!AllAgents[CurrentAgentIndex]->IsAlive() && CurrentAgentIndex != StartIndex);

	SwitchToAgent(AllAgents[CurrentAgentIndex]);
}

void ADESpectatorController::GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	if (bCameraActive && IsValid(CurrentTarget))
	{
		// Camera follows the agent with a fixed offset and fixed rotation (locked at switch time)
		// Negate X so positive CameraOffset.X means "behind the agent"
		const FVector BehindOffset(-CameraOffset.X, CameraOffset.Y, CameraOffset.Z);
		const FVector TargetLocation = CurrentTarget->GetActorLocation() + FixedCameraRotation.RotateVector(BehindOffset);
		const FRotator TargetRotation = FixedCameraRotation;

		if (bIsBlending && ViewBlendTime > 0.f)
		{
			const float Alpha = FMath::Clamp(BlendElapsed / ViewBlendTime, 0.f, 1.f);
			const float SmoothAlpha = FMath::SmoothStep(0.f, 1.f, Alpha);

			OutLocation = FMath::Lerp(BlendFromLocation, TargetLocation, SmoothAlpha);
			OutRotation = FMath::Lerp(BlendFromRotation, TargetRotation, SmoothAlpha);
		}
		else
		{
			OutLocation = TargetLocation;
			OutRotation = TargetRotation;
		}
		return;
	}
	Super::GetPlayerViewPoint(OutLocation, OutRotation);
}

void ADESpectatorController::SwitchToAgent(ADECharacter* Agent)
{
	if (!IsValid(Agent)) return;

	// Save current camera state for blending
	if (bCameraActive && IsValid(CurrentTarget))
	{
		const FVector BehindOffsetBlend(-CameraOffset.X, CameraOffset.Y, CameraOffset.Z);
		BlendFromLocation = CurrentTarget->GetActorLocation() + FixedCameraRotation.RotateVector(BehindOffsetBlend);
		BlendFromRotation = FixedCameraRotation;
	}
	else
	{
		// First activation — blend from current spectator view
		GetPlayerViewPoint(BlendFromLocation, BlendFromRotation);
	}

	// Clear previous observation flag
	if (IsValid(CurrentTarget) && CurrentTarget != Agent)
	{
		CurrentTarget->bIsBeingObserved = false;
	}

	CurrentTarget = Agent;
	bCameraActive = true;
	Agent->bIsBeingObserved = true;

	// Lock camera rotation to the agent's current facing direction with a slight downward pitch
	FixedCameraRotation = Agent->GetActorRotation();
	FixedCameraRotation.Pitch = -15.f;

	// Start blend interpolation
	BlendElapsed = 0.f;
	bIsBlending = true;

	// View through this controller so GetPlayerViewPoint drives the camera.
	SetViewTarget(this);
}

void ADESpectatorController::DisableCamera()
{
	// Clear observation flag on previous target
	if (IsValid(CurrentTarget))
	{
		CurrentTarget->bIsBeingObserved = false;
	}

	CurrentTarget        = nullptr;
	CurrentAgentIndex    = -1;
	CurrentStrategyPhase = 0;
	bCameraActive        = false;
	bIsCycling         = false;

	GetWorldTimerManager().ClearTimer(HoldTimerHandle);
	GetWorldTimerManager().ClearTimer(CycleTimerHandle);

	// Return to default spectator view (no explicit target)
	SetViewTarget(this);
}
