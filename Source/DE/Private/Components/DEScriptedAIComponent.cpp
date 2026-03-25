// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DEScriptedAIComponent.h"
#include "Characters/DEAgent.h"
#include "Team/DETeamInterface.h"
#include "Engine/OverlapResult.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"
#include "EQS/DynamicEQSExecutor.h"


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

	// Stagger tick intervals so agents don't all call StopMovement() simultaneously.
	// Without this, 5 agents start ticking in the same frame → nav system gridlock.
	const float Stagger = FMath::FRandRange(0.0f, UpdateInterval * 0.6f);
	PrimaryComponentTick.TickInterval = UpdateInterval + Stagger;

	UE_LOG(LogTemp, Log, TEXT("[DEScriptedAI] Initialized on %s (Tier %d, State %s)"),
		*GetOwner()->GetName(), CurrentTier,
		*StaticEnum<EScriptedAIState>()->GetNameStringByValue((int64)CurrentState));
}

void UDEScriptedAIComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OwnerAgent.IsValid()) return;

	// State machine runs every tick interval (0.5s)
	UpdateAIState();
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

	// Reduce noise as tier increases — higher tiers use consistent, competent strategies.
	// Tier 3 is fully deterministic so the RL agent faces a stable, hard opponent.
	switch (CurrentTier)
	{
	case 0: NoiseMagnitude = 0.0f;  break; // Pure random — noise has no meaning
	case 1: NoiseMagnitude = 0.08f; break;
	case 2: NoiseMagnitude = 0.04f; break;
	case 3: NoiseMagnitude = 0.0f;  break; // Deterministic expert
	}

	// Tighten state-machine thresholds at Tier 3: agents fight longer before retreating
	// and recover faster, making them more aggressive overall.
	if (CurrentTier >= 3)
	{
		RetreatHealthThreshold  = 0.20f;
		RecoverHealthThreshold  = 0.75f;
	}
	else
	{
		RetreatHealthThreshold  = 0.30f;
		RecoverHealthThreshold  = 0.60f;
	}

	if (OwnerAgent.IsValid())
	{
		ApplyWeightsToAgent();
	}
}


// ─────────────────────────────────────────────────────────────────────────────
// State Machine
// ─────────────────────────────────────────────────────────────────────────────

void UDEScriptedAIComponent::UpdateAIState()
{
	if (!OwnerAgent.IsValid()) return;

	const float HealthPct      = OwnerAgent->GetHealthPercentage();
	const bool  bEnemyDetected = IsEnemyWithinRange(DetectionRadius);
	const bool  bEnemyNear     = IsEnemyWithinRange(EngagementRadius);

	EScriptedAIState NewState = CurrentState;

	switch (CurrentState)
	{
	case EScriptedAIState::Patrol:
		if (bEnemyDetected)
			NewState = EScriptedAIState::Approach;
		break;

	case EScriptedAIState::Approach:
		if (HealthPct < RetreatHealthThreshold)
			NewState = EScriptedAIState::Retreat;
		else if (bEnemyNear)
			NewState = EScriptedAIState::Engage;
		else if (!bEnemyDetected)
			NewState = EScriptedAIState::Patrol;
		break;

	case EScriptedAIState::Engage:
		if (HealthPct < RetreatHealthThreshold)
			NewState = EScriptedAIState::Retreat;
		else if (!bEnemyNear)
			NewState = EScriptedAIState::Approach;
		break;

	case EScriptedAIState::Retreat:
		if (HealthPct > RecoverHealthThreshold)
			NewState = EScriptedAIState::Approach;
		break;
	}

	if (NewState != CurrentState)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[DEScriptedAI] %s: %s → %s  (hp=%.2f)"),
			*GetOwner()->GetName(),
			*StaticEnum<EScriptedAIState>()->GetNameStringByValue((int64)CurrentState),
			*StaticEnum<EScriptedAIState>()->GetNameStringByValue((int64)NewState),
			HealthPct);
		CurrentState = NewState;
	}
}

