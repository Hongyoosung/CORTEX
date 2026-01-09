// TacticalObserver.cpp - Schola observer for 68-feature tactical observation (v6.0)

#include "Schola/TacticalObserver.h"
#include "Team/FollowerAgentComponent.h"
#include "Observation/ObservationElement.h"
#include "Common/Spaces/BoxSpace.h"
#include "Common/Points/BoxPoint.h"

UTacticalObserver::UTacticalObserver()
{
	// Build observation space (68 continuous features, v6.0: 64 base + 4 Mission context)
	TArray<FBoxSpaceDimension> Dimensions;
	Dimensions.Reserve(68);

	// All features normalized to [-1, 1] or [0, 1]
	for (int32 i = 0; i < 68; ++i)
	{
		FBoxSpaceDimension Dim;
		Dim.Low = -1.0f;
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

	OutObservations.Values.SetNum(68);  // v6.0: 68 features (64 base + 4 Mission
	// CRITICAL: Add safety checks to prevent crash during initialization
	if (!FollowerAgent || !FollowerAgent->IsValidLowLevel() || !FollowerAgent->GetOwner())
	{
		if (CallCount % 100 == 1) // Log every 100th call to avoid spam
		{
			UE_LOG(LogTemp, Error, TEXT("[TacticalObserver] CollectObservations called but FollowerAgent is NULL or invalid! (Call #%d)"), CallCount);
		}
		// Return zeros if no follower
		for (int32 i = 0; i < 68; ++i)
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

	// v6.0: Get observation from follower (now includes Mission context automatically)
	// Includes: Agent State(7) + Combat(1) + Perception(32) + Enemies(16) +
	//           Tactical(4) + Support Context(4) + Mission Context(4) = 68 features
	const FObservationElement& Obs = FollowerAgent->GetLocalObservation();
	TArray<float> Features = Obs.ToFeatureVector();

	// Copy all 68 features (v6.0: ToFeatureVector now includes Mission context)
	check(Features.Num() == 68);
	for (int32 i = 0; i < 68; ++i)
	{
		OutObservations.Values[i] = Features[i];
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
