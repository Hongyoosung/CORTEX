
#include "Schola/Observers/DETacticalObserver.h"
#include "Schola/Trainers/DETrainer.h"
#include "Schola/Components/DEScholaAgent.h"
#include "Schola/DEScholaEnvironment.h"
#include "Characters/DECharacter.h"
#include "Team/DEMatchManager.h"
#include "Actors/DECapturePoint.h"
#include "Common/Spaces/BoxSpace.h"
#include "Common/Points/BoxPoint.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

UDETacticalObserver::UDETacticalObserver()
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

void UDETacticalObserver::InitializeObserver()
{
	Super::InitializeObserver();

	// Path 1: Training mode — outer is ADETrainer (AIController that possesses pawn)
	CachedTrainer = GetTypedOuter<ADETrainer>();
	if (CachedTrainer)
	{
		// Cache environment origin for position normalization
		ADECharacter* Char = Cast<ADECharacter>(CachedTrainer->GetPawn());
		if (Char && Char->GetMatchManager())
		{
			TArray<AActor*> EnvActors;
			UGameplayStatics::GetAllActorsOfClass(CachedTrainer->GetWorld(), ADEScholaEnvironment::StaticClass(), EnvActors);
			for (AActor* Actor : EnvActors)
			{
				if (ADEScholaEnvironment* Env = Cast<ADEScholaEnvironment>(Actor))
				{
					if (Env->GetMatchManager() == Char->GetMatchManager())
					{
						CachedEnvironmentOrigin = Env->GetActorLocation();
						break;
					}
				}
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[DETacticalObserver] Initialized (Training mode) for trainer: %s (EnvOrigin: %s)"),
			*CachedTrainer->GetName(), *CachedEnvironmentOrigin.ToString());
		return;
	}

	// Path 2: Inference mode — outer is UDEScholaAgent component on ADECharacter (pawn)
	CachedCharacter = GetTypedOuter<ADECharacter>();
	if (CachedCharacter)
	{
		// Cache environment origin for position normalization
		if (CachedCharacter->GetMatchManager())
		{
			TArray<AActor*> EnvActors;
			UGameplayStatics::GetAllActorsOfClass(CachedCharacter->GetWorld(), ADEScholaEnvironment::StaticClass(), EnvActors);
			for (AActor* Actor : EnvActors)
			{
				if (ADEScholaEnvironment* Env = Cast<ADEScholaEnvironment>(Actor))
				{
					if (Env->GetMatchManager() == CachedCharacter->GetMatchManager())
					{
						CachedEnvironmentOrigin = Env->GetActorLocation();
						break;
					}
				}
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[DETacticalObserver] Initialized (Inference mode) for character: %s (EnvOrigin: %s)"),
			*CachedCharacter->GetName(), *CachedEnvironmentOrigin.ToString());
		return;
	}

	UE_LOG(LogTemp, Error, TEXT("[DETacticalObserver] Could not find ADETrainer or ADECharacter in outer chain! Observer will not function."));
}

void UDETacticalObserver::ResetObserver()
{
	Super::ResetObserver();
	ObservationCallCount = 0;
}

FBoxSpace UDETacticalObserver::GetObservationSpace() const
{
	return CachedObservationSpace;
}

void UDETacticalObserver::CollectObservations(FBoxPoint& OutObservations)
{
	ObservationCallCount++;

	// === DIAGNOSTIC: Log first few observations to confirm pipeline is flowing ===
	if (ObservationCallCount <= 5)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DETacticalObserver] CollectObservations() call #%d | Trainer=%s | Character=%s"),
			ObservationCallCount,
			CachedTrainer ? *CachedTrainer->GetName() : TEXT("NULL"),
			GetControlledCharacter() ? *GetControlledCharacter()->GetName() : TEXT("NULL"));
	}

	// Initialize output with correct size (54-dim: 48 base + 3 team composition + 3 strategy one-hot)
	OutObservations.Values.SetNum(54);

	// Safety check: Verify trainer and character are valid
	ADECharacter* Character = GetControlledCharacter();
	if (!Character || !Character->IsValidLowLevel())
	{
		if (ObservationCallCount % DebugLogFrequency == 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[DETacticalObserver] Character invalid at observation #%d - returning zeros"), ObservationCallCount);
		}

		for (int32 i = 0; i < 54; ++i)
		{
			OutObservations.Values[i] = 0.0f;
		}
		return;
	}

	// 1. Gather 48-dim base observation
	FDEObservation BaseObs = GatherBaseObservation();
	TArray<float> BaseFeatures = BaseObs.ToArray();

	if (BaseFeatures.Num() != 48)
	{
		UE_LOG(LogTemp, Error, TEXT("[DETacticalObserver] Base observation size mismatch! Expected 48, got %d"), BaseFeatures.Num());
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
		ADEMatchManager* MatchMgr = Character->GetMatchManager();
		if (MatchMgr)
		{
			TArray<ADECharacter*> Teammates = MatchMgr->GetTeamAgents(Character->GetTeamID_Implementation());
			for (ADECharacter* Mate : Teammates)
			{
				if (!Mate) continue;
				switch (Mate->GetCommandedStrategy())
				{
				case EDEStrategyType::Assault: CompositionCounts[0] += 1.0f; break;
				case EDEStrategyType::Defend:  CompositionCounts[1] += 1.0f; break;
				case EDEStrategyType::Support: CompositionCounts[2] += 1.0f; break;
				default: break;
				}
			}
		}
		OutObservations.Values[48] = CompositionCounts[0] / 5.0f;
		OutObservations.Values[49] = CompositionCounts[1] / 5.0f;
		OutObservations.Values[50] = CompositionCounts[2] / 5.0f;
	}

	// 3. Append commanded strategy as one-hot (3-dim at indices 51-53)
	EDEStrategyType CommandedStrategy = Character->GetCommandedStrategy();
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
			UE_LOG(LogTemp, Log, TEXT("[DETacticalObserver] Observation #%d collected successfully (Strategy: %s)"),
				ObservationCallCount,
				*UEnum::GetValueAsString(CommandedStrategy));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[DETacticalObserver] Observation #%d validation FAILED!"), ObservationCallCount);
		}
	}

