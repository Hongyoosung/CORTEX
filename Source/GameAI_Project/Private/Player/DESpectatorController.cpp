// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/DESpectatorController.h"
#include "Characters/DECharacter.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Math/RandomStream.h"

ADESpectatorController::ADESpectatorController()
{
	bAutoManageActiveCameraTarget = false;
}

void ADESpectatorController::BeginPlay()
{
	Super::BeginPlay();

	// Show cursor for spectator convenience (optional)
	bShowMouseCursor = false;
}

void ADESpectatorController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent) return;

	InputComponent->BindKey(EKeys::One, IE_Pressed,  this, &ADESpectatorController::OnCameraKeyPressed);
	InputComponent->BindKey(EKeys::One, IE_Released, this, &ADESpectatorController::OnCameraKeyReleased);
	InputComponent->BindKey(EKeys::Two, IE_Pressed,  this, &ADESpectatorController::OnDisableCameraPressed);

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
// Agent Management
// ---------------------------------------------------------------------------

void ADESpectatorController::CollectAgents()
{
	AllAgents.Empty();
	if (UWorld* World = GetWorld())
	{
		for (ADECharacter* Char : TActorRange<ADECharacter>(World))
		{
			if (IsValid(Char) && Char->IsAlive_Implementation())
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
			if (IsValid(Agent) && Agent->IsAlive_Implementation() && Agent->GetCommandedStrategy() == TargetStrategy)
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
	while (!AllAgents[CurrentAgentIndex]->IsAlive_Implementation() && CurrentAgentIndex != StartIndex);

	SwitchToAgent(AllAgents[CurrentAgentIndex]);
}

void ADESpectatorController::GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	if (bCameraActive && IsValid(CurrentTarget))
	{
		// Use the agent's third-person camera component if available
		if (UCameraComponent* Cam = CurrentTarget->AgentCamera.Get())
		{
			OutLocation = Cam->GetComponentLocation();
			OutRotation = Cam->GetComponentRotation();
		}
		else
		{
			OutLocation = CurrentTarget->GetPawnViewLocation();
			OutRotation = CurrentTarget->GetActorRotation();
		}
		return;
	}
	Super::GetPlayerViewPoint(OutLocation, OutRotation);
}

void ADESpectatorController::SwitchToAgent(ADECharacter* Agent)
{
	if (!IsValid(Agent)) return;

	// Clear previous observation flag
	if (IsValid(CurrentTarget) && CurrentTarget != Agent)
	{
		CurrentTarget->bIsBeingObserved = false;
	}

	CurrentTarget = Agent;
	bCameraActive = true;
	Agent->bIsBeingObserved = true;

	// View through this controller so GetPlayerViewPoint drives position + rotation.
	// The AI controller of the agent is unaffected (no possession).
	SetViewTargetWithBlend(this, ViewBlendTime, VTBlend_Cubic);
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
