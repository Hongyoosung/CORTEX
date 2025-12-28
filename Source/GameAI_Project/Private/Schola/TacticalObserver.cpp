// TacticalObserver.cpp - Schola observer for 70-feature tactical observation (v4.0)

#include "Schola/TacticalObserver.h"
#include "Team/FollowerAgentComponent.h"
#include "Observation/ObservationElement.h"
#include "Common/Spaces/BoxSpace.h"
#include "Common/Points/BoxPoint.h"

UTacticalObserver::UTacticalObserver()
{
	// Build observation space (74 continuous features = 70 tactical + 4 objective embedding, v4.0)
	TArray<FBoxSpaceDimension> Dimensions;
	Dimensions.Reserve(74);

	// All features normalized to [-1, 1] or [0, 1]
	for (int32 i = 0; i < 74; ++i)
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

	OutObservations.Values.SetNum(74);

	// CRITICAL: Add safety checks to prevent crash during initialization
	if (!FollowerAgent || !FollowerAgent->IsValidLowLevel() || !FollowerAgent->GetOwner())
	{
		if (CallCount % 100 == 1) // Log every 100th call to avoid spam
		{
			UE_LOG(LogTemp, Error, TEXT("[TacticalObserver] CollectObservations called but FollowerAgent is NULL or invalid! (Call #%d)"), CallCount);
		}
		// Return zeros if no follower
		for (int32 i = 0; i < 74; ++i)
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

	// Get observation from follower and convert to feature vector (70 features, v4.0)
	const FObservationElement& Obs = FollowerAgent->GetLocalObservation();
	TArray<float> Features = Obs.ToFeatureVector();

	// Copy all 70 tactical features
	check(Features.Num() == 70);
	for (int32 i = 0; i < 70; ++i)
	{
		OutObservations.Values[i] = Features[i];
	}

	// Add 4-dimensional objective embedding (one-hot encoding, v4.0 simplified)
	// Assault=0, Defend=1, Support=2, Retreat=3
	// None = all zeros
	for (int32 i = 0; i < 4; ++i)
	{
		OutObservations.Values[70 + i] = 0.0f;
	}

	// Get current objective from follower and encode as one-hot
	UObjective* CurrentObjective = FollowerAgent->GetCurrentObjective();
	if (CurrentObjective && CurrentObjective->IsActive())
	{
		// EObjectiveType enum (v4.0): None=0, Assault=1, Defend=2, Support=3, Retreat=4
		int32 ObjectiveIndex = static_cast<int32>(CurrentObjective->Type);

		// Map to 0-3 range (skip None=0 by subtracting 1)
		if (ObjectiveIndex >= 1 && ObjectiveIndex <= 4)
		{
			OutObservations.Values[70 + (ObjectiveIndex - 1)] = 1.0f;
		}
		// If None (0) or invalid: keep all zeros (already initialized)
	}
	// If no objective or inactive: keep all zeros (already initialized)
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
