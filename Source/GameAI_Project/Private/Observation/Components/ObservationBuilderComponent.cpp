#include "Observation/Components/ObservationBuilderComponent.h"
#include "Combat/Components/AgentPerceptionComponent.h"
#include "Combat/Components/HealthComponent.h"
#include "Team/ObjectiveActor.h"  

UObservationBuilderComponent::UObservationBuilderComponent()
{
	PrimaryComponentTick.bCanEverTick = false; 
}

void UObservationBuilderComponent::BeginPlay()
{
	Super::BeginPlay();


	UE_LOG(LogTemp, Log, TEXT("[ObservationBuilder v9.0] '%s': Waiting for dependency injection"), *GetOwner()->GetName());
}

//------------------------------------------------------------------------------
// v9.0 PHASE 3: DEPENDENCY INJECTION
//------------------------------------------------------------------------------

void UObservationBuilderComponent::SetHealthComponent(UHealthComponent* Health)
{
	CachedHealthComponent = Health;

	if (CachedHealthComponent)
	{
		UE_LOG(LogTemp, Log, TEXT("[ObservationBuilder v9.0] '%s': Injected HealthComponent"),
			*GetOwner()->GetName());
	}
}

void UObservationBuilderComponent::SetPerceptionComponent(UAgentPerceptionComponent* Perception)
{
	CachedPerceptionComponent = Perception;

	if (CachedPerceptionComponent)
	{
		UE_LOG(LogTemp, Log, TEXT("[ObservationBuilder v9.0] '%s': Injected PerceptionComponent"),
			*GetOwner()->GetName());
	}
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
	// SUPPORT CONTEXT (4 features)
	// Find ally most in need of help for Support strategy
	// Uses CachedTeamObservation (injected via UpdateTeamIntel) instead of direct TeamLeader access
	// ========================================
	{
		const float MaxAllyDistance = 5000.0f;  // Normalization constant
		const float AllyNeedsHelpThreshold = 0.5f;  // Health below 50% triggers help

		// Use cached team observation data (injected from TeamLeader via UpdateTeamIntel)
		// This eliminates direct dependency on TeamLeader and follows SRP
		if (CachedTeamObservation.FollowerObservations.Num() > 0)
		{
			float WorstAllyHealth = 1.0f;
			FVector AllyLocation = FVector::ZeroVector;
			bool bFoundAllyInNeed = false;

			// Find ally with lowest health from cached team observation
			for (const FObservationElement& AllyObs : CachedTeamObservation.FollowerObservations)
			{
				// Skip self (compare positions)
				if (FVector::Distance(AllyObs.Position, Owner->GetActorLocation()) < 10.0f)
				{
					continue;
				}

				// Check if this ally needs help (lowest health)
				if (AllyObs.AgentHealth < WorstAllyHealth && AllyObs.AgentHealth < AllyNeedsHelpThreshold)
				{
					WorstAllyHealth = AllyObs.AgentHealth;
					AllyLocation = AllyObs.Position;
					bFoundAllyInNeed = true;
				}
			}

			if (bFoundAllyInNeed)
			{
				// Indicate ally needs help
				Observation.bAllyNeedsHelp = true;
				Observation.AllyHealth = WorstAllyHealth;

				// Calculate distance and direction to ally
				FVector ToAlly = AllyLocation - Owner->GetActorLocation();
				float Distance = ToAlly.Size();
				Observation.AllyDistance = FMath::Clamp(Distance / MaxAllyDistance, 0.0f, 1.0f);

				// Normalized 2D direction
				ToAlly.Z = 0; // Flatten to 2D
				ToAlly.Normalize();
				Observation.AllyDirection = FVector2D(ToAlly.X, ToAlly.Y);

				UE_LOG(LogTemp, VeryVerbose, TEXT("[OBS v9.0] %s: Ally needs help (Health=%.2f, Dist=%.2f)"),
					*Owner->GetName(), WorstAllyHealth, Distance);
			}
			else
			{
				// No ally needs help
				Observation.bAllyNeedsHelp = false;
				Observation.AllyDistance = 0.0f;
				Observation.AllyHealth = 1.0f;
				Observation.AllyDirection = FVector2D::ZeroVector;
			}
		}
		else
		{
			// No team observation available yet - use defaults
			Observation.bAllyNeedsHelp = false;
			Observation.AllyDistance = 0.0f;
			Observation.AllyHealth = 1.0f;
			Observation.AllyDirection = FVector2D::ZeroVector;
		}
	}

	// v9.0: Populate objective context
	PopulateObjectiveContext(Observation);

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


	// Clear cover cache
	bHasCachedCover = false;
	CachedCoverLocation = FVector::ZeroVector;
	CachedCoverDistance = 9999.0f;
	LastCoverQueryTime = 0.0f;
}