bool UDEScriptedAIComponent::IsEnemyWithinRange(float Radius) const
{
	if (!OwnerAgent.IsValid() || !GetWorld()) return false;

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ScriptedAIDetect), false, OwnerAgent.Get());

	const bool bHit = GetWorld()->OverlapMultiByChannel(
		Overlaps,
		OwnerAgent->GetActorLocation(),
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(Radius),
		Params
	);

	if (!bHit) return false;

	const int32 MyTeam = OwnerAgent->GetTeamID_Implementation();
	for (const FOverlapResult& O : Overlaps)
	{
		const IDETeamInterface* TeamIface = Cast<IDETeamInterface>(O.GetActor());
		if (TeamIface && TeamIface->Execute_GetTeamID(O.GetActor()) != MyTeam)
		{
			return true;
		}
	}
	return false;
}


// ─────────────────────────────────────────────────────────────────────────────
// Weight Computation
// ─────────────────────────────────────────────────────────────────────────────

FDEEQSWeightParameters UDEScriptedAIComponent::GetBaseWeights(EDEClassType Class)
{
	// Base weights represent the Tier 2 (Full) per-class profile.
	// Tier 3 (Expert) overrides these in ApplyTierModifiers.
	FDEEQSWeightParameters W;

	switch (Class)
	{
	case EDEClassType::Strike:
		W.EnemyObjectiveProximity = 0.8f;   // Push toward enemy objective
		W.AllyObjectiveProximity  = 0.1f;
		W.CoverDensity            = 0.5f;   // Use cover: ranged DPS benefits from concealment
		W.EnemyVisibility         = 0.7f;
		W.AllyProximity           = 0.2f;
		W.CombatRange             = 0.5f;   // Maintain engagement distance
		W.AssignedBaseProximity   = -0.2f;  // Actively leave spawn
		break;

	case EDEClassType::Vanguard:
		W.EnemyObjectiveProximity = 0.9f;   // Strongest forward push (melee tank)
		W.AllyObjectiveProximity  = 0.0f;
		W.CoverDensity            = 0.1f;   // Melee doesn't need cover
		W.EnemyVisibility         = 0.7f;   // Seek out enemies actively
		W.AllyProximity           = 0.2f;
		W.CombatRange             = -0.7f;  // Close distance aggressively
		W.AssignedBaseProximity   = -0.3f;  // Push out of base
		break;

	case EDEClassType::Support:
		W.EnemyObjectiveProximity = 0.0f;
		W.AllyObjectiveProximity  = 0.3f;
		W.CoverDensity            = 0.7f;   // Stay in cover while supporting
		W.EnemyVisibility         = -0.3f;  // Avoid enemy line of sight
		W.AllyProximity           = 0.9f;   // Follow teammates forward
		W.CombatRange             = -0.3f;
		W.AssignedBaseProximity   = -0.1f;  // Push out of spawn
		break;
	}

	return W;
}

