// MocTacticalObserver.cpp - MOC v10.2 Executor Agent Observer Implementation

#include "Schola/Observers/MocTacticalObserver.h"
#include "Schola/Trainers/MocTrainer.h"
#include "Characters/MocCharacter.h"
#include "Common/Spaces/BoxSpace.h"
#include "Common/Points/BoxPoint.h"
#include "Kismet/GameplayStatics.h"

UMocTacticalObserver::UMocTacticalObserver()
{
	// Build observation space (55 continuous features)
	TArray<FBoxSpaceDimension> Dimensions;
	Dimensions.Reserve(55);

	// 52 base features: normalized to [-1, 1] or [0, 1]
	// Most features are position/velocity-based, normalized for neural network
	for (int32 i = 0; i < 52; ++i)
	{
		FBoxSpaceDimension Dim;
		Dim.Low = -1.0f;
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
	ObservationCallCount = 0;
}

void UMocTacticalObserver::InitializeObserver()
{
	Super::InitializeObserver();

	// Cache trainer reference (owner should be AMocTrainer)
	CachedTrainer = GetTypedOuter<AMocTrainer>();
	if (!CachedTrainer)
	{
		UE_LOG(LogTemp, Error, TEXT("[MocTacticalObserver] Owner is not AMocTrainer! Observer will not function correctly."));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[MocTacticalObserver] Initialized for trainer: %s"), *CachedTrainer->GetName());
	}
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

	// Initialize output with correct size (55-dim)
	OutObservations.Values.SetNum(55);

	// Safety check: Verify trainer and character are valid
	AMocCharacter* Character = GetControlledCharacter();
	if (!Character || !Character->IsValidLowLevel())
	{
		if (ObservationCallCount % DebugLogFrequency == 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MocTacticalObserver] Character invalid at observation #%d - returning zeros"), ObservationCallCount);
		}

		// Return zero observation if character not available
		for (int32 i = 0; i < 55; ++i)
		{
			OutObservations.Values[i] = 0.0f;
		}
		return;
	}

	// 1. Gather 52-dim base observation
	FObservation BaseObs = GatherBaseObservation();
	TArray<float> BaseFeatures = BaseObs.ToArray();

	// Validate base observation size
	if (BaseFeatures.Num() != 52)
	{
		UE_LOG(LogTemp, Error, TEXT("[MocTacticalObserver] Base observation size mismatch! Expected 52, got %d"), BaseFeatures.Num());
		for (int32 i = 0; i < 55; ++i)
		{
			OutObservations.Values[i] = 0.0f;
		}
		return;
	}

	// Copy base features (52-dim)
	for (int32 i = 0; i < 52; ++i)
	{
		OutObservations.Values[i] = BaseFeatures[i];
	}

	// 2. Append commanded strategy as one-hot (3-dim)
	EStrategyType CommandedStrategy = Character->GetCommandedStrategy();
	TArray<float> StrategyOneHot = EncodeStrategyOneHot(CommandedStrategy);

	for (int32 i = 0; i < 3; ++i)
	{
		OutObservations.Values[52 + i] = StrategyOneHot[i];
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
	if (!CachedTrainer)
	{
		return nullptr;
	}

	// Get pawn from trainer (should be AMocCharacter)
	APawn* ControlledPawn = CachedTrainer->GetPawn();
	return Cast<AMocCharacter>(ControlledPawn);
}

FObservation UMocTacticalObserver::GatherBaseObservation() const
{
	FObservation Obs;

	AMocCharacter* Character = GetControlledCharacter();
	if (!Character)
	{
		return Obs; // Return default-initialized observation
	}

	// Self state (10-dim)
	Obs.Position = Character->GetActorLocation();
	Obs.Health = Character->GetHealthPercentage_Implementation();
	Obs.Velocity = Character->GetVelocity();
	Obs.WeaponCooldown = Character->GetWeaponCooldown_Implementation();
	Obs.CurrentStrategy = Character->GetCommandedStrategy();
	Obs.bIsAlive = Character->IsAlive_Implementation();

	// Gather team information (allies + enemies)
	TArray<AActor*> AllCharacters;
	UGameplayStatics::GetAllActorsOfClass(
		Character->GetWorld(),
		AMocCharacter::StaticClass(),
		AllCharacters
	);

	int32 AllyIndex = 0;
	int32 EnemyIndex = 0;
	const int32 MyTeamID = Character->GetTeamID_Implementation();

	for (AActor* Actor : AllCharacters)
	{
		AMocCharacter* OtherChar = Cast<AMocCharacter>(Actor);
		if (!OtherChar || OtherChar == Character)
		{
			continue;
		}

		if (OtherChar->GetTeamID_Implementation() == MyTeamID)
		{
			// Ally (max 4)
			if (AllyIndex < 4)
			{
				Obs.AllyPositions[AllyIndex] = OtherChar->GetActorLocation();
				Obs.AllyHealths[AllyIndex] = OtherChar->GetHealthPercentage_Implementation();
				Obs.AllyStrategies[AllyIndex] = OtherChar->GetCommandedStrategy();
				AllyIndex++;
			}
		}
		else
		{
			// Enemy (max 5)
			if (EnemyIndex < 5)
			{
				Obs.EnemyPositions[EnemyIndex] = OtherChar->GetActorLocation();

				// Simple visibility check (line of sight within vision range)
				FVector ToEnemy = OtherChar->GetActorLocation() - Character->GetActorLocation();
				float Distance = ToEnemy.Size();
				bool bVisible = false;

				if (Distance < 8000.0f) // Vision range
				{
					FHitResult HitResult;
					FCollisionQueryParams QueryParams;
					QueryParams.AddIgnoredActor(Character);

					bVisible = !Character->GetWorld()->LineTraceSingleByChannel(
						HitResult,
						Character->GetActorLocation() + FVector(0, 0, 90), // Eye height
						OtherChar->GetActorLocation() + FVector(0, 0, 90),
						ECC_Visibility,
						QueryParams
					);
				}

				Obs.EnemyVisible[EnemyIndex] = bVisible;
				EnemyIndex++;
			}
		}
	}

	// Map state (2-dim)
	// TODO: Implement capture point balance calculation from game mode
	Obs.CapturePointBalance = 0;
	Obs.TimeRemaining = 1.0f;

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
	if (Observation.Num() != 55)
	{
		UE_LOG(LogTemp, Error, TEXT("[MocTacticalObserver] Invalid observation size: %d (expected 55)"), Observation.Num());
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
