#include "Observation/Components/ObservationBuilderComponent.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "Combat/Components/AgentPerceptionComponent.h"
#include "Combat/Components/HealthComponent.h"

UObservationBuilderComponent::UObservationBuilderComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // No tick needed - called explicitly
}

void UObservationBuilderComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache components to avoid repeated FindComponentByClass calls
	CachedHealthComponent = GetOwner()->FindComponentByClass<UHealthComponent>();
	CachedPerceptionComponent = GetOwner()->FindComponentByClass<UAgentPerceptionComponent>();

	if (!CachedHealthComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ObservationBuilder] '%s': No HealthComponent found"),
			*GetOwner()->GetName());
	}

	if (!CachedPerceptionComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ObservationBuilder] '%s': No PerceptionComponent found"),
			*GetOwner()->GetName());
	}

	UE_LOG(LogTemp, Log, TEXT("ObservationBuilderComponent: Initialized on %s"), *GetOwner()->GetName());
}

//------------------------------------------------------------------------------
// OBSERVATION BUILDING
//------------------------------------------------------------------------------

void UObservationBuilderComponent::UpdateLocalObservation(const FObservationElement& NewObservation)
{
	// Store previous observation for RL experience collection
	PreviousObservation = LocalObservation;
	LocalObservation = NewObservation;
}

