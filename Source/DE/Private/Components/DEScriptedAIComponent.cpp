// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DEScriptedAIComponent.h"
#include "Characters/DEAgent.h"
#include "Team/DETeamInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
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

	// Issue 1 diagnostic: EQSExecutor must have a QueryTemplate assigned in the Blueprint.
	// If it is null, ExecuteQuerySynchronous() will always fail and the agent will use
	// the 300 cm fallback wander, causing it to appear stuck at its spawn position.
	if (UDynamicEQSExecutor* EQSExec = OwnerAgent->FindComponentByClass<UDynamicEQSExecutor>())
	{
		if (!EQSExec->QueryTemplate)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[DEScriptedAI] %s: EQSExecutor.QueryTemplate is NULL. "
				     "Open the agent's Blueprint, select the EQSExecutor component, "
				     "and assign the same EQS query asset used by the RL agent BPs. "
				     "Until this is fixed the agent will use 300 cm fallback movement."),
				*GetOwner()->GetName());
		}
	}

	// Load tier set by train.py curriculum scheduler
	LoadTierFromConfig();

	// Apply initial weights
	ApplyWeightsToAgent();

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
// Config loading (tier set by train.py)
// ─────────────────────────────────────────────────────────────────────────────

void UDEScriptedAIComponent::LoadTierFromConfig()
{
	const FString ConfigPath = FPaths::ProjectDir() / TEXT("scripted_ai_config.json");

	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *ConfigPath))
	{
		// Config not present — keep default tier
		return;
	}

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEScriptedAI] Failed to parse scripted_ai_config.json"));
		return;
	}

	int32 NewTier = CurrentTier;
	if (JsonObj->TryGetNumberField(TEXT("difficulty_tier"), NewTier))
	{
		SetDifficultyTier(NewTier);
		UE_LOG(LogTemp, Log, TEXT("[DEScriptedAI] Loaded tier %d from scripted_ai_config.json"), CurrentTier);
	}
}


// ─────────────────────────────────────────────────────────────────────────────
// Difficulty Tier
// ─────────────────────────────────────────────────────────────────────────────

void UDEScriptedAIComponent::SetDifficultyTier(int32 Tier)
{
	CurrentTier = FMath::Clamp(Tier, 0, 3);
	UE_LOG(LogTemp, Log, TEXT("[DEScriptedAI] %s tier set to %d"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("Unknown"), CurrentTier);

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
	FDEEQSWeightParameters W;

	switch (Class)
	{
	case EDEClassType::Strike:
		W.EnemyObjectiveProximity = 0.8f;   // Dominant: push toward enemy objective
		W.AllyObjectiveProximity  = 0.1f;
		W.CoverDensity            = 0.3f;   // Reduced: don't hug walls
		W.EnemyVisibility         = 0.7f;
		W.AllyProximity           = 0.2f;
		W.CombatRange             = 0.2f;   // Low: don't fight EnemyObjectiveProximity at match start
		W.AssignedBaseProximity   = -0.2f;  // Negative: actively leave spawn
		break;

	case EDEClassType::Vanguard:
		W.EnemyObjectiveProximity = 0.9f;   // Strongest forward push (melee tank)
		W.AllyObjectiveProximity  = 0.0f;
		W.CoverDensity            = 0.1f;   // Melee doesn't need cover
		W.EnemyVisibility         = 0.4f;
		W.AllyProximity           = 0.3f;
		W.CombatRange             = -0.5f;
		W.AssignedBaseProximity   = -0.3f;  // Negative: push out of base
		break;

	case EDEClassType::Support:
		W.EnemyObjectiveProximity = -0.3f;
		W.AllyObjectiveProximity  = 0.3f;
		W.CoverDensity            = 0.6f;
		W.EnemyVisibility         = -0.2f;
		W.AllyProximity           = 0.9f;   // Follow teammates forward
		W.CombatRange             = -0.3f;
		W.AssignedBaseProximity   = 0.0f;   // Neutral: follows allies instead
		break;
	}

	return W;
}

FDEEQSWeightParameters UDEScriptedAIComponent::ApplyTierModifiers(const FDEEQSWeightParameters& Base) const
{
	FDEEQSWeightParameters W = Base;

	switch (CurrentTier)
	{
	case 0: // Passive — stay near base
		W.EnemyObjectiveProximity = 0.0f;
		W.AllyObjectiveProximity  = 0.0f;
		W.CoverDensity            = 0.0f;
		W.EnemyVisibility         = 0.0f;
		W.AllyProximity           = 0.0f;
		W.CombatRange             = 0.0f;
		W.AssignedBaseProximity   = 0.5f;
		break;

	case 1: // Basic — base weights, combat suppressed at ability level
		break;

	case 2: // Standard — full weights
		break;

	case 3: // Aggressive — boost forward pressure
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

FDEEQSWeightParameters UDEScriptedAIComponent::ApplyStateBehavior(const FDEEQSWeightParameters& In) const
{
	// Passive tier overrides everything — state machine has no effect
	if (CurrentTier == 0) return In;

	FDEEQSWeightParameters W = In;

	switch (CurrentState)
	{
	case EScriptedAIState::Patrol:
		// No enemies nearby — push hard toward objective, ignore combat range noise
		W.EnemyObjectiveProximity = FMath::Min(W.EnemyObjectiveProximity + 0.2f, 1.0f);
		W.CombatRange = 0.0f;
		break;

	case EScriptedAIState::Approach:
		// Enemy spotted, closing in — use base weights as-is
		break;

	case EScriptedAIState::Engage:
		// In fight — maximize visibility, close distance slightly
		W.EnemyVisibility = FMath::Min(W.EnemyVisibility + 0.2f, 1.0f);
		W.CombatRange     = FMath::Max(W.CombatRange - 0.1f, -1.0f);
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
	return ApplyStateBehavior(Noisy);
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

	// [DIAG] Log final computed weights per agent (first 3 calls, then every 20s equivalent at 0.5s tick = every 40 ticks)
	{
		static TMap<FString, int32> WeightLogCount;
		int32& WCount = WeightLogCount.FindOrAdd(GetOwner()->GetName());
		if (WCount < 3 || WCount % 40 == 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[DEScriptedAI] %s class=%d state=%s tier=%d | EnemyObj=%.2f AllyObj=%.2f Cover=%.2f EnemyVis=%.2f AllyProx=%.2f Range=%.2f AssignBase=%.2f"),
				*GetOwner()->GetName(), (int32)Class,
				*StaticEnum<EScriptedAIState>()->GetNameStringByValue((int64)CurrentState),
				CurrentTier,
				Weights.EnemyObjectiveProximity, Weights.AllyObjectiveProximity,
				Weights.CoverDensity, Weights.EnemyVisibility,
				Weights.AllyProximity, Weights.CombatRange, Weights.AssignedBaseProximity);
		}
		++WCount;
	}

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
