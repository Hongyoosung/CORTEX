// TacticalObserver.cpp - Schola observer for 68-feature tactical observation (v6.0)

#include "Schola/TacticalObserver.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Observation/ObservationElement.h"
#include "Common/Spaces/BoxSpace.h"
#include "Common/Points/BoxPoint.h"

UTacticalObserver::UTacticalObserver()
{
	// Build observation space (50 continuous features: 46 base + 4 strategy one-hot)
	TArray<FBoxSpaceDimension> Dimensions;
	Dimensions.Reserve(50);

	// 46 base features: normalized to [-1, 1] or [0, 1]
	for (int32 i = 0; i < 46; ++i)
	{
		FBoxSpaceDimension Dim;
		Dim.Low = -1.0f;
		Dim.High = 1.0f;
		Dimensions.Add(Dim);
	}

	// 4 strategy one-hot features: [0, 1]
	for (int32 i = 0; i < 4; ++i)
	{
		FBoxSpaceDimension Dim;
		Dim.Low = 0.0f;
		Dim.High = 1.0f;
		Dimensions.Add(Dim);
	}

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

	OutObservations.Values.SetNum(50);  // v8.0: 46 base features + 4 strategy one-hot
	// CRITICAL: Add safety checks to prevent crash during initialization
	if (!FollowerAgent || !FollowerAgent->IsValidLowLevel() || !FollowerAgent->GetOwner())
	{
		if (CallCount % 100 == 1) // Log every 100th call to avoid spam
		{
			UE_LOG(LogTemp, Error, TEXT("[TacticalObserver] CollectObservations called but FollowerAgent is NULL or invalid! (Call #%d)"), CallCount);
		}
		// Return zeros if no follower (46 base + 4 strategy = 50)
		for (int32 i = 0; i < 50; ++i)
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

	// v8.0: Get observation from follower (46 base features)
	// Includes: Position(3) + Health(1) + EnemyDist(1) + Raycasts(16) +
	//           EnemyInfo(16) + Tactical(4) + Support(5) = 46 features
	const FObservationElement& Obs = FollowerAgent->GetLocalObservation();
	TArray<float> Features = Obs.ToFeatureVector();

	// Copy all 46 base features
	check(Features.Num() == 46);
	for (int32 i = 0; i < 46; ++i)
	{
		OutObservations.Values[i] = Features[i];
	}

	// Append strategy one-hot encoding (4 features) from MCTS assignment
	EStrategyType AssignedStrategy = FollowerAgent->GetAssignedStrategy();
	int32 StrategyIdx = static_cast<int32>(AssignedStrategy);

	// Initialize strategy one-hot to [0, 0, 0, 0]
	for (int32 i = 46; i < 50; ++i)
	{
		OutObservations.Values[i] = 0.0f;
	}

	// Set the assigned strategy to 1.0
	if (StrategyIdx >= 0 && StrategyIdx < 4)
	{
		OutObservations.Values[46 + StrategyIdx] = 1.0f;
	}
	else
	{
		// Fallback to Assault if invalid
		OutObservations.Values[46] = 1.0f;
	}
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
