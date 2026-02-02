#include "Combat/Components/AgentPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AIPerceptionSystem.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Team/Components/TeamLeaderComponent.h"
#include "Team/Components/TeamCommsComponent.h"
#include "Core/SimulationManagerGameMode.h"
#include "Combat/Components/HealthComponent.h"
#include "Actor/LeaderCharacter.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"

UAgentPerceptionComponent::UAgentPerceptionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f; // Update 10 times per second
	
	InitializePerception();
}

void UAgentPerceptionComponent::BeginPlay()
{
	Super::BeginPlay();

	

	// Bind perception callbacks
	OnPerceptionUpdated.AddDynamic(this, &UAgentPerceptionComponent::OnPerceptionUpdatedCallback);
	OnTargetPerceptionUpdated.AddDynamic(this, &UAgentPerceptionComponent::OnTargetPerceivedCallback);

	// Cache references
	CachedSimulationManager = GetSimulationManager();
	CachedFollowerComponent = GetFollowerComponent();
}

void UAgentPerceptionComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateTrackedEnemies();

	// Auto-update observation if enabled
	if (bAutoUpdateObservation && CachedFollowerComponent)
	{
		const float CurrentTime = GetWorld()->GetTimeSeconds();
		if (ObservationUpdateInterval <= 0.0f ||
			(CurrentTime - LastObservationUpdateTime) >= ObservationUpdateInterval)
		{
			FObservationElement Observation = CachedFollowerComponent->GetLocalObservation();
			UpdateObservationWithEnemies(Observation);
			LastObservationUpdateTime = CurrentTime;
		}
	}

	// Debug visualization
	if (bDrawDebugInfo)
	{
		const AActor* Owner = GetOwner();
		if (!Owner) return;

		const FVector OwnerLocation = Owner->GetActorLocation();
		const FRotator OwnerRotation = Owner->GetActorRotation();

		// Draw sight radius
		DrawDebugCircle(GetWorld(), OwnerLocation, SightRadius, 32, FColor::Green, false, -1.0f, 0, 10.0f,
			FVector(0, 1, 0), FVector(1, 0, 0), false);

		// Draw vision cone
		const FVector ForwardVector = OwnerRotation.Vector();
		DrawDebugCone(GetWorld(), OwnerLocation, ForwardVector, SightRadius,
			FMath::DegreesToRadians(PeripheralVisionAngle),
			FMath::DegreesToRadians(PeripheralVisionAngle),
			16, FColor::Yellow, false, -1.0f, 0, 2.0f);

		// Draw detected enemies
		for (AActor* Enemy : TrackedEnemies)
		{
			if (Enemy)
			{
				DrawDebugLine(GetWorld(), OwnerLocation, Enemy->GetActorLocation(),
					FColor::Red, false, -1.0f, 0, 2.0f);
				DrawDebugSphere(GetWorld(), Enemy->GetActorLocation(), 50.0f,
					8, FColor::Red, false, -1.0f, 0, 2.0f);
			}
		}
	}
}

void UAgentPerceptionComponent::InitializePerception()
{

	UAISenseConfig_Sight* SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	// Configure sight sense
	if (SightConfig)
	{
		SightConfig->SightRadius = SightRadius;
		SightConfig->LoseSightRadius = LoseSightRadius;
		SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngle;
		SightConfig->DetectionByAffiliation.bDetectEnemies = true;
		SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
		SightConfig->DetectionByAffiliation.bDetectFriendlies = false;
		SightConfig->SetMaxAge(5.0f);

		ConfigureSense(*SightConfig);
		SetDominantSense(SightConfig->GetSenseImplementation());
	}
}

void UAgentPerceptionComponent::OnPerceptionUpdatedCallback(const TArray<AActor*>& UpdatedActors)
{
	// Update tracked enemies list
	UpdateTrackedEnemies();
}

void UAgentPerceptionComponent::OnTargetPerceivedCallback(AActor* Actor, FAIStimulus Stimulus)
{
	if (!Actor || !CachedSimulationManager) return;

	// Check if this is an enemy
	if (IsActorEnemy(Actor))
	{
		// Successfully sensed enemy
		if (Stimulus.WasSuccessfullySensed())
		{
			// Report to team leader if enabled and not already reported
			if (bAutoReportToLeader && !ReportedEnemies.Contains(Actor))
			{
				SignalEnemySpotted(Actor);
				ReportedEnemies.Add(Actor);
			}
		}
		else
		{
			ReportedEnemies.Remove(Actor);
		}
	}
}

