// TacticalObserver.cpp - Schola observer for 68-feature tactical observation (v6.0)

#include "Schola/Observers/TacticalObserver.h"
#include "Actor/FollowerCharacter.h"
#include "Observation/ObservationElement.h"
#include "Common/Spaces/BoxSpace.h"
#include "Common/Points/BoxPoint.h"


UTacticalObserver::UTacticalObserver()
{
	// Build observation space (56 continuous features: 52 base + 4 strategy one-hot)
	TArray<FBoxSpaceDimension> Dimensions;
	Dimensions.Reserve(56);

	// 52 base features (v9.0: +6 objective context): normalized to [-1, 1] or [0, 1]
	for (int32 i = 0; i < 52; ++i)
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


void UTacticalObserver::SetFollowerAgent(AActor* OwnerAgent)
{
	if (!OwnerAgent)
	{
		return;
	}

	AFollowerCharacter* Follower = Cast<AFollowerCharacter>(OwnerAgent);

	FollowerAgent = Follower;
}


FBoxSpace UTacticalObserver::GetObservationSpace() const
{
	return CachedObservationSpace;
}

void UTacticalObserver::CollectObservations(FBoxPoint& OutObservations)
{
	static int32 CallCount = 0;
	CallCount++;

	OutObservations.Values.SetNum(56);  // v9.0: 52 base features + 4 strategy one-hot
	// CRITICAL: Add safety checks to prevent crash during initialization
	if (!FollowerAgent || !FollowerAgent->IsValidLowLevel() || !FollowerAgent->GetOwner())
	{
		if (CallCount % 100 == 1) // Log every 100th call to avoid spam
		{
			UE_LOG(LogTemp, Error, TEXT("[TacticalObserver] CollectObservations called but FollowerAgent is NULL or invalid! (Call #%d)"), CallCount);
		}
		// Return zeros if no follower (52 base + 4 strategy = 56)
		for (int32 i = 0; i < 56; ++i)
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

	// v9.0: Get observation from follower (52 base features)
	// Includes: Position(3) + Health(1) + EnemyDist(1) + Raycasts(16) +
	//           EnemyInfo(16) + Tactical(4) + Support(5) + Objectives(6) = 52 features
	const FObservationElement& Obs = FollowerAgent->GetLocalObservation();
	TArray<float> Features = Obs.ToFeatureVector();

	// Copy all 52 base features
	check(Features.Num() == 56);
	for (int32 i = 0; i < 56; ++i)
	{
		OutObservations.Values[i] = Features[i];
	}
}