//------------------------------------------------------------------------------
// OBJECTIVE CONTEXT (v9.0)
//------------------------------------------------------------------------------

void UObservationBuilderComponent::PopulateObjectiveContext(FObservationElement& Observation)
{
	AActor* Owner = GetOwner();
	if (!Owner) return;


	const float MaxNormDistance = 10000.0f;
	const FVector AgentLocation = Owner->GetActorLocation();

	// [변경] 멤버 변수 사용
	AObjectiveActor* FriendlyObjective = CachedFriendlyObjective;
	AObjectiveActor* HostileObjective = CachedHostileObjective;

	// Populate friendly objective context
	if (FriendlyObjective)
	{
		FVector ToFriendly = FriendlyObjective->GetActorLocation() - AgentLocation;
		float Distance = ToFriendly.Size();

		// Normalized distance [0, 1]
		Observation.FriendlyObjectiveDistance = FMath::Clamp(Distance / MaxNormDistance, 0.0f, 1.0f);

		// Normalized 2D direction
		ToFriendly.Z = 0; // Flatten to 2D
		ToFriendly.Normalize();
		Observation.FriendlyObjectiveDirection = FVector2D(ToFriendly.X, ToFriendly.Y);
	}
	else
	{
		// Default values if objective not found
		Observation.FriendlyObjectiveDistance = 1.0f;
		Observation.FriendlyObjectiveDirection = FVector2D::ZeroVector;
	}

	// Populate hostile objective context
	if (HostileObjective)
	{
		FVector ToHostile = HostileObjective->GetActorLocation() - AgentLocation;
		float Distance = ToHostile.Size();

		// Normalized distance [0, 1]
		Observation.HostileObjectiveDistance = FMath::Clamp(Distance / MaxNormDistance, 0.0f, 1.0f);

		// Normalized 2D direction
		ToHostile.Z = 0; // Flatten to 2D
		ToHostile.Normalize();
		Observation.HostileObjectiveDirection = FVector2D(ToHostile.X, ToHostile.Y);
	}
	else
	{
		// Default values if objective not found
		Observation.HostileObjectiveDistance = 1.0f;
		Observation.HostileObjectiveDirection = FVector2D::ZeroVector;
	}

	// v9.0: Enhanced periodic logging with diagnostic information
	static TMap<UObservationBuilderComponent*, float> LastLogTimes;
	float& LastLogTime = LastLogTimes.FindOrAdd(this, 0.0f);
	float CurrentTime = GetWorld()->GetTimeSeconds();

	if (CurrentTime - LastLogTime > 5.0f)
	{
		LastLogTime = CurrentTime;

		// Check if values are at default (indicates missing objectives)
		bool bHasDefaults = (Observation.FriendlyObjectiveDistance >= 0.99f || Observation.HostileObjectiveDistance >= 0.99f);

		if (bHasDefaults)
		{
			UE_LOG(LogTemp, Error, TEXT("⚠️ [OBS CONTEXT v9.0] %s: FriendlyDist=%.3f, HostileDist=%.3f - USING DEFAULTS (objectives missing!)"),
				*Owner->GetName(),
				Observation.FriendlyObjectiveDistance,
				Observation.HostileObjectiveDistance);
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("✅ [OBS CONTEXT v9.0] %s: FriendlyDist=%.3f, HostileDist=%.3f"),
				*Owner->GetName(),
				Observation.FriendlyObjectiveDistance,
				Observation.HostileObjectiveDistance);
		}
	}
}

void UObservationBuilderComponent::SetObjectives(AObjectiveActor* Friendly, AObjectiveActor* Hostile)
{
	CachedFriendlyObjective = Friendly;
	CachedHostileObjective = Hostile;
}

void UObservationBuilderComponent::UpdateTeamIntel(const FTeamObservation& TeamObs)
{
	CachedTeamObservation = TeamObs;
}