void UAgentPerceptionComponent::UpdateTrackedEnemies()
{
	// Track previous enemy count for event detection
	int32 PreviousEnemyCount = TrackedEnemies.Num();
	TSet<AActor*> PreviousEnemies(TrackedEnemies);

	TrackedEnemies.Empty();

	// Get all currently perceived actors
	TArray<AActor*> PerceivedActors;
	GetCurrentlyPerceivedActors(nullptr, PerceivedActors);

	static int32 LogCounter = 0;
	bool bShouldLog = (++LogCounter % 60 == 0); // Log every 60 calls (~6 seconds at 10Hz)

	// Get owner's team ID for diagnostic logging
	int32 OwnerTeamID = -1;
	if (CachedSimulationManager && GetOwner())
	{
		OwnerTeamID = CachedSimulationManager->GetTeamIDForActor(GetOwner());
	}

	// Filter for enemies only (must be alive)
	for (AActor* Actor : PerceivedActors)
	{
		// Get actor's team ID for diagnostic
		int32 ActorTeamID = -1;
		if (CachedSimulationManager && Actor)
		{
			ActorTeamID = CachedSimulationManager->GetTeamIDForActor(Actor);
		}

		bool bIsEnemy = IsActorEnemy(Actor);

		if (bIsEnemy)
		{
			// Skip dead enemies
			UHealthComponent* HealthComp = Actor->FindComponentByClass<UHealthComponent>();
			if (HealthComp && HealthComp->IsDead())
			{
				// Remove from reported enemies so they can be re-reported if respawned
				ReportedEnemies.Remove(Actor);
				continue;
			}

			TrackedEnemies.Add(Actor);

			// EVENT: Fire OnEnemySpotted for newly detected enemies (v4.0 event-driven decisions)
			if (!PreviousEnemies.Contains(Actor))
			{
				OnEnemySpotted.Broadcast(Actor);
				UE_LOG(LogTemp, Warning, TEXT("🚨 [EVENT] '%s': Enemy spotted '%s' → Triggering immediate decision"),
					*GetOwner()->GetName(), *Actor->GetName());
			}

			if (bShouldLog)
			{
				float Distance = FVector::Dist(GetOwner()->GetActorLocation(), Actor->GetActorLocation());
				UE_LOG(LogTemp, Display, TEXT("    → Added to TrackedEnemies (Distance: %.1f cm)"), Distance);
			}
		}
	}

	// EVENT: Fire OnAllEnemiesLost when transitioning from enemies → no enemies
	if (PreviousEnemyCount > 0 && TrackedEnemies.Num() == 0)
	{
		OnAllEnemiesLost.Broadcast();
		UE_LOG(LogTemp, Warning, TEXT("🔵 [EVENT] '%s': All enemies lost → Triggering strategic decision"),
			*GetOwner()->GetName());
	}

	// Sort by distance (nearest first)
	const AActor* Owner = GetOwner();
	if (Owner)
	{
		const FVector OwnerLocation = Owner->GetActorLocation();
		TrackedEnemies.Sort([OwnerLocation](const AActor& A, const AActor& B)
		{
			const float DistA = FVector::DistSquared(OwnerLocation, A.GetActorLocation());
			const float DistB = FVector::DistSquared(OwnerLocation, B.GetActorLocation());
			return DistA < DistB;
		});
	}
}

TArray<AActor*> UAgentPerceptionComponent::GetDetectedEnemies() const
{
	// THREAD SAFETY: Return a copy to avoid race conditions with async MCTS
	return TArray<AActor*>(TrackedEnemies);
}

TArray<AActor*> UAgentPerceptionComponent::GetNearestEnemies(int32 MaxCount) const
{
	// THREAD SAFETY: Make a copy of TrackedEnemies to avoid race conditions with async MCTS
	TArray<AActor*> EnemiesCopy = TrackedEnemies;

	TArray<AActor*> Result;
	const int32 Count = FMath::Min(MaxCount, EnemiesCopy.Num());

	for (int32 i = 0; i < Count; ++i)
	{
		Result.Add(EnemiesCopy[i]);
	}

	return Result;
}

