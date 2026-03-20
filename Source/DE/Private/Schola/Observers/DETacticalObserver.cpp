
#include "Schola/Observers/DETacticalObserver.h"
#include "Schola/Trainers/DETrainer.h"
#include "Schola/Components/DEScholaAgent.h"
#include "Schola/DEScholaEnvironment.h"
#include "Characters/DEAgent.h"
#include "Team/DEMatchManager.h"
#include "Actors/DECapturePoint.h"
#include "Spaces/BoxSpace.h"
#include "Points/BoxPoint.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

UDETacticalObserver::UDETacticalObserver()
{
	// Build observation space (V2 entity-centric: 194-dim padded flat)
	// Layout: Self(7) + Allies(8×8=64) + Enemies(8×5=40) + Bases(8×7=56) + Masks(8+8+8=24) + Strategy(3) = 194
	// Ally token: [rel_pos(3), health, alive, is_assault, is_defend, is_support]
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

	// The observer is a UObject subobject of UDEScholaAgent (on ADEAgent).
	// Outer chain: UDETacticalObserver -> UDEScholaAgent -> ADEAgent.
	// Trainer reference is obtained from the character controller when in training mode.
	CachedCharacter = GetTypedOuter<ADEAgent>();
	if (!CachedCharacter)
	{
		UE_LOG(LogTemp, Error, TEXT("[DETacticalObserver] Could not find ADEAgent in outer chain!"));
		return;
	}

	// Training: controller is ADETrainer. Inference: may be nullptr or a different controller.
	CachedTrainer = Cast<ADETrainer>(CachedCharacter->GetController());

	// Cache environment origin for position normalization.
	// Step 1: Try to find the matching DEScholaEnvironment (training mode).
	bool bFoundOrigin = false;
	{
		TArray<AActor*> EnvActors;
		UGameplayStatics::GetAllActorsOfClass(CachedCharacter->GetWorld(), ADEScholaEnvironment::StaticClass(), EnvActors);
		ADEMatchManager* CharMatchMgr = CachedCharacter->GetMatchManager();
		for (AActor* Actor : EnvActors)
		{
			if (ADEScholaEnvironment* Env = Cast<ADEScholaEnvironment>(Actor))
			{
				if (CharMatchMgr && Env->GetMatchManager() == CharMatchMgr)
				{
					CachedEnvironmentOrigin = Env->GetActorLocation();
					bFoundOrigin = true;
					break;
				}
			}
		}
	}

	// Step 2: Fallback — no DEScholaEnvironment matched (inference mode or unpopulated MatchManager).
	if (!bFoundOrigin)
	{
		// Auto-detect: compute centroid of all DECapturePoints as arena center
		TArray<AActor*> CPActors;
		UGameplayStatics::GetAllActorsOfClass(CachedCharacter->GetWorld(), ADECapturePoint::StaticClass(), CPActors);
		if (CPActors.Num() > 0)
		{
			FVector Center = FVector::ZeroVector;
			for (AActor* A : CPActors)
				Center += A->GetActorLocation();
			CachedEnvironmentOrigin = Center / static_cast<float>(CPActors.Num());
			UE_LOG(LogTemp, Log, TEXT("[DETacticalObserver] Auto-detected arena center from %d capture points: %s"),
				CPActors.Num(), *CachedEnvironmentOrigin.ToString());
		}
		else
		{
			CachedEnvironmentOrigin = FVector::ZeroVector;
			UE_LOG(LogTemp, Warning, TEXT("[DETacticalObserver] No capture points found, using world origin."));
		}
	}

	const FString ModeStr = CachedTrainer ? TEXT("Training") : TEXT("Inference");
	UE_LOG(LogTemp, Log, TEXT("[DETacticalObserver] Initialized (%s mode) for character: %s (EnvOrigin: %s)"),
		*ModeStr, *CachedCharacter->GetName(), *CachedEnvironmentOrigin.ToString());
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

	// Initialize output (V2: 170-dim padded flat)
	OutObservations.Values.SetNum(DE_OBS_V2_DIM);

	ADEAgent* Character = GetControlledCharacter();
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

	// Detailed inference diagnostic: log first 3 observations + every DebugLogFrequency thereafter
	const bool bIsInference = (CachedTrainer == nullptr);
	const bool bShouldDiag = bIsInference && (ObservationCallCount <= 3 || (bEnableDebugLogging && ObservationCallCount % DebugLogFrequency == 0));

	if (bShouldDiag)
	{
		ADEAgent* DiagChar = GetControlledCharacter();
		const FVector DiagPos = DiagChar ? DiagChar->GetActorLocation() : FVector::ZeroVector;
		const FVector DiagRelSelf = DiagPos - CachedEnvironmentOrigin;

		UE_LOG(LogTemp, Warning, TEXT("===== [DETacticalObserver] INFERENCE DIAG #%d for %s ====="),
			ObservationCallCount, DiagChar ? *DiagChar->GetName() : TEXT("NULL"));

		// Self token
		UE_LOG(LogTemp, Warning, TEXT("  EnvOrigin: %s | WorldPos: %s | RelSelf: (%.3f, %.3f, %.3f)"),
			*CachedEnvironmentOrigin.ToString(), *DiagPos.ToString(),
			DiagRelSelf.X / 7500.0, DiagRelSelf.Y / 7500.0, DiagRelSelf.Z / 1000.0);

		// Self token raw values
		if (ObsV2.SelfToken.Features.Num() >= 7)
		{
			UE_LOG(LogTemp, Warning, TEXT("  SelfToken: pos(%.3f,%.3f,%.3f) hp=%.3f vel(%.3f,%.3f,%.3f)"),
				ObsV2.SelfToken.Features[0], ObsV2.SelfToken.Features[1], ObsV2.SelfToken.Features[2],
				ObsV2.SelfToken.Features[3],
				ObsV2.SelfToken.Features[4], ObsV2.SelfToken.Features[5], ObsV2.SelfToken.Features[6]);
		}

		// Token counts
		UE_LOG(LogTemp, Warning, TEXT("  Tokens: allies=%d enemies=%d bases=%d | MatchMgr=%s"),
			ObsV2.AllyTokens.Num(), ObsV2.EnemyTokens.Num(), ObsV2.BaseTokens.Num(),
			(DiagChar && DiagChar->GetMatchManager()) ? TEXT("YES") : TEXT("NO(fallback)"));

		// Strategy
		UE_LOG(LogTemp, Warning, TEXT("  CommandedStrategy: %d"), static_cast<int32>(ObsV2.CommandedStrategy));

		// Ally details (8-dim: pos, hp, alive, strategy one-hot)
		for (int32 i = 0; i < ObsV2.AllyTokens.Num(); ++i)
		{
			const auto& T = ObsV2.AllyTokens[i];
			if (T.Features.Num() >= 8)
			{
				const FString StratStr = T.Features[5] > 0.5f ? TEXT("Assault")
					: (T.Features[6] > 0.5f ? TEXT("Defend") : TEXT("Support"));
				UE_LOG(LogTemp, Warning, TEXT("  Ally[%d]: relPos(%.3f,%.3f,%.3f) hp=%.2f alive=%.0f strategy=%s"),
					i, T.Features[0], T.Features[1], T.Features[2], T.Features[3], T.Features[4], *StratStr);
			}
		}

		// Enemy details
		for (int32 i = 0; i < ObsV2.EnemyTokens.Num(); ++i)
		{
			const auto& T = ObsV2.EnemyTokens[i];
			if (T.Features.Num() >= 5)
			{
				UE_LOG(LogTemp, Warning, TEXT("  Enemy[%d]: relPos(%.3f,%.3f,%.3f) vis=%.0f conf=%.0f"),
					i, T.Features[0], T.Features[1], T.Features[2], T.Features[3], T.Features[4]);
			}
		}

		// Base details
		UE_LOG(LogTemp, Warning, TEXT("  AssignedBaseIndex=%d | AssignedCapturePoints=%d"),
			DiagChar->AssignedBaseIndex, DiagChar->AssignedCapturePoints.Num());
		for (int32 i = 0; i < ObsV2.BaseTokens.Num(); ++i)
		{
			const auto& T = ObsV2.BaseTokens[i];
			if (T.Features.Num() >= 7)
			{
				UE_LOG(LogTemp, Warning, TEXT("  Base[%d]: relPos(%.3f,%.3f,%.3f) own=%.1f prog=%.1f assigned=%.0f val=%.1f"),
					i, T.Features[0], T.Features[1], T.Features[2], T.Features[3], T.Features[4], T.Features[5], T.Features[6]);
			}
		}

		// Scan flat array for out-of-range values
		int32 OutOfRangeCount = 0;
		float MaxAbs = 0.0f;
		int32 MaxAbsIdx = -1;
		for (int32 i = 0; i < OutObservations.Values.Num(); ++i)
		{
			const float AbsVal = FMath::Abs(OutObservations.Values[i]);
			if (AbsVal > 1.0f)
			{
				OutOfRangeCount++;
				if (AbsVal > MaxAbs)
				{
					MaxAbs = AbsVal;
					MaxAbsIdx = i;
				}
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("  FlatArray: %d/%d values outside [-1,1] | worst: idx=%d val=%.3f"),
			OutOfRangeCount, OutObservations.Values.Num(), MaxAbsIdx, MaxAbsIdx >= 0 ? OutObservations.Values[MaxAbsIdx] : 0.0f);

		UE_LOG(LogTemp, Warning, TEXT("===== END DIAG ====="));
	}
	else if (bEnableDebugLogging && ObservationCallCount % DebugLogFrequency == 0)
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

ADEAgent* UDETacticalObserver::GetControlledCharacter() const
{
	// Path 1: Training mode — get pawn from trainer
	if (CachedTrainer)
	{
		return Cast<ADEAgent>(CachedTrainer->GetPawn());
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

	ADEAgent* Character = GetControlledCharacter();
	if (!Character) return Obs;

	const FVector MyPos = Character->GetActorLocation();
	const FVector RelSelfPos = MyPos - CachedEnvironmentOrigin;
	const FVector Vel = Character->GetVelocity();

	// ---- Self token (7-dim) ----
	Obs.SelfToken.Features = {
		FMath::Clamp(static_cast<float>(RelSelfPos.X / 7500.0), -1.0f, 1.0f),
		FMath::Clamp(static_cast<float>(RelSelfPos.Y / 7500.0), -1.0f, 1.0f),
		FMath::Clamp(static_cast<float>(RelSelfPos.Z / 1000.0), -1.0f, 1.0f),
		Character->GetHealthPercentage(),
		FMath::Clamp(static_cast<float>(Vel.X / 600.0), -1.0f, 1.0f),
		FMath::Clamp(static_cast<float>(Vel.Y / 600.0), -1.0f, 1.0f),
		FMath::Clamp(static_cast<float>(Vel.Z / 600.0), -1.0f, 1.0f)
	};

	const int32 MyTeamID = Character->GetTeamID_Implementation();

	// Strategy one-hot (populated before MatchMgr block so it's always set)
	Obs.CommandedStrategy = Character->GetCommandedStrategy();

	ADEMatchManager* MatchMgr = Character->GetMatchManager();

	// Gather ally, enemy, and base lists.
	// Use MatchManager if it has populated data, otherwise query world directly.
	TArray<ADEAgent*> Allies;
	TArray<ADEAgent*> Enemies;
	TArray<ADECapturePoint*> CapturePoints;

	bool bUsedMatchMgr = false;
	if (MatchMgr)
	{
		Allies = MatchMgr->GetTeamAgents(MyTeamID);
		Enemies = MatchMgr->GetEnemyAgents(MyTeamID);
		const TArray<ADECapturePoint*>& CPs = MatchMgr->GetCapturePoints();
		CapturePoints = CPs;

		// Check if MatchManager actually has data (it may exist but be unpopulated in inference)
		bUsedMatchMgr = (Allies.Num() > 0 || Enemies.Num() > 0 || CapturePoints.Num() > 0);
	}

	if (!bUsedMatchMgr)
	{
		// Fallback: query actors directly from the world
		Allies.Empty();
		Enemies.Empty();
		CapturePoints.Empty();

		TArray<AActor*> AllCharacters;
		UGameplayStatics::GetAllActorsOfClass(Character->GetWorld(), ADEAgent::StaticClass(), AllCharacters);
		for (AActor* Actor : AllCharacters)
		{
			ADEAgent* OtherChar = Cast<ADEAgent>(Actor);
			if (!OtherChar || OtherChar == Character) continue;
			if (OtherChar->GetTeamID_Implementation() == MyTeamID)
				Allies.Add(OtherChar);
			else
				Enemies.Add(OtherChar);
		}

		TArray<AActor*> AllCPs;
		UGameplayStatics::GetAllActorsOfClass(Character->GetWorld(), ADECapturePoint::StaticClass(), AllCPs);
		for (AActor* Actor : AllCPs)
		{
			if (ADECapturePoint* CP = Cast<ADECapturePoint>(Actor))
				CapturePoints.Add(CP);
		}
	}

	// ---- Ally tokens (8-dim each): [rel_pos/8000(3), health, alive, is_assault, is_defend, is_support] ----
	for (ADEAgent* Ally : Allies)
	{
		if (!Ally || Ally == Character) continue;
		if (Obs.AllyTokens.Num() >= DE_MAX_ALLIES) break;

		// Dead allies: zero out position to prevent living agents from being
		// pulled toward corpse/spawn locations (e.g. world origin).
		const bool bAllyAlive = Ally->IsAlive();
		const FVector RelPos = bAllyAlive
			? (Ally->GetActorLocation() - MyPos) / 8000.0f
			: FVector::ZeroVector;
		const EDEStrategyType AllyStrategy = Ally->GetCommandedStrategy();
		FDEEntityToken Tok;
		Tok.Features = {
			static_cast<float>(RelPos.X),
			static_cast<float>(RelPos.Y),
			static_cast<float>(RelPos.Z),
			bAllyAlive ? Ally->GetHealthPercentage() : 0.0f,
			bAllyAlive ? 1.0f : 0.0f,
			(AllyStrategy == EDEStrategyType::Assault) ? 1.0f : 0.0f,
			(AllyStrategy == EDEStrategyType::Defend)  ? 1.0f : 0.0f,
			(AllyStrategy == EDEStrategyType::Support) ? 1.0f : 0.0f
		};
		Obs.AllyTokens.Add(MoveTemp(Tok));
	}

	// ---- Enemy tokens (5-dim each): [rel_pos/8000(3), visible, confidence=1] ----
	for (ADEAgent* Enemy : Enemies)
	{
		if (!Enemy) continue;
		if (Obs.EnemyTokens.Num() >= DE_MAX_ENEMIES) break;

		// Dead enemies: zero out — don't report stale corpse positions.
		if (!Enemy->IsAlive())
		{
			FDEEntityToken Tok;
			Tok.Features = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
			Obs.EnemyTokens.Add(MoveTemp(Tok));
			continue;
		}

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
	// IsAssigned: Training path uses AssignedBaseIndex (set by MatchManager).
	// Inference path: pick nearest CP from AssignedCapturePoints (matches training's initial assignment).

	// Determine effective assigned CP for inference when AssignedBaseIndex is unset
	ADECapturePoint* InferenceAssignedCP = nullptr;
	if (Character->AssignedBaseIndex < 0 && Character->AssignedCapturePoints.Num() > 0)
	{
		float BestDistSq = TNumericLimits<float>::Max();
		for (ADECapturePoint* ACP : Character->AssignedCapturePoints)
		{
			if (!ACP) continue;
			const float DistSq = FVector::DistSquared(ACP->GetActorLocation(), MyPos);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				InferenceAssignedCP = ACP;
			}
		}
	}

	for (int32 i = 0; i < CapturePoints.Num() && Obs.BaseTokens.Num() < DE_MAX_BASES; ++i)
	{
		ADECapturePoint* CP = CapturePoints[i];
		if (!CP) continue;

		const FVector RelPos = (CP->GetActorLocation() - MyPos) / 15000.0f;
		const int32 OwnerTeam = CP->GetTeamID_Implementation();
		float Ownership = 0.0f;
		if (OwnerTeam == MyTeamID) Ownership = 1.0f;
		else if (OwnerTeam >= 0)   Ownership = -1.0f;

		float IsAssigned = 0.0f;
		if (Character->AssignedBaseIndex >= 0)
		{
			// Training path: MatchManager set the index directly
			IsAssigned = (Character->AssignedBaseIndex == i) ? 1.0f : 0.0f;
		}
		else
		{
			// Inference path: nearest CP from AssignedCapturePoints
			IsAssigned = (CP == InferenceAssignedCP) ? 1.0f : 0.0f;
		}

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

