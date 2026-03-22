// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DEScriptedAIComponent.h"
#include "Characters/DEAgent.h"


UDEScriptedAIComponent::UDEScriptedAIComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.5f;

	// Initialize noise offsets to zero
	NoiseOffsets.SetNumZeroed(FDEEQSWeightParameters::NumWeights);
}

void UDEScriptedAIComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerAgent = Cast<ADEAgent>(GetOwner());
	if (!OwnerAgent.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[DEScriptedAI] Owner is not an ADEAgent — component will be inactive."));
		SetComponentTickEnabled(false);
		return;
	}

	// Apply initial weights
	ApplyWeightsToAgent();

	UE_LOG(LogTemp, Log, TEXT("[DEScriptedAI] Initialized on %s (Tier %d)"),
		*GetOwner()->GetName(), CurrentTier);
}

void UDEScriptedAIComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OwnerAgent.IsValid()) return;

	// Apply weights at tick interval (set via PrimaryComponentTick.TickInterval)
	ApplyWeightsToAgent();
}


// ─────────────────────────────────────────────────────────────────────────────
// Difficulty Tier
// ─────────────────────────────────────────────────────────────────────────────

void UDEScriptedAIComponent::SetDifficultyTier(int32 Tier)
{
	CurrentTier = FMath::Clamp(Tier, 0, 3);
	UE_LOG(LogTemp, Log, TEXT("[DEScriptedAI] %s tier set to %d"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"), CurrentTier);

	// Immediately apply new weights
	if (OwnerAgent.IsValid())
	{
		ApplyWeightsToAgent();
	}
}


// ─────────────────────────────────────────────────────────────────────────────
// Weight Computation
// ─────────────────────────────────────────────────────────────────────────────

FDEEQSWeightParameters UDEScriptedAIComponent::GetBaseWeights(EDEClassType Class)
{
	FDEEQSWeightParameters W;

	switch (Class)
	{
	case EDEClassType::Strike:
		W.EnemyObjectiveProximity = 0.6f;
		W.AllyObjectiveProximity  = 0.2f;
		W.CoverDensity            = 0.5f;
		W.EnemyVisibility         = 0.7f;
		W.AllyProximity           = 0.3f;
		W.CombatRange             = 0.8f;
		W.AssignedBaseProximity   = 0.5f;
		break;

	case EDEClassType::Vanguard:
		W.EnemyObjectiveProximity = 0.8f;
		W.AllyObjectiveProximity  = 0.1f;
		W.CoverDensity            = 0.2f;
		W.EnemyVisibility         = 0.4f;
		W.AllyProximity           = 0.3f;
		W.CombatRange             = -0.5f;
		W.AssignedBaseProximity   = 0.6f;
		break;

	case EDEClassType::Support:
		W.EnemyObjectiveProximity = -0.3f;
		W.AllyObjectiveProximity  = 0.4f;
		W.CoverDensity            = 0.7f;
		W.EnemyVisibility         = -0.2f;
		W.AllyProximity           = 0.9f;
		W.CombatRange             = -0.3f;
		W.AssignedBaseProximity   = 0.3f;
		break;
	}

	return W;
}

FDEEQSWeightParameters UDEScriptedAIComponent::ApplyTierModifiers(const FDEEQSWeightParameters& Base) const
{
	FDEEQSWeightParameters W = Base;

	switch (CurrentTier)
	{
	case 0: // Passive — all weights zeroed except AssignedBaseProximity
		W.EnemyObjectiveProximity = 0.0f;
		W.AllyObjectiveProximity  = 0.0f;
		W.CoverDensity            = 0.0f;
		W.EnemyVisibility         = 0.0f;
		W.AllyProximity           = 0.0f;
		W.CombatRange             = 0.0f;
		W.AssignedBaseProximity   = 0.5f;
		break;

	case 1: // Basic — use base weights (no attack ability usage handled by ability system)
		// Weights as-is; combat suppressed at the ability level
		break;

	case 2: // Standard — full weights + normal combat
		// No modification needed
		break;

	case 3: // Aggressive — boost EnemyObjectiveProximity by +0.2
		W.EnemyObjectiveProximity = FMath::Clamp(W.EnemyObjectiveProximity + 0.2f, -1.0f, 1.0f);
		break;
	}

	return W;
}

FDEEQSWeightParameters UDEScriptedAIComponent::ApplyNoise(const FDEEQSWeightParameters& Weights) const
{
	if (NoiseOffsets.Num() != FDEEQSWeightParameters::NumWeights) return Weights;

	TArray<float> Arr = Weights.ToArray();
	for (int32 i = 0; i < Arr.Num() && i < NoiseOffsets.Num(); ++i)
	{
		Arr[i] = FMath::Clamp(Arr[i] + NoiseOffsets[i], -1.0f, 1.0f);
	}
	return FDEEQSWeightParameters::FromArray(Arr);
}

FDEEQSWeightParameters UDEScriptedAIComponent::GetScriptedWeights(EDEClassType Class) const
{
	FDEEQSWeightParameters Base = GetBaseWeights(Class);
	FDEEQSWeightParameters Tiered = ApplyTierModifiers(Base);
	return ApplyNoise(Tiered);
}

void UDEScriptedAIComponent::ResampleNoise(const FRandomStream& Stream)
{
	NoiseOffsets.SetNum(FDEEQSWeightParameters::NumWeights);
	for (int32 i = 0; i < NoiseOffsets.Num(); ++i)
	{
		NoiseOffsets[i] = Stream.FRandRange(-NoiseMagnitude, NoiseMagnitude);
	}
}

void UDEScriptedAIComponent::ApplyWeightsToAgent()
{
	if (!OwnerAgent.IsValid()) return;

	ADEAgent* Agent = OwnerAgent.Get();
	const EDEClassType Class = Agent->GetCommandedClass();
	const FDEEQSWeightParameters Weights = GetScriptedWeights(Class);

	Agent->UpdateTacticalWeights(Weights);
	Agent->PerformTacticalAction();
}