FObservationElement UObservationBuilderComponent::BuildLocalObservation()
{
	// Start profiling
	double StartTime = FPlatformTime::Seconds();

	FObservationElement Observation;

	AActor* Owner = GetOwner();
	if (!Owner) return Observation;

	// v5.0: Streamlined Agent State (7 features)
	Observation.Position = Owner->GetActorLocation();

	// v5.0: Health (normalized [0, 1])
	if (CachedHealthComponent)
	{
		Observation.AgentHealth = CachedHealthComponent->GetHealthPercentage();  // Already [0, 1]
	}

	// Get perception component for enemy and raycast info
	if (CachedPerceptionComponent)
	{
		// Update enemy information from perception
		CachedPerceptionComponent->UpdateObservationWithEnemies(Observation);

		// Calculate raycast distances (normalized)
		const FVector OwnerLocation = Owner->GetActorLocation();
		Observation.RaycastDistances.Init(1.0f, 16);

		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(Owner);

		const float MaxRayDistance = 5000.0f;
		const float AngleStep = 360.0f / 16;
		const FRotator OwnerRotation = Owner->GetActorRotation();  // Use local variable for raycast calculation

		for (int32 i = 0; i < 16; ++i)
		{
			const float Angle = i * AngleStep;
			const FRotator RayRotation = OwnerRotation + FRotator(0, Angle, 0);
			const FVector RayDirection = RayRotation.Vector();
			const FVector EndLocation = OwnerLocation + (RayDirection * MaxRayDistance);

			FHitResult HitResult;
			if (GetWorld()->LineTraceSingleByChannel(HitResult, OwnerLocation, EndLocation,
				ECC_Visibility, QueryParams))
			{
				// Normalized distance (0-1)
				Observation.RaycastDistances[i] = FMath::Clamp(HitResult.Distance / MaxRayDistance, 0.0f, 1.0f);
			}
		}
	}
	else
	{
		// Initialize empty if no perception component
		Observation.InitializeRaycasts(16);
	}

	// Get cover information from perception (cached to avoid expensive per-tick queries)
	if (CachedPerceptionComponent)
	{
		TArray<AActor*> Enemies = CachedPerceptionComponent->GetDetectedEnemies();
		if (Enemies.Num() > 0)
		{
			// Update cover cache if interval elapsed
			float CurrentTime = GetWorld()->GetTimeSeconds();
			if (CurrentTime - LastCoverQueryTime >= CoverQueryInterval)
			{
				FVector CoverLocation;
				float CoverDistance;
				bHasCachedCover = FindNearestCover(CoverLocation, CoverDistance, Enemies);
				if (bHasCachedCover)
				{
					CachedCoverLocation = CoverLocation;
					CachedCoverDistance = CoverDistance;
				}
				LastCoverQueryTime = CurrentTime;
			}

			// Use cached cover data
			const float MaxCoverDistance = 5000.0f;  // v5.0: normalization constant
			if (bHasCachedCover)
			{
				Observation.bHasCover = true;
				Observation.NearestCoverDistance = FMath::Clamp(CachedCoverDistance / MaxCoverDistance, 0.0f, 1.0f);

				// Calculate direction to cover (normalized 2D)
				FVector ToCover = CachedCoverLocation - Owner->GetActorLocation();
				ToCover.Z = 0; // Flatten to 2D
				ToCover.Normalize();
				Observation.CoverDirection = FVector2D(ToCover.X, ToCover.Y);
			}
			else
			{
				Observation.bHasCover = false;
				Observation.NearestCoverDistance = 1.0f;  // v5.0: normalized max distance
				Observation.CoverDirection = FVector2D::ZeroVector;
			}
		}
		else
		{
			// No enemies - no cover needed
			Observation.bHasCover = false;
			Observation.NearestCoverDistance = 1.0f;  // v5.0: normalized max distance
			Observation.CoverDirection = FVector2D::ZeroVector;
		}
	}

	// ========================================
	// v5.0: SUPPORT CONTEXT (4 features)
	// Find ally most in need of help for Support strategy
	// ========================================
	if (TeamLeader)
	{
		const float MaxAllyDistance = 5000.0f;  // Normalization constant
		const float AllyNeedsHelpThreshold = 0.5f;  // Health below 50% triggers help

		AActor* AllyInNeed = nullptr;
		float WorstAllyHealth = 1.0f;
		FVector AllyLocation = FVector::ZeroVector;

		// Find ally with lowest health
		for (AActor* Ally : TeamLeader->GetAliveFollowers())
		{
			if (!Ally || Ally == Owner) continue;

			UHealthComponent* AllyHealthComp = Ally->FindComponentByClass<UHealthComponent>();
			if (AllyHealthComp && AllyHealthComp->IsAlive())
			{
				float AllyHealthPct = AllyHealthComp->GetHealthPercentage();
				if (AllyHealthPct < WorstAllyHealth)
				{
					WorstAllyHealth = AllyHealthPct;
					AllyInNeed = Ally;
					AllyLocation = Ally->GetActorLocation();
				}
			}
		}

		if (AllyInNeed)
		{
			// Calculate distance and direction to ally
			FVector ToAlly = AllyLocation - Owner->GetActorLocation();
			float Distance = ToAlly.Size();
			Observation.AllyDistance = FMath::Clamp(Distance / MaxAllyDistance, 0.0f, 1.0f);

			// Calculate angle to ally (normalized [-1, 1] based on forward direction)
			ToAlly.Normalize();
			FVector Forward = Owner->GetActorForwardVector();
			float Angle = FMath::Atan2(
				FVector::CrossProduct(Forward, ToAlly).Z,
				FVector::DotProduct(Forward, ToAlly)
			);
		}
		else
		{
			Observation.AllyDistance = 0.0f;
		}
	}

	// End profiling
	double EndTime = FPlatformTime::Seconds();
	TotalObservationTime += static_cast<float>((EndTime - StartTime) * 1000.0); // Convert to ms

	return Observation;
}

//------------------------------------------------------------------------------
// COVER DETECTION
//------------------------------------------------------------------------------

