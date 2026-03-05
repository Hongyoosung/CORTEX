// MocTacticalObserver.cpp - MOC v10.2 Executor Agent Observer Implementation

#include "Schola/Observers/MocTacticalObserver.h"
#include "Schola/Trainers/MocTrainer.h"
#include "Schola/Components/ScholaMocAgent.h"
#include "Characters/MocCharacter.h"
#include "Team/TeamManager.h"
#include "Actors/CapturePoint.h"
#include "Common/Spaces/BoxSpace.h"
#include "Common/Points/BoxPoint.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

UMocTacticalObserver::UMocTacticalObserver()
{
	// Build observation space (54 continuous features: 48 base + 3 team composition + 3 strategy one-hot)
	TArray<FBoxSpaceDimension> Dimensions;
	Dimensions.Reserve(54);

	// 48 base features: normalized to [-1, 1] or [0, 1]
	for (int32 i = 0; i < 48; ++i)
	{
		FBoxSpaceDimension Dim;
		Dim.Low = -1.0f;
		Dim.High = 1.0f;
		Dimensions.Add(Dim);
	}

	// 3 team composition features: [0, 1] (num_assault/5, num_defend/5, num_support/5)
	for (int32 i = 0; i < 3; ++i)
	{
		FBoxSpaceDimension Dim;
		Dim.Low = 0.0f;
		Dim.High = 1.0f;
		Dimensions.Add(Dim);
	}

	// 3 strategy one-hot features: [0, 1] (mutually exclusive)
	for (int32 i = 0; i < 3; ++i)
	{
		FBoxSpaceDimension Dim;
		Dim.Low = 0.0f;
		Dim.High = 1.0f;
		Dimensions.Add(Dim);
	}

	CachedObservationSpace = FBoxSpace(Dimensions);

	// Initialize cached references
	CachedTrainer = nullptr;
	CachedCharacter = nullptr;
	ObservationCallCount = 0;
}

void UMocTacticalObserver::InitializeObserver()
{
	Super::InitializeObserver();

	// Path 1: Training mode — outer is AMocTrainer (AIController that possesses pawn)
	CachedTrainer = GetTypedOuter<AMocTrainer>();
	if (CachedTrainer)
	{
		UE_LOG(LogTemp, Log, TEXT("[MocTacticalObserver] Initialized (Training mode) for trainer: %s"), *CachedTrainer->GetName());
		return;
	}

	// Path 2: Inference mode — outer is UScholaMocAgent component on AMocCharacter (pawn)
	CachedCharacter = GetTypedOuter<AMocCharacter>();
	if (CachedCharacter)
	{
		UE_LOG(LogTemp, Log, TEXT("[MocTacticalObserver] Initialized (Inference mode) for character: %s"), *CachedCharacter->GetName());
		return;
	}

	UE_LOG(LogTemp, Error, TEXT("[MocTacticalObserver] Could not find AMocTrainer or AMocCharacter in outer chain! Observer will not function."));
}

void UMocTacticalObserver::ResetObserver()
{
	Super::ResetObserver();
	ObservationCallCount = 0;
}

FBoxSpace UMocTacticalObserver::GetObservationSpace() const
{
	return CachedObservationSpace;
}

