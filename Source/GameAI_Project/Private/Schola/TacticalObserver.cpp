// TacticalObserver.cpp - Schola observer for 64-feature tactical observation (v5.0)

#include "Schola/TacticalObserver.h"
#include "Team/FollowerAgentComponent.h"
#include "Observation/ObservationElement.h"
#include "Common/Spaces/BoxSpace.h"
#include "Common/Points/BoxPoint.h"

UTacticalObserver::UTacticalObserver()
{
	// Build observation space (65 continuous features, v5.0: 64 + strategy index)
	TArray<FBoxSpaceDimension> Dimensions;
	Dimensions.Reserve(65);

	// All features normalized to [-1, 1] or [0, 1]
	for (int32 i = 0; i < 64; ++i)
	{
		FBoxSpaceDimension Dim;
		Dim.Low = -1.0f;
		Dim.High = 1.0f;
		Dimensions.Add(Dim);
	}

	// Strategy index (65th feature): 0=Assault, 1=Defend, 2=Support, 3=Retreat
	FBoxSpaceDimension StrategyDim;
	StrategyDim.Low = 0.0f;
	StrategyDim.High = 3.0f;
	Dimensions.Add(StrategyDim);

	CachedObservationSpace = FBoxSpace(Dimensions);
}

void UTacticalObserver::InitializeObserver()
{
	if (bAutoFindFollower && FollowerAgent == nullptr)
	{
		FollowerAgent = FindFollowerAgent();
	}

	if (FollowerAgent)
	{
		UE_LOG(LogTemp, Log, TEXT("[TacticalObserver] Initialized with FollowerAgent on %s"),
			*GetOuter()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalObserver] No FollowerAgent found on %s"),
			*GetOuter()->GetName());
	}
}

void UTacticalObserver::ResetObserver()
{
	// Re-find follower if needed
	if (bAutoFindFollower && FollowerAgent == nullptr)
	{
		FollowerAgent = FindFollowerAgent();
	}
}

FBoxSpace UTacticalObserver::GetObservationSpace() const
{
	return CachedObservationSpace;
}

void UTacticalObserver::CollectObservations(FBoxPoint& OutObservations)
{
	static int32 CallCount = 0;
	CallCount++;

	OutObservations.Values.SetNum(65);  // v5.0: 64 core features + 1 strategy index

	// CRITICAL: Add safety checks to prevent crash during initialization
	if (!FollowerAgent || !FollowerAgent->IsValidLowLevel() || !FollowerAgent->GetOwner())
	{
		if (CallCount % 100 == 1) // Log every 100th call to avoid spam
		{
			UE_LOG(LogTemp, Error, TEXT("[TacticalObserver] CollectObservations called but FollowerAgent is NULL or invalid! (Call #%d)"), CallCount);
		}
		// Return zeros if no follower
		for (int32 i = 0; i < 65; ++i)
		{
			OutObservations.Values[i] = 0.0f;
		}
		return;
	}

	if (CallCount % 100 == 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TacticalObserver] CollectObservations #%d for %s"),
			CallCount, *FollowerAgent->GetOwner()->GetName());
	}

	// Get observation from follower and convert to feature vector (64 features, v5.0)
	// Includes: Agent State(7) + Combat(1) + Perception(32) + Enemies(16) + Tactical(4) + Support Context(4)
	const FObservationElement& Obs = FollowerAgent->GetLocalObservation();
	TArray<float> Features = Obs.ToFeatureVector();

	// Copy all 64 core features
	check(Features.Num() == 64);
	for (int32 i = 0; i < 64; ++i)
	{
		OutObservations.Values[i] = Features[i];
	}

	// v5.0: Append strategy index as 65th feature for Python multi-head network
	// Strategy derived from current objective assigned by MCTS
	UObjective* CurrentObjective = FollowerAgent->GetCurrentObjective();
	float StrategyIndex = 0.0f;  // Default: Assault

	if (CurrentObjective && CurrentObjective->IsActive())
	{
		// Map EStrategyType to index: Assault=0, Defend=1, Support=2, Retreat=3
		StrategyIndex = static_cast<float>(CurrentObjective->Type);
	}

	OutObservations.Values[64] = StrategyIndex;
}

UFollowerAgentComponent* UTacticalObserver::FindFollowerAgent() const
{
	AActor* Owner = Cast<AActor>(GetOuter());
	if (!Owner)
	{
		// Try to find through actor component hierarchy
		UActorComponent* OuterComponent = Cast<UActorComponent>(GetOuter());
		if (OuterComponent)
		{
			Owner = OuterComponent->GetOwner();
		}
	}

	if (Owner)
	{
		return Owner->FindComponentByClass<UFollowerAgentComponent>();
	}

	return nullptr;
}