FDEEQSWeightParameters UDEScriptedAIComponent::ApplyTierModifiers(const FDEEQSWeightParameters& Base) const
{
	FDEEQSWeightParameters W = Base;

	switch (CurrentTier)
	{
	case 0:
		// Random roam — randomize all weights each tick interval.
		// Introduces chaotic but non-threatening behaviour for early curriculum.
		W.EnemyObjectiveProximity = FMath::FRandRange(-1.0f, 1.0f);
		W.AllyObjectiveProximity  = FMath::FRandRange(-1.0f, 1.0f);
		W.CoverDensity            = FMath::FRandRange(-1.0f, 1.0f);
		W.EnemyVisibility         = FMath::FRandRange(-1.0f, 1.0f);
		W.AllyProximity           = FMath::FRandRange(-1.0f, 1.0f);
		W.CombatRange             = FMath::FRandRange(-1.0f, 1.0f);
		W.AssignedBaseProximity   = FMath::FRandRange(-1.0f, 1.0f);
		break;

	case 1:
		// Directed — per-class differentiation but no state machine.
		// Strike keeps range, Vanguard charges, Support follows.
		switch (OwnerAgent.IsValid() ? OwnerAgent->GetCommandedClass() : EDEClassType::Strike)
		{
		case EDEClassType::Strike:
			W.EnemyObjectiveProximity = 0.7f;
			W.AllyObjectiveProximity  = 0.0f;
			W.CoverDensity            = 0.4f;
			W.EnemyVisibility         = 0.8f;
			W.AllyProximity           = 0.0f;
			W.CombatRange             = 0.5f;
			W.AssignedBaseProximity   = -0.3f;
			break;
		case EDEClassType::Vanguard:
			W.EnemyObjectiveProximity = 1.0f;
			W.AllyObjectiveProximity  = 0.0f;
			W.CoverDensity            = 0.0f;
			W.EnemyVisibility         = 0.5f;
			W.AllyProximity           = 0.0f;
			W.CombatRange             = -0.8f;
			W.AssignedBaseProximity   = -0.4f;
			break;
		case EDEClassType::Support:
			W.EnemyObjectiveProximity = -0.2f;
			W.AllyObjectiveProximity  = 0.5f;
			W.CoverDensity            = 0.7f;
			W.EnemyVisibility         = -0.4f;
			W.AllyProximity           = 1.0f;
			W.CombatRange             = -0.3f;
			W.AssignedBaseProximity   = -0.1f;
			break;
		}
		break;

	case 2:
		// Full — use per-class base weights from GetBaseWeights(); state machine active.
		break;

	case 3:
		// Expert — maximally competent, fully deterministic (NoiseMagnitude=0).
		// Strike kites at range; Vanguard flanks; Support strict rear-guard.
		switch (OwnerAgent.IsValid() ? OwnerAgent->GetCommandedClass() : EDEClassType::Strike)
		{
		case EDEClassType::Strike:
			W.EnemyObjectiveProximity = 0.8f;
			W.AllyObjectiveProximity  = 0.0f;
			W.CoverDensity            = 0.7f;   // Prefer flanking/cover positions
			W.EnemyVisibility         = 0.9f;   // Always maintain sight
			W.AllyProximity           = 0.2f;   // Loose team cohesion — advance with squad
			W.CombatRange             = 0.8f;   // Strong range discipline
			W.AssignedBaseProximity   = -0.3f;
			break;
		case EDEClassType::Vanguard:
			W.EnemyObjectiveProximity = 1.0f;
			W.AllyObjectiveProximity  = 0.0f;
			W.CoverDensity            = 0.0f;
			W.EnemyVisibility         = 0.8f;
			W.AllyProximity           = 0.2f;   // Flank with squad rather than fully solo
			W.CombatRange             = -1.0f;  // Always close to melee
			W.AssignedBaseProximity   = -0.5f;
			break;
		case EDEClassType::Support:
			W.EnemyObjectiveProximity = -0.5f;  // Stay away from enemy objectives
			W.AllyObjectiveProximity  = 0.4f;
			W.CoverDensity            = 0.9f;   // Maximum cover usage
			W.EnemyVisibility         = -0.5f;  // Avoid all enemy sight
			// 0.5 (not 1.0) — moderate pull keeps Support within healing range
			// without two Support agents converging to the exact same point.
			// For hard minimum-distance enforcement the EQS AllyProximity test
			// would need a donut scoring curve; this is the weight-layer cap.
			W.AllyProximity           = 0.5f;
			W.CombatRange             = -0.4f;
			W.AssignedBaseProximity   = -0.2f;
			break;
		}
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

FDEEQSWeightParameters UDEScriptedAIComponent::ApplyStateBehavior(const FDEEQSWeightParameters& In, EDEClassType Class) const
{
	// State machine active at Tier 2 and above
	if (CurrentTier < 2) return In;

	FDEEQSWeightParameters W = In;

	switch (CurrentState)
	{
	case EScriptedAIState::Patrol:
		// No enemies nearby — push hard toward objective, ignore combat range noise
		W.EnemyObjectiveProximity = FMath::Min(W.EnemyObjectiveProximity + 0.2f, 1.0f);
		W.CombatRange = 0.0f;
		// Ensure all classes actively leave spawn during patrol (prevents spawn clustering)
		W.AssignedBaseProximity = FMath::Min(W.AssignedBaseProximity, -0.1f);
		if (Class == EDEClassType::Support)
		{
			// Support follows allies even in Patrol — keep AllyProximity positive
			// so it trails Strike/Vanguard as they advance. Cap to avoid pinning
			// to spawn if teammates haven't moved yet.
			W.AllyProximity = FMath::Min(W.AllyProximity, 0.5f);
		}
		else
		{
			// Strike/Vanguard spread out: negative AllyProximity acts as soft
			// minimum-distance repulsion, preventing spawn clustering.
			W.AllyProximity = -0.3f;
		}
		break;

	case EScriptedAIState::Approach:
		// Enemy spotted, closing in — use base weights as-is
		break;

	case EScriptedAIState::Engage:
		// In fight — maximize visibility, close distance slightly
		W.EnemyVisibility = FMath::Min(W.EnemyVisibility + 0.2f, 1.0f);
		W.CombatRange     = FMath::Max(W.CombatRange - 0.1f, -1.0f);
		// Support: cap AllyProximity so two Support agents don't converge
		// to the same position while trying to heal the same target.
		// 0.4 provides enough pull to stay in healing range without stacking.
		if (Class == EDEClassType::Support)
		{
			W.AllyProximity = FMath::Min(W.AllyProximity, 0.4f);
		}
		break;

	case EScriptedAIState::Retreat:
		// Low health — flee to cover and allies regardless of class
		W.EnemyObjectiveProximity = -0.8f;
		W.CoverDensity            =  0.9f;
		W.AllyProximity           =  0.7f;
		W.AssignedBaseProximity   =  0.6f;
		W.EnemyVisibility         = -0.5f;
		W.CombatRange             = -0.8f;
		break;
	}

	W.Clamp();
	return W;
}

FDEEQSWeightParameters UDEScriptedAIComponent::GetScriptedWeights(EDEClassType Class) const
{
	const FDEEQSWeightParameters Base   = GetBaseWeights(Class);
	const FDEEQSWeightParameters Tiered = ApplyTierModifiers(Base);
	const FDEEQSWeightParameters Noisy  = ApplyNoise(Tiered);
	return ApplyStateBehavior(Noisy, Class);
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

	// Issue 3 fix: mirror weights to the Blackboard so "debug AI" displays
	// the actual values instead of zeros. The EQS reads from EQSExecutor
	// directly, but the BB keys are the authoritative debug view.
	if (AAIController* AIC = Cast<AAIController>(Agent->GetController()))
	{
		if (UBlackboardComponent* BB = AIC->GetBlackboardComponent())
		{
			BB->SetValueAsFloat(TEXT("Weight_EnemyObj"),   Weights.EnemyObjectiveProximity);
			BB->SetValueAsFloat(TEXT("Weight_AllyObj"),    Weights.AllyObjectiveProximity);
			BB->SetValueAsFloat(TEXT("Weight_Cover"),      Weights.CoverDensity);
			BB->SetValueAsFloat(TEXT("Weight_EnemyVis"),   Weights.EnemyVisibility);
			BB->SetValueAsFloat(TEXT("Weight_AllyProx"),   Weights.AllyProximity);
			BB->SetValueAsFloat(TEXT("Weight_Range"),      Weights.CombatRange);
			BB->SetValueAsFloat(TEXT("Weight_AssignBase"), Weights.AssignedBaseProximity);
		}
	}

	Agent->PerformTacticalAction();
}