void UMocTacticalObserver::CollectObservations(FBoxPoint& OutObservations)
{
	ObservationCallCount++;

	// === DIAGNOSTIC: Log first few observations to confirm pipeline is flowing ===
	if (ObservationCallCount <= 5)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MocTacticalObserver] CollectObservations() call #%d | Trainer=%s | Character=%s"),
			ObservationCallCount,
			CachedTrainer ? *CachedTrainer->GetName() : TEXT("NULL"),
			GetControlledCharacter() ? *GetControlledCharacter()->GetName() : TEXT("NULL"));
	}

	// Initialize output with correct size (54-dim: 48 base + 3 team composition + 3 strategy one-hot)
	OutObservations.Values.SetNum(54);

	// Safety check: Verify trainer and character are valid
	AMocCharacter* Character = GetControlledCharacter();
	if (!Character || !Character->IsValidLowLevel())
	{
		if (ObservationCallCount % DebugLogFrequency == 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MocTacticalObserver] Character invalid at observation #%d - returning zeros"), ObservationCallCount);
		}

		for (int32 i = 0; i < 54; ++i)
		{
			OutObservations.Values[i] = 0.0f;
		}
		return;
	}

	// 1. Gather 48-dim base observation
	FObservation BaseObs = GatherBaseObservation();
	TArray<float> BaseFeatures = BaseObs.ToArray();

	if (BaseFeatures.Num() != 48)
	{
		UE_LOG(LogTemp, Error, TEXT("[MocTacticalObserver] Base observation size mismatch! Expected 48, got %d"), BaseFeatures.Num());
		for (int32 i = 0; i < 54; ++i)
		{
			OutObservations.Values[i] = 0.0f;
		}
		return;
	}

	// Copy base features (48-dim)
	for (int32 i = 0; i < 48; ++i)
	{
		OutObservations.Values[i] = BaseFeatures[i];
	}

	// 2. Append team strategy composition (3-dim at indices 48-50)
	//    [num_assault/5, num_defend/5, num_support/5]
	{
		float CompositionCounts[3] = {0.0f, 0.0f, 0.0f}; // Assault, Defend, Support
		ATeamManager* TeamMgr = Character->GetTeamManager();
		if (TeamMgr)
		{
			TArray<AMocCharacter*> Teammates = TeamMgr->GetTeamAgents(Character->GetTeamID_Implementation());
			for (AMocCharacter* Mate : Teammates)
			{
				if (!Mate) continue;
				switch (Mate->GetCommandedStrategy())
				{
				case EStrategyType::Assault: CompositionCounts[0] += 1.0f; break;
				case EStrategyType::Defend:  CompositionCounts[1] += 1.0f; break;
				case EStrategyType::Support: CompositionCounts[2] += 1.0f; break;
				default: break;
				}
			}
		}
		OutObservations.Values[48] = CompositionCounts[0] / 5.0f;
		OutObservations.Values[49] = CompositionCounts[1] / 5.0f;
		OutObservations.Values[50] = CompositionCounts[2] / 5.0f;
	}

	// 3. Append commanded strategy as one-hot (3-dim at indices 51-53)
	EStrategyType CommandedStrategy = Character->GetCommandedStrategy();
	TArray<float> StrategyOneHot = EncodeStrategyOneHot(CommandedStrategy);

	for (int32 i = 0; i < 3; ++i)
	{
		OutObservations.Values[51 + i] = StrategyOneHot[i];
	}

	// Validate final observation
	if (bEnableDebugLogging && ObservationCallCount % DebugLogFrequency == 0)
	{
		if (ValidateObservation(OutObservations.Values))
		{
			UE_LOG(LogTemp, Log, TEXT("[MocTacticalObserver] Observation #%d collected successfully (Strategy: %s)"),
				ObservationCallCount,
				*UEnum::GetValueAsString(CommandedStrategy));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[MocTacticalObserver] Observation #%d validation FAILED!"), ObservationCallCount);
		}
	}

#if WITH_EDITORONLY_DATA
	DebugLastObservation = OutObservations.Values;
#endif
}

AMocCharacter* UMocTacticalObserver::GetControlledCharacter() const
{
	// Path 1: Training mode — get pawn from trainer
	if (CachedTrainer)
	{
		return Cast<AMocCharacter>(CachedTrainer->GetPawn());
	}

	// Path 2: Inference mode — direct character reference
	if (CachedCharacter)
	{
		return CachedCharacter;
	}

	return nullptr;
}