TArray<FEnemyObservation> UAgentPerceptionComponent::GetEnemyObservations(int32 MaxCount) const
{
	TArray<FEnemyObservation> Observations;

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		// Return empty observations
		for (int32 i = 0; i < MaxCount; ++i)
		{
			Observations.Add(FEnemyObservation());
		}
		return Observations;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	const FVector OwnerForward = Owner->GetActorForwardVector();

	// THREAD SAFETY: Make a copy of TrackedEnemies to avoid race conditions with async MCTS
	// TrackedEnemies can be modified on game thread while this is called from worker thread
	TArray<AActor*> EnemiesCopy = TrackedEnemies;

	// Build observations for nearest enemies
	const int32 NumEnemies = FMath::Min(MaxCount, EnemiesCopy.Num());
	for (int32 i = 0; i < NumEnemies; ++i)
	{
		AActor* Enemy = EnemiesCopy[i];
		if (!Enemy) continue;

		FEnemyObservation Obs;
		Obs.EnemyActor = Enemy;
		Obs.Distance = FVector::Dist(OwnerLocation, Enemy->GetActorLocation());
		Obs.RelativeAngle = GetRelativeAngleToTarget(Enemy);

		// Try to get enemy health (if available)
		// You can extend this with your health component interface
		Obs.Health = 100.0f; // Default

		Observations.Add(Obs);
	}

	// Fill remaining slots with empty observations
	for (int32 i = NumEnemies; i < MaxCount; ++i)
	{
		Observations.Add(FEnemyObservation());
	}

	return Observations;
}

bool UAgentPerceptionComponent::IsActorEnemy(AActor* Actor) const
{
	if (!Actor || !CachedSimulationManager) return false;

	AActor* Owner = GetOwner();
	if (!Owner) return false;

	// Filter out Leader characters - they should not be recognized as enemies
	if (Actor->IsA<ALeaderCharacter>())
	{
		return false;
	}

	return CachedSimulationManager->AreActorsEnemies(Owner, Actor);
}

int32 UAgentPerceptionComponent::GetVisibleEnemyCount() const
{
	return TrackedEnemies.Num();
}

TArray<FPerceptionResult> UAgentPerceptionComponent::GetAllPerceptionResults() const
{
	TArray<FPerceptionResult> Results;

	TArray<AActor*> PerceivedActors;
	GetCurrentlyPerceivedActors(nullptr, PerceivedActors);

	const AActor* Owner = GetOwner();
	if (!Owner) return Results;

	const FVector OwnerLocation = Owner->GetActorLocation();

	for (AActor* Actor : PerceivedActors)
	{
		if (!Actor) continue;

		FPerceptionResult Result;
		Result.DetectedActor = Actor;
		Result.bIsEnemy = IsActorEnemy(Actor);
		Result.Distance = FVector::Dist(OwnerLocation, Actor->GetActorLocation());
		Result.RelativeAngle = GetRelativeAngleToTarget(Actor);

		// Check if currently sensed
		const FActorPerceptionInfo* Info = GetActorInfo(*Actor);
		if (Info)
		{
			Result.bSuccessfullySensed = Info->HasAnyCurrentStimulus();
          
			// LastSensedStimuli 배열에서 가장 최근 stimulus의 시간 가져오기
			if (Info->LastSensedStimuli.Num() > 0)
			{
				float LatestTime = 0.f;
				for (const FAIStimulus& Stimulus : Info->LastSensedStimuli)
				{
					if (Stimulus.WasSuccessfullySensed())
					{
						const float StimulusAge = Stimulus.GetAge();
						const float StimulusTime = GetWorld()->GetTimeSeconds() - StimulusAge;
						LatestTime = FMath::Max(LatestTime, StimulusTime);
					}
				}
				Result.LastSenseTime = LatestTime;
			}
		}

		Results.Add(Result);
	}

	return Results;
}

void UAgentPerceptionComponent::UpdateObservationWithEnemies(FObservationElement& OutObservation)
{
	// Update enemy count
	OutObservation.VisibleEnemyCount = GetVisibleEnemyCount();

	// Update nearby enemies array
	OutObservation.NearbyEnemies = GetEnemyObservations(MaxTrackedEnemies);
}

