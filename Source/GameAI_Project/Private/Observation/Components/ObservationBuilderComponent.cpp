#include "Observation/Components/ObservationBuilderComponent.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Combat/Components/AgentPerceptionComponent.h"
#include "Combat/Components/HealthComponent.h"
#include "Team/ObjectiveActor.h"  // v9.0: For objective context

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
	UFollowerAgentComponent* FollowerComp = GetOwner()->FindComponentByClass<UFollowerAgentComponent>();
	TeamLeader = FollowerComp->GetTeamLeader();

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

			Observation.AllyDirection = FVector2D(ToAlly.X, ToAlly.Y);

		}
		else
		{
			Observation.AllyDistance = 0.0f;
		}
	}

	// v9.0: Populate objective context
	PopulateObjectiveContext(Observation);

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

//------------------------------------------------------------------------------
// OBJECTIVE CONTEXT (v9.0)
//------------------------------------------------------------------------------

void UObservationBuilderComponent::PopulateObjectiveContext(FObservationElement& Observation)
{
	AActor* Owner = GetOwner();
	if (!Owner || !TeamLeader)
	{
		// v9.0 FIX: Enhanced error logging with diagnostic information
		static TMap<UObservationBuilderComponent*, int32> WarningCounts;
		int32& Count = WarningCounts.FindOrAdd(this, 0);

		if (++Count % 100 == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("🔴 [OBS ERROR v9.0] Agent %s: TeamLeader=%s - Cannot populate objective context! (occurrences=%d)"),
				Owner ? *Owner->GetName() : TEXT("NULL"),
				TeamLeader ? TEXT("valid") : TEXT("NULL"),
				Count);

			// Diagnostic: Check why TeamLeader is NULL
			if (!TeamLeader && Owner)
			{
				UFollowerAgentComponent* FollowerComp = Owner->FindComponentByClass<UFollowerAgentComponent>();
				if (FollowerComp)
				{
					UTeamLeaderComponent* Leader = FollowerComp->GetTeamLeader();
					UE_LOG(LogTemp, Error, TEXT("   └─ Diagnostic: FollowerAgentComponent exists, GetTeamLeader()=%s"),
						Leader ? *Leader->GetOwner()->GetName() : TEXT("NULL"));

					if (!Leader)
					{
						UE_LOG(LogTemp, Error, TEXT("   └─ ISSUE: FollowerAgentComponent has NULL TeamLeader. Check BeginPlay initialization order."));
					}
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("   └─ ISSUE: NO FollowerAgentComponent found on owner! This ObservationBuilder is orphaned."));
				}
			}

			if (Count == 100)
			{
				UE_LOG(LogTemp, Error, TEXT("   └─ This will cause all objective distances to default to 1.0 (max distance)."));
				UE_LOG(LogTemp, Error, TEXT("   └─ Result: All agents in this environment will have IDENTICAL observations and rewards."));
			}
		}

		// Set default values (max distance - worst case for rewards)
		Observation.FriendlyObjectiveDistance = 1.0f;
		Observation.FriendlyObjectiveDirection = FVector2D::ZeroVector;
		Observation.HostileObjectiveDistance = 1.0f;
		Observation.HostileObjectiveDirection = FVector2D::ZeroVector;
		return;
	}

	const float MaxNormDistance = 10000.0f;  // Normalization constant
	const FVector AgentLocation = Owner->GetActorLocation();

	// Get objectives from team leader
	AObjectiveActor* FriendlyObjective = TeamLeader->GetFriendlyObjective();
	AObjectiveActor* HostileObjective = TeamLeader->GetHostileObjective();

	// v9.0 DEBUG: Log objective status periodically
	static int32 LogCounter = 0;
	if (++LogCounter % 200 == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("📍 [OBS] Agent %s: FriendlyObj=%s, HostileObj=%s"),
			*Owner->GetName(),
			FriendlyObjective ? *FriendlyObjective->GetName() : TEXT("NULL"),
			HostileObjective ? *HostileObjective->GetName() : TEXT("NULL"));
	}

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

		// v9.0 DEBUG: Log actual distances periodically
		if (LogCounter % 200 == 0)
		{
			UE_LOG(LogTemp, Display, TEXT("  Friendly: RawDist=%.0f cm, Normalized=%.3f"),
				Distance, Observation.FriendlyObjectiveDistance);
		}
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

		// v9.0 DEBUG: Log actual distances periodically
		if (LogCounter % 200 == 0)
		{
			UE_LOG(LogTemp, Display, TEXT("  Hostile: RawDist=%.0f cm, Normalized=%.3f"),
				Distance, Observation.HostileObjectiveDistance);
		}
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