FObservation UMocTacticalObserver::GatherBaseObservation() const
{
	FObservation Obs;

	AMocCharacter* Character = GetControlledCharacter();
	if (!Character)
	{
		return Obs;
	}

	// Self state
	Obs.Position = Character->GetActorLocation();
	Obs.Health = Character->GetHealthPercentage_Implementation();
	Obs.Velocity = Character->GetVelocity();
	Obs.WeaponCooldown = Character->GetWeaponCooldown_Implementation();
	// CurrentStrategy and bIsAlive stored for reward logic, not included in ToArray
	Obs.CurrentStrategy = Character->GetCommandedStrategy();
	Obs.bIsAlive = Character->IsAlive_Implementation();

	TArray<AActor*> AllCharacters;
	UGameplayStatics::GetAllActorsOfClass(
		Character->GetWorld(),
		AMocCharacter::StaticClass(),
		AllCharacters
	);

	int32 AllyIndex = 0;
	int32 EnemyIndex = 0;
	const int32 MyTeamID = Character->GetTeamID_Implementation();
	const FVector MyLocation = Character->GetActorLocation();

	for (AActor* Actor : AllCharacters)
	{
		AMocCharacter* OtherChar = Cast<AMocCharacter>(Actor);
		if (!OtherChar || OtherChar == Character)
		{
			continue;
		}

		if (OtherChar->GetTeamID_Implementation() == MyTeamID)
		{
			// Ally (max 4) — always known, no LoS required
			if (AllyIndex < 4)
			{
				Obs.AllyPositions[AllyIndex] = OtherChar->GetActorLocation();
				Obs.AllyHealths[AllyIndex] = OtherChar->GetHealthPercentage_Implementation();
				AllyIndex++;
			}
		}
		else
		{
			// Enemy (max 5) — direct line-of-sight only, no FogOfWar
			if (EnemyIndex < 5)
			{
				const FVector ToEnemy = OtherChar->GetActorLocation() - MyLocation;
				const float Distance = ToEnemy.Size();
				bool bVisible = false;

				if (Distance < 8000.0f)
				{
					FHitResult HitResult;
					FCollisionQueryParams QueryParams;
					QueryParams.AddIgnoredActor(Character);

					bVisible = !Character->GetWorld()->LineTraceSingleByChannel(
						HitResult,
						MyLocation + FVector(0, 0, 90),
						OtherChar->GetActorLocation() + FVector(0, 0, 90),
						ECC_Visibility,
						QueryParams
					);
				}

				// Store actual position only when visible; ToArray zeros non-visible slots
				Obs.EnemyPositions[EnemyIndex] = bVisible ? OtherChar->GetActorLocation() : FVector::ZeroVector;
				Obs.EnemyVisible[EnemyIndex] = bVisible;
				EnemyIndex++;
			}
		}
	}

	// Map state: per-point capture ownership
	// Find capture points matching this character's EnvID
	{
		static const ECapturePointID PointOrder[] = {
			ECapturePointID::PointA,
			ECapturePointID::PointB,
			ECapturePointID::PointC,
			ECapturePointID::PointD,
			ECapturePointID::PointE
		};

		// Build a map of PointID -> CapturePoint for this environment
		TMap<ECapturePointID, const ACapturePoint*> EnvCapturePoints;

		// Scan world for capture points matching this agent's EnvID
		for (TActorIterator<ACapturePoint> It(Character->GetWorld()); It; ++It)
		{
			ACapturePoint* Point = *It;
			if (Point && Point->GetEnvID_Implementation() == Character->GetEnvID_Implementation())
			{
				EnvCapturePoints.Add(Point->PointID, Point);
			}
		}

		for (int32 i = 0; i < 5; ++i)
		{
			if (const ACapturePoint* const* FoundPoint = EnvCapturePoints.Find(PointOrder[i]))
			{
				const int32 PointOwner = (*FoundPoint)->GetOwningTeamID();
				if (PointOwner == MyTeamID)
				{
					Obs.CapturePointStatuses[i] = 1.0f;
				}
				else if (PointOwner == -1)
				{
					Obs.CapturePointStatuses[i] = 0.0f;
				}
				else
				{
					Obs.CapturePointStatuses[i] = -1.0f;
				}
			}
		}
	}
	// CapturePointStatuses defaults to all 0.0 (neutral) from constructor if no CPs found

	return Obs;
}

TArray<float> UMocTacticalObserver::EncodeStrategyOneHot(EStrategyType Strategy) const
{
	TArray<float> OneHot = {0.0f, 0.0f, 0.0f};

	// Map strategy to index
	switch (Strategy)
	{
	case EStrategyType::Assault:
		OneHot[0] = 1.0f;
		break;
	case EStrategyType::Defend:
		OneHot[1] = 1.0f;
		break;
	case EStrategyType::Support:
		OneHot[2] = 1.0f;
		break;
	default:
		UE_LOG(LogTemp, Warning, TEXT("[MocTacticalObserver] Unknown strategy type: %d"), static_cast<int32>(Strategy));
		OneHot[0] = 1.0f; // Default to Assault
		break;
	}

	return OneHot;
}

bool UMocTacticalObserver::ValidateObservation(const TArray<float>& Observation) const
{
	if (Observation.Num() != 54)
	{
		UE_LOG(LogTemp, Error, TEXT("[MocTacticalObserver] Invalid observation size: %d (expected 54)"), Observation.Num());
		return false;
	}

	for (int32 i = 0; i < Observation.Num(); ++i)
	{
		const float Value = Observation[i];

		// Check for NaN or infinity
		if (!FMath::IsFinite(Value))
		{
			UE_LOG(LogTemp, Error, TEXT("[MocTacticalObserver] Non-finite value at index %d: %f"), i, Value);
			return false;
		}

		// Check reasonable bounds (most values should be [-1, 1])
		if (FMath::Abs(Value) > 100.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MocTacticalObserver] Suspiciously large value at index %d: %f"), i, Value);
		}
	}

	return true;
}

#if WITH_EDITOR
void UMocTacticalObserver::SetDebugObservations(TPoint& Temp)
{
	// Schola editor utility - extract FBoxPoint for inspection
	if (Temp.IsType<FBoxPoint>())
	{
		FBoxPoint BoxPoint = Temp.Get<FBoxPoint>();
		DebugLastObservation = BoxPoint.Values;
	}
}
#endif