TArray<ERaycastHitType> UAgentPerceptionComponent::BuildRaycastHitTypes(int32 NumRays, float RayLength)
{
	TArray<ERaycastHitType> HitTypes;
	HitTypes.Init(ERaycastHitType::None, NumRays);

	const AActor* Owner = GetOwner();
	if (!Owner || !GetWorld()) return HitTypes;

	const FVector StartLocation = Owner->GetActorLocation();
	const FRotator BaseRotation = Owner->GetActorRotation();

	// Trace parameters
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Owner);
	QueryParams.bTraceComplex = false;

	// Cast rays in 360 degrees
	const float AngleStep = 360.0f / NumRays;
	for (int32 i = 0; i < NumRays; ++i)
	{
		const float Angle = i * AngleStep;
		const FRotator RayRotation = BaseRotation + FRotator(0, Angle, 0);
		const FVector RayDirection = RayRotation.Vector();
		const FVector EndLocation = StartLocation + (RayDirection * RayLength);

		FHitResult HitResult;
		if (GetWorld()->LineTraceSingleByChannel(HitResult, StartLocation, EndLocation,
			ECC_Visibility, QueryParams))
		{
			AActor* HitActor = HitResult.GetActor();
			if (HitActor)
			{
				// Check what we hit
				if (IsActorEnemy(HitActor))
				{
					HitTypes[i] = ERaycastHitType::Enemy;
				}
				else if (HitActor->ActorHasTag(TEXT("Cover")))
				{
					HitTypes[i] = ERaycastHitType::Cover;
				}
				else if (HitActor->ActorHasTag(TEXT("Ally")))
				{
					HitTypes[i] = ERaycastHitType::Ally;
				}
				else
				{
					HitTypes[i] = ERaycastHitType::Wall;
				}
			}
			else
			{
				HitTypes[i] = ERaycastHitType::Wall;
			}
		}
	}

	return HitTypes;
}


void UAgentPerceptionComponent::SignalEnemySpotted(AActor* Enemy)
{
	if (!Enemy || !CachedFollowerComponent) return;

	UTeamCommsComponent* TeamComms = GetOwner()->FindComponentByClass<UTeamCommsComponent>();
	if (!TeamComms)
	{
		UE_LOG(LogTemp, Error, TEXT("🔵 [PERCEPTION] %s: No TeamCommsComponent found, cannot report enemy %s"),
			*GetOwner()->GetName(),
			*Enemy->GetName());
	}

	// 2. Get Leader via Comms
	UTeamLeaderComponent* TeamLeader = TeamComms->GetTeamLeader();

	if (!TeamLeader)
	{
		UE_LOG(LogTemp, Error, TEXT("🔵 [PERCEPTION] %s: No Team Leader found, cannot report enemy %s"),
			*GetOwner()->GetName(),
			*Enemy->GetName());
		return;
	}

	// Register enemy with leader
	TeamLeader->RegisterEnemy(Enemy);

	// Signal event to leader (high priority)
	CachedFollowerComponent->SignalEventToLeader(
		EStrategicEvent::EnemySpotted,
		Enemy,
		Enemy->GetActorLocation(),
		7 // High priority to trigger MCTS
	);

}

ASimulationManagerGameMode* UAgentPerceptionComponent::GetSimulationManager() const
{
	if (CachedSimulationManager)
	{
		return CachedSimulationManager;
	}

	UWorld* World = GetWorld();
	if (!World) return nullptr;

	AGameModeBase* GameMode = UGameplayStatics::GetGameMode(World);
	return Cast<ASimulationManagerGameMode>(GameMode);
}

UFollowerAgentComponent* UAgentPerceptionComponent::GetFollowerComponent() const
{
	if (CachedFollowerComponent)
	{
		return CachedFollowerComponent;
	}

	AActor* Owner = GetOwner();
	if (!Owner) return nullptr;

	return Owner->FindComponentByClass<UFollowerAgentComponent>();
}

float UAgentPerceptionComponent::GetRelativeAngleToTarget(AActor* Target) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Target) return 0.0f;

	const FVector OwnerLocation = Owner->GetActorLocation();
	const FVector OwnerForward = Owner->GetActorForwardVector();
	const FVector ToTarget = (Target->GetActorLocation() - OwnerLocation).GetSafeNormal();

	// Calculate signed angle (-180 to 180)
	const float DotProduct = FVector::DotProduct(OwnerForward, ToTarget);
	const float Angle = FMath::RadiansToDegrees(FMath::Acos(DotProduct));

	// Determine sign using cross product
	const FVector CrossProduct = FVector::CrossProduct(OwnerForward, ToTarget);
	const float Sign = (CrossProduct.Z >= 0.0f) ? 1.0f : -1.0f;

	return Angle * Sign;
}