bool UObservationBuilderComponent::FindNearestCover(FVector& OutCoverLocation, float& OutDistance, const TArray<AActor*>& Enemies)
{
	// Start profiling
	double StartTime = FPlatformTime::Seconds();

	AActor* Owner = GetOwner();
	if (!Owner || Enemies.Num() == 0)
	{
		return false;
	}

	const FVector AgentLocation = Owner->GetActorLocation();
	const FVector AgentEyeLevel = AgentLocation + FVector(0, 0, 150.0f);
	UWorld* World = GetWorld();

	// Calculate average enemy direction
	FVector AvgEnemyDirection = FVector::ZeroVector;
	for (AActor* Enemy : Enemies)
	{
		if (Enemy)
		{
			FVector ToEnemy = Enemy->GetActorLocation() - AgentLocation;
			ToEnemy.Z = 0; // Flatten to 2D
			ToEnemy.Normalize();
			AvgEnemyDirection += ToEnemy;
		}
	}
	AvgEnemyDirection.Normalize();

	// Sample points in a circle around the agent (perpendicular to enemy direction preferred)
	float BestScore = -1.0f;
	FVector BestCoverLocation = FVector::ZeroVector;
	const float AngleStep = 360.0f / CoverSearchSamples;

	for (int32 i = 0; i < CoverSearchSamples; ++i)
	{
		float Angle = i * AngleStep;
		FRotator SearchRotation = FRotator(0, Angle, 0);
		FVector SearchDirection = SearchRotation.Vector();
		FVector SearchEnd = AgentLocation + (SearchDirection * CoverSearchRadius);

		// Raycast to find obstacles
		FHitResult HitResult;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(Owner);
		Params.bTraceComplex = false;

		if (World->LineTraceSingleByChannel(HitResult, AgentLocation, SearchEnd, ECC_WorldStatic, Params))
		{
			// Found an obstacle - check if it provides cover from enemies
			FVector PotentialCover = HitResult.Location;
			FVector PotentialCoverEye = PotentialCover + FVector(0, 0, 150.0f);

			// Check height (must be tall enough for cover)
			if (HitResult.Normal.Z < -0.5f || FMath::Abs(HitResult.ImpactPoint.Z - AgentLocation.Z) < MinCoverHeight)
			{
				continue; // Ground hit or too low
			}

			// Check if this position blocks LOS to enemies
			int32 BlockedEnemies = 0;
			for (AActor* Enemy : Enemies)
			{
				if (!Enemy) continue;

				FVector EnemyEyeLevel = Enemy->GetActorLocation() + FVector(0, 0, 150.0f);
				FHitResult LOSResult;
				FCollisionQueryParams LOSParams;
				LOSParams.bTraceComplex = false;

				if (World->LineTraceSingleByChannel(LOSResult, PotentialCoverEye, EnemyEyeLevel, ECC_Visibility, LOSParams))
				{
					BlockedEnemies++; // LOS blocked
				}
			}

			// Score this cover position
			float Distance = FVector::Dist(AgentLocation, PotentialCover);
			float CoverRatio = static_cast<float>(BlockedEnemies) / Enemies.Num();
			float DistanceScore = 1.0f - FMath::Clamp(Distance / CoverSearchRadius, 0.0f, 1.0f);

			// Prefer cover that blocks more enemies and is closer
			float Score = (CoverRatio * 0.7f) + (DistanceScore * 0.3f);

			if (Score > BestScore)
			{
				BestScore = Score;
				BestCoverLocation = PotentialCover;
			}
		}
	}

	// End profiling
	double EndTime = FPlatformTime::Seconds();
	TotalCoverQueryTime += static_cast<float>((EndTime - StartTime) * 1000.0); // Convert to ms
	CoverQueriesThisEpisode++;

	// Return best cover if found
	if (BestScore > 0.0f)
	{
		OutCoverLocation = BestCoverLocation;
		OutDistance = FVector::Dist(AgentLocation, BestCoverLocation);
		return true;
	}

	return false;
}

//------------------------------------------------------------------------------
// RESET
//------------------------------------------------------------------------------

void UObservationBuilderComponent::ResetEpisode()
{
	PreviousObservation = FObservationElement();
	LocalObservation = FObservationElement();

	// Reset profiling stats
	TotalObservationTime = 0.0f;
	TotalCoverQueryTime = 0.0f;
	CoverQueriesThisEpisode = 0;

	// Clear cover cache
	bHasCachedCover = false;
	CachedCoverLocation = FVector::ZeroVector;
	CachedCoverDistance = 9999.0f;
	LastCoverQueryTime = 0.0f;
}
