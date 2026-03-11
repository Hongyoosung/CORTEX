
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
	// Build observation space (V2 entity-centric: 167-dim padded flat)
	// Layout: Self(7) + Allies(8×5=40) + Enemies(8×5=40) + Bases(8×7=56) + Masks(8+8+8=24) = 167
	TArray<FBoxSpaceDimension> Dimensions;
	Dimensions.Reserve(DE_OBS_V2_DIM);

	for (int32 i = 0; i < DE_OBS_V2_DIM; ++i)
	{
		FBoxSpaceDimension Dim;
		Dim.Low  = -1.0f;
		Dim.High =  1.0f;
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

	if (ObservationCallCount <= 5)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DETacticalObserver] CollectObservations() call #%d | Trainer=%s | Character=%s"),
			ObservationCallCount,
			CachedTrainer ? *CachedTrainer->GetName() : TEXT("NULL"),
			GetControlledCharacter() ? *GetControlledCharacter()->GetName() : TEXT("NULL"));
	}

	// Initialize output (V2: 167-dim padded flat)
	OutObservations.Values.SetNum(DE_OBS_V2_DIM);

	ADECharacter* Character = GetControlledCharacter();
	if (!Character || !Character->IsValidLowLevel())
	{
		if (ObservationCallCount % DebugLogFrequency == 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[DETacticalObserver] Character invalid at observation #%d - returning zeros"), ObservationCallCount);
		}
		FMemory::Memzero(OutObservations.Values.GetData(), DE_OBS_V2_DIM * sizeof(float));
		return;
	}

	// Gather V2 entity-centric observation and serialise as padded flat array
	FDEObservationV2 ObsV2 = GatherObservationV2();
	TArray<float> FlatObs = ObsV2.ToFlatArray();

	if (FlatObs.Num() != DE_OBS_V2_DIM)
	{
		UE_LOG(LogTemp, Error, TEXT("[DETacticalObserver] V2 observation size mismatch! Expected %d, got %d"),
			DE_OBS_V2_DIM, FlatObs.Num());
		FMemory::Memzero(OutObservations.Values.GetData(), DE_OBS_V2_DIM * sizeof(float));
		return;
	}

	OutObservations.Values = MoveTemp(FlatObs);

	if (bEnableDebugLogging && ObservationCallCount % DebugLogFrequency == 0)
	{
		if (ValidateObservation(OutObservations.Values))
		{
			UE_LOG(LogTemp, Log, TEXT("[DETacticalObserver] V2 Observation #%d OK (allies=%d enemies=%d bases=%d)"),
				ObservationCallCount, ObsV2.AllyTokens.Num(), ObsV2.EnemyTokens.Num(), ObsV2.BaseTokens.Num());
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


FDEObservationV2 UDETacticalObserver::GatherObservationV2() const
{
	FDEObservationV2 Obs;

	ADECharacter* Character = GetControlledCharacter();
	if (!Character) return Obs;

	const FVector MyPos = Character->GetActorLocation();
	const FVector RelSelfPos = MyPos - CachedEnvironmentOrigin;
	const FVector Vel = Character->GetVelocity();

	// ---- Self token (7-dim) ----
	Obs.SelfToken.Features = {
		static_cast<float>(RelSelfPos.X / 7500.0),
		static_cast<float>(RelSelfPos.Y / 7500.0),
		static_cast<float>(RelSelfPos.Z / 1000.0),
		Character->GetHealthPercentage(), 
		static_cast<float>(Vel.X / 600.0),
		static_cast<float>(Vel.Y / 600.0),
		static_cast<float>(Vel.Z / 600.0)
	};

	const int32 MyTeamID = Character->GetTeamID_Implementation();

	ADEMatchManager* MatchMgr = Character->GetMatchManager();
	if (MatchMgr)
	{
		// ---- Ally tokens (5-dim each): [rel_pos/8000(3), health, alive] ----
		TArray<ADECharacter*> Allies = MatchMgr->GetTeamAgents(MyTeamID);
		for (ADECharacter* Ally : Allies)
		{
			if (!Ally || Ally == Character) continue;
			if (Obs.AllyTokens.Num() >= DE_MAX_ALLIES) break;

			const FVector RelPos = (Ally->GetActorLocation() - MyPos) / 8000.0f;
			FDEEntityToken Tok;
			Tok.Features = {
				static_cast<float>(RelPos.X),
				static_cast<float>(RelPos.Y),
				static_cast<float>(RelPos.Z),
				Ally->GetHealthPercentage(),
				Ally->IsAlive() ? 1.0f : 0.0f
			};
			Obs.AllyTokens.Add(MoveTemp(Tok));
		}

		// ---- Enemy tokens (5-dim each): [rel_pos/8000(3), visible, confidence=1] ----
		TArray<ADECharacter*> Enemies = MatchMgr->GetEnemyAgents(MyTeamID);
		for (ADECharacter* Enemy : Enemies)
		{
			if (!Enemy) continue;
			if (Obs.EnemyTokens.Num() >= DE_MAX_ENEMIES) break;

			const float Dist = FVector::Dist(Enemy->GetActorLocation(), MyPos);
			bool bVisible = false;
			if (Dist < 8000.0f)
			{
				FHitResult Hit;
				FCollisionQueryParams QP;
				QP.AddIgnoredActor(Character);
				bVisible = !Character->GetWorld()->LineTraceSingleByChannel(
					Hit,
					MyPos + FVector(0, 0, 90),
					Enemy->GetActorLocation() + FVector(0, 0, 90),
					ECC_Visibility, QP);
			}

			const FVector RelPos = bVisible ? (Enemy->GetActorLocation() - MyPos) / 8000.0f : FVector::ZeroVector;
			FDEEntityToken Tok;
			Tok.Features = {
				static_cast<float>(RelPos.X),
				static_cast<float>(RelPos.Y),
				static_cast<float>(RelPos.Z),
				bVisible ? 1.0f : 0.0f,
				bVisible ? 1.0f : 0.0f
			};
			Obs.EnemyTokens.Add(MoveTemp(Tok));
		}

		// ---- Base tokens (7-dim each) ----
		// [rel_pos/15000(3), ownership, capture_progress, is_assigned_target, strategic_value]
		const TArray<ADECapturePoint*>& CPs = MatchMgr->GetCapturePoints();
		for (int32 i = 0; i < CPs.Num() && Obs.BaseTokens.Num() < DE_MAX_BASES; ++i)
		{
			ADECapturePoint* CP = CPs[i];
			if (!CP) continue;

			const FVector RelPos = (CP->GetActorLocation() - MyPos) / 15000.0f;
			const int32 OwnerTeam = CP->GetTeamID_Implementation();
			float Ownership = 0.0f;
			if (OwnerTeam == MyTeamID) Ownership = 1.0f;
			else if (OwnerTeam >= 0)   Ownership = -1.0f;

			const float IsAssigned = (Character->AssignedBaseIndex == i) ? 1.0f : 0.0f;

			FDEEntityToken Tok;
			Tok.Features = {
				static_cast<float>(RelPos.X),
				static_cast<float>(RelPos.Y),
				static_cast<float>(RelPos.Z),
				Ownership,
				0.0f,
				IsAssigned,
				0.5f
			};
			Obs.BaseTokens.Add(MoveTemp(Tok));
		}
	}

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
	if (Observation.Num() != DE_OBS_V2_DIM)
	{
		UE_LOG(LogTemp, Error, TEXT("[DETacticalObserver] Invalid observation size: %d (expected %d)"),
			Observation.Num(), DE_OBS_V2_DIM);
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