#if WITH_EDITORONLY_DATA
	DebugLastObservation = OutObservations.Values;
#endif
}

ADECharacter* UDETacticalObserver::GetControlledCharacter() const
{
	// Path 1: Training mode — get pawn from trainer
	if (CachedTrainer)
	{
		return Cast<ADECharacter>(CachedTrainer->GetPawn());
	}

	// Path 2: Inference mode — direct character reference
	if (CachedCharacter)
	{
		return CachedCharacter;
	}

	return nullptr;
}

FDEObservation UDETacticalObserver::GatherBaseObservation() const
{
	FDEObservation Obs;

	ADECharacter* Character = GetControlledCharacter();
	if (!Character)
	{
		return Obs;
	}

	// Self state
	Obs.Position = Character->GetActorLocation();
	Obs.EnvironmentOrigin = CachedEnvironmentOrigin;
	Obs.Health = Character->GetHealthPercentage_Implementation();
	Obs.Velocity = Character->GetVelocity();
	Obs.WeaponCooldown = Character->GetWeaponCooldown_Implementation();
	// CurrentStrategy and bIsAlive stored for reward logic, not included in ToArray
	Obs.CurrentStrategy = Character->GetCommandedStrategy();
	Obs.bIsAlive = Character->IsAlive_Implementation();

	const int32 MyTeamID = Character->GetTeamID_Implementation();
	const int32 MyEnvID = Character->GetEnvID_Implementation();
	const FVector MyLocation = Character->GetActorLocation();

	int32 AllyIndex = 0;
	int32 EnemyIndex = 0;

	// Prefer MatchManager cached lists (O(1) lookup, already environment-scoped)
	ADEMatchManager* MatchMgr = Character->GetMatchManager();
	if (MatchMgr)
	{
		// Allies — from MatchManager (already filtered by team and environment)
		TArray<ADECharacter*> Allies = MatchMgr->GetTeamAgents(MyTeamID);
		for (ADECharacter* Ally : Allies)
		{
			if (!Ally || Ally == Character) continue;
			if (AllyIndex >= 4) break;

			Obs.AllyPositions[AllyIndex] = Ally->GetActorLocation();
			Obs.AllyHealths[AllyIndex] = Ally->GetHealthPercentage_Implementation();
			AllyIndex++;
		}

		// Enemies — from MatchManager (already filtered by environment)
		TArray<ADECharacter*> Enemies = MatchMgr->GetEnemyAgents(MyTeamID);
		for (ADECharacter* Enemy : Enemies)
		{
			if (!Enemy) continue;
			if (EnemyIndex >= 5) break;

			const float Distance = FVector::Dist(Enemy->GetActorLocation(), MyLocation);
			bool bVisible = false;

			if (Distance < 8000.0f)
			{
				FHitResult HitResult;
				FCollisionQueryParams QueryParams;
				QueryParams.AddIgnoredActor(Character);

				bVisible = !Character->GetWorld()->LineTraceSingleByChannel(
					HitResult,
					MyLocation + FVector(0, 0, 90),
					Enemy->GetActorLocation() + FVector(0, 0, 90),
					ECC_Visibility,
					QueryParams
				);
			}

			Obs.EnemyPositions[EnemyIndex] = bVisible ? Enemy->GetActorLocation() : FVector::ZeroVector;
			Obs.EnemyVisible[EnemyIndex] = bVisible;
			EnemyIndex++;
		}
	}
	else
	{
		// Fallback: world scan filtered by EnvID (MatchManager unavailable at init)
		TArray<AActor*> AllCharacters;
		UGameplayStatics::GetAllActorsOfClass(
			Character->GetWorld(),
			ADECharacter::StaticClass(),
			AllCharacters
		);

		for (AActor* Actor : AllCharacters)
		{
			ADECharacter* OtherChar = Cast<ADECharacter>(Actor);
			if (!OtherChar || OtherChar == Character) continue;

			// Filter by EnvID — only observe agents in the same environment
			if (OtherChar->GetEnvID_Implementation() != MyEnvID) continue;

			if (OtherChar->GetTeamID_Implementation() == MyTeamID)
			{
				if (AllyIndex < 4)
				{
					Obs.AllyPositions[AllyIndex] = OtherChar->GetActorLocation();
					Obs.AllyHealths[AllyIndex] = OtherChar->GetHealthPercentage_Implementation();
					AllyIndex++;
				}
			}
			else
			{
				if (EnemyIndex < 5)
				{
					const float Distance = FVector::Dist(OtherChar->GetActorLocation(), MyLocation);
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

					Obs.EnemyPositions[EnemyIndex] = bVisible ? OtherChar->GetActorLocation() : FVector::ZeroVector;
					Obs.EnemyVisible[EnemyIndex] = bVisible;
					EnemyIndex++;
				}
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

		// Build a map of PointID -> DECapturePoint for this environment
		TMap<ECapturePointID, const ADECapturePoint*> EnvCapturePoints;

		// Scan world for capture points matching this agent's EnvID
		for (TActorIterator<ADECapturePoint> It(Character->GetWorld()); It; ++It)
		{
			ADECapturePoint* Point = *It;
			if (Point && Point->GetEnvID_Implementation() == Character->GetEnvID_Implementation())
			{
				EnvCapturePoints.Add(Point->PointID, Point);
			}
		}

		for (int32 i = 0; i < 5; ++i)
		{
			if (const ADECapturePoint* const* FoundPoint = EnvCapturePoints.Find(PointOrder[i]))
			{
				const int32 PointOwner = (*FoundPoint)->GetTeamID_Implementation();
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

TArray<float> UDETacticalObserver::EncodeStrategyOneHot(EDEStrategyType Strategy) const
{
	TArray<float> OneHot = {0.0f, 0.0f, 0.0f};

	// Map strategy to index
	switch (Strategy)
	{
	case EDEStrategyType::Assault:
		OneHot[0] = 1.0f;
		break;
	case EDEStrategyType::Defend:
		OneHot[1] = 1.0f;
		break;
	case EDEStrategyType::Support:
		OneHot[2] = 1.0f;
		break;
	default:
		UE_LOG(LogTemp, Warning, TEXT("[DETacticalObserver] Unknown strategy type: %d"), static_cast<int32>(Strategy));
		OneHot[0] = 1.0f; // Default to Assault
		break;
	}

	return OneHot;
}

bool UDETacticalObserver::ValidateObservation(const TArray<float>& Observation) const
{
	if (Observation.Num() != 54)
	{
		UE_LOG(LogTemp, Error, TEXT("[DETacticalObserver] Invalid observation size: %d (expected 54)"), Observation.Num());
		return false;
	}

	for (int32 i = 0; i < Observation.Num(); ++i)
	{
		const float Value = Observation[i];

		// Check for NaN or infinity
		if (!FMath::IsFinite(Value))
		{
			UE_LOG(LogTemp, Error, TEXT("[DETacticalObserver] Non-finite value at index %d: %f"), i, Value);
			return false;
		}

		// Check reasonable bounds (most values should be [-1, 1])
		if (FMath::Abs(Value) > 100.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("[DETacticalObserver] Suspiciously large value at index %d: %f"), i, Value);
		}
	}

	return true;
}

#if WITH_EDITOR
void UDETacticalObserver::SetDebugObservations(TPoint& Temp)
{
	// Schola editor utility - extract FBoxPoint for inspection
	if (Temp.IsType<FBoxPoint>())
	{
		FBoxPoint BoxPoint = Temp.Get<FBoxPoint>();
		DebugLastObservation = BoxPoint.Values;
	}
}
#endif
