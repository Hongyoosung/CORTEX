// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/Models/TeamWorldModel.h"
#include "Misc/ScopeLock.h"

UTeamWorldModel::UTeamWorldModel()
	: AgentWorldModel(nullptr)
	, LastInferenceTimeMs(0.0f)
{
}

bool UTeamWorldModel::Initialize(ULearnedWorldModel* InAgentWorldModel)
{
	if (!InAgentWorldModel)
	{
		UE_LOG(LogTemp, Error, TEXT("TeamWorldModel: Cannot initialize with null AgentWorldModel"));
		return false;
	}

	AgentWorldModel = InAgentWorldModel;

	UE_LOG(LogTemp, Log, TEXT("TeamWorldModel: Initialized successfully with agent model wrapper"));
	return true;
}

FTeamStatePrediction UTeamWorldModel::PredictTeamTransition(
	const FTeamState& CurrentState,
	ETacticalPlay SelectedPlay)
{
	// Thread safety for future async support
	FScopeLock Lock(&PredictionMutex);

	// Start performance timer
	double StartTime = FPlatformTime::Seconds();

	FTeamStatePrediction Result;

	// Validate agent world model
	if (!AgentWorldModel)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamWorldModel: AgentWorldModel not initialized, returning default prediction"));
		Result.NextState = CurrentState;
		Result.Confidence = 0.0f;
		return Result;
	}

	// Step 1: Decompose tactical play into role distribution
	TArray<EStrategyType> Strategies = DecomposeTacticalPlay(SelectedPlay);

	if (Strategies.Num() != 5)
	{
		UE_LOG(LogTemp, Error, TEXT("TeamWorldModel: DecomposeTacticalPlay returned %d strategies (expected 5)"), Strategies.Num());
		Result.NextState = CurrentState;
		Result.Confidence = 0.0f;
		return Result;
	}

	// Step 2: Convert team state to agent observations
	TArray<FObservation> AgentObservations = ConvertTeamStateToAgentObservations(CurrentState, Strategies);

	// Step 3: Generate tactical options for each agent
	TArray<FTacticalOption> TacticalOptions;
	TacticalOptions.Reserve(5);

	for (int32 i = 0; i < 5; ++i)
	{
		FTacticalOption Option = GenerateTacticalOption(i, Strategies[i], CurrentState);
		TacticalOptions.Add(Option);
	}

	// Step 4: Batch predict via agent world model
	FBatchModelInput BatchInput;
	BatchInput.CurrentStates = AgentObservations;
	BatchInput.SelectedOptions = TacticalOptions;

	FBatchModelOutput BatchOutput = AgentWorldModel->PredictBatch(BatchInput);

	// Validate output
	if (BatchOutput.PredictedStates.Num() != 5)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamWorldModel: Agent model returned %d predictions (expected 5)"),
			BatchOutput.PredictedStates.Num());
		Result.NextState = CurrentState;
		Result.Confidence = 0.0f;
		return Result;
	}

	// Step 5: Aggregate agent predictions to team state
	Result.NextState = AggregateToTeamState(BatchOutput.PredictedStates, CurrentState);

	// Step 6: Aggregate rewards
	Result.Reward = AggregateRewards(BatchOutput.Rewards, Strategies);

	// Step 7: Calculate confidence (minimum of individual confidences)
	Result.Confidence = 1.0f;
	for (float Conf : BatchOutput.Confidences)
	{
		Result.Confidence = FMath::Min(Result.Confidence, Conf);
	}

	// Record inference time
	double EndTime = FPlatformTime::Seconds();
	LastInferenceTimeMs = (EndTime - StartTime) * 1000.0f;

	// Warn if performance budget exceeded
	if (LastInferenceTimeMs > 5.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamWorldModel: Prediction took %.2f ms (target < 5ms)"), LastInferenceTimeMs);
	}

	return Result;
}

FTeamBatchOutput UTeamWorldModel::PredictBatch(const FTeamBatchInput& BatchInput)
{
	FScopeLock Lock(&PredictionMutex);

	double StartTime = FPlatformTime::Seconds();

	FTeamBatchOutput Result;

	// Validate input
	if (!BatchInput.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("TeamWorldModel: Invalid batch input"));
		return Result;
	}

	int32 BatchSize = BatchInput.GetBatchSize();

	// Pre-allocate output arrays
	Result.PredictedStates.Reserve(BatchSize);
	Result.Rewards.Reserve(BatchSize);
	Result.Confidences.Reserve(BatchSize);

	// Process each item in batch
	for (int32 i = 0; i < BatchSize; ++i)
	{
		FTeamStatePrediction Prediction = PredictTeamTransition(
			BatchInput.CurrentStates[i],
			BatchInput.SelectedPlays[i]
		);

		Result.PredictedStates.Add(Prediction.NextState);
		Result.Rewards.Add(Prediction.Reward);
		Result.Confidences.Add(Prediction.Confidence);
	}

	// Record total batch time
	double EndTime = FPlatformTime::Seconds();
	LastInferenceTimeMs = (EndTime - StartTime) * 1000.0f;

	if (LastInferenceTimeMs > 8.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamWorldModel: Batch prediction (%d items) took %.2f ms (target < 8ms)"),
			BatchSize, LastInferenceTimeMs);
	}

	return Result;
}

//========================================
// Decompose & Convert (Team → Agent)
//========================================

TArray<EStrategyType> UTeamWorldModel::DecomposeTacticalPlay(ETacticalPlay Play) const
{
	// Reuse logic from ASquadManager::DecodeTacticalPlay()
	TArray<EStrategyType> Roles;

	switch (Play)
	{
	case ETacticalPlay::AllOutRush:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Assault
		};
		break;

	case ETacticalPlay::AggressivePush:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Support
		};
		break;

	case ETacticalPlay::Phalanx:
		Roles = {
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Support,
			EStrategyType::Support,
			EStrategyType::Support
		};
		break;

	case ETacticalPlay::StandardComp:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Support
		};
		break;

	case ETacticalPlay::FortressDefense:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend
		};
		break;

	case ETacticalPlay::TurtleFormation:
		Roles = {
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend
		};
		break;

	case ETacticalPlay::BaitStrategy:
		Roles = {
			EStrategyType::Assault, // Bait unit
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Defend
		};
		break;

	case ETacticalPlay::PincerManeuver:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Support,
			EStrategyType::Support
		};
		break;

	case ETacticalPlay::HealerComp:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Defend,
			EStrategyType::Support,
			EStrategyType::Support
		};
		break;

	case ETacticalPlay::ResourceDeny:
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Support,
			EStrategyType::Support,
			EStrategyType::Support
		};
		break;

	default:
		// Fallback: Standard composition
		Roles = {
			EStrategyType::Assault,
			EStrategyType::Assault,
			EStrategyType::Defend,
			EStrategyType::Defend,
			EStrategyType::Support
		};
		break;
	}

	return Roles;
}

TArray<FObservation> UTeamWorldModel::ConvertTeamStateToAgentObservations(
	const FTeamState& TeamState,
	const TArray<EStrategyType>& Strategies) const
{
	TArray<FObservation> Observations;
	Observations.Reserve(5);

	// Extract observation for each agent
	for (int32 i = 0; i < 5; ++i)
	{
		FObservation AgentObs = ExtractAgentObservation(TeamState, i);
		Observations.Add(AgentObs);
	}

	return Observations;
}

FObservation UTeamWorldModel::ExtractAgentObservation(
	const FTeamState& TeamState,
	int32 AgentIndex) const
{
	FObservation Obs;
	Obs.Features.Reserve(56);

	// Validate index
	if (AgentIndex < 0 || AgentIndex >= 5)
	{
		UE_LOG(LogTemp, Error, TEXT("TeamWorldModel: Invalid agent index %d"), AgentIndex);
		return Obs;
	}

	FVector AgentPos = TeamState.FriendlyPositions[AgentIndex];

	// 1. Self state (6-dim)
	// Position (normalized by 10000 for ~100m scale)
	Obs.Features.Add(AgentPos.X / 10000.0f);
	Obs.Features.Add(AgentPos.Y / 10000.0f);
	Obs.Features.Add(AgentPos.Z / 1000.0f);

	// Health
	Obs.Features.Add(TeamState.FriendlyHealths[AgentIndex]);

	// Cooldown
	Obs.Features.Add(TeamState.FriendlyCooldowns[AgentIndex]);

	// Alive status
	Obs.Features.Add(TeamState.FriendlyAlive[AgentIndex] ? 1.0f : 0.0f);

	// 2. Nearby allies (4 × 5 = 20-dim)
	for (int32 j = 0; j < 5; ++j)
	{
		if (j == AgentIndex) continue; // Skip self

		// Relative position (agent-centric)
		FVector RelativePos = (TeamState.FriendlyPositions[j] - AgentPos) / 10000.0f;
		Obs.Features.Add(RelativePos.X);
		Obs.Features.Add(RelativePos.Y);
		Obs.Features.Add(RelativePos.Z);

		// Ally health
		Obs.Features.Add(TeamState.FriendlyHealths[j]);

		// Ally alive status
		Obs.Features.Add(TeamState.FriendlyAlive[j] ? 1.0f : 0.0f);
	}

	// 3. Nearby enemies (5 × 4 = 20-dim)
	for (int32 k = 0; k < 5; ++k)
	{
		// Relative position
		FVector RelativePos = (TeamState.EnemyPositions[k] - AgentPos) / 10000.0f;
		Obs.Features.Add(RelativePos.X);
		Obs.Features.Add(RelativePos.Y);
		Obs.Features.Add(RelativePos.Z);

		// Enemy confidence (fog of war mask)
		Obs.Features.Add(TeamState.EnemyConfidences[k]);
	}

	// 4. Map state (10-dim)
	// Capture point ownership (5-dim)
	for (int32 i = 0; i < 5; ++i)
	{
		Obs.Features.Add(static_cast<float>(TeamState.CapturePointOwnership[i]));
	}

	// Pickup availability (5-dim - expand bitmask)
	for (int32 i = 0; i < 5; ++i)
	{
		bool bAvailable = (TeamState.PickupAvailability & (1 << i)) != 0;
		Obs.Features.Add(bAvailable ? 1.0f : 0.0f);
	}

	// Validate final size
	if (Obs.Features.Num() != 56)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamWorldModel: Agent %d observation has %d features (expected 56)"),
			AgentIndex, Obs.Features.Num());
	}

	return Obs;
}

FTacticalOption UTeamWorldModel::GenerateTacticalOption(
	int32 AgentIndex,
	EStrategyType Strategy,
	const FTeamState& TeamState) const
{
	FTacticalOption Option;
	Option.Strategy = Strategy;
	Option.Duration = TimeStepSeconds;
	Option.Confidence = 0.7f; // Default medium confidence

	// Validate agent index
	if (AgentIndex < 0 || AgentIndex >= 5)
	{
		Option.TargetPosition = FVector::ZeroVector;
		return Option;
	}

	FVector AgentPos = TeamState.FriendlyPositions[AgentIndex];

	// Strategy-based target selection heuristics
	switch (Strategy)
	{
	case EStrategyType::Assault:
	{
		// Target nearest visible enemy or map center
		float MinDist = FLT_MAX;
		FVector BestTarget = FVector(7500.0f, 7500.0f, 0.0f); // Map center (150m / 2)

		for (int32 i = 0; i < 5; ++i)
		{
			if (TeamState.EnemyConfidences[i] > 0.3f && TeamState.EnemyAlive[i])
			{
				float Dist = FVector::Dist(AgentPos, TeamState.EnemyPositions[i]);
				if (Dist < MinDist)
				{
					MinDist = Dist;
					BestTarget = TeamState.EnemyPositions[i];
					Option.Confidence = TeamState.EnemyConfidences[i];
				}
			}
		}

		Option.TargetPosition = BestTarget;
		break;
	}

	case EStrategyType::Defend:
	{
		// Target friendly-controlled objective or hold position
		FVector BestTarget = AgentPos; // Default: hold current position
		float BestScore = -1.0f;

		for (int32 i = 0; i < 5; ++i)
		{
			if (TeamState.CapturePointOwnership[i] == 1) // Friendly controlled
			{
				// Assume capture points are evenly distributed
				// Simple heuristic: points on a line across map
				FVector PointPos = FVector(3000.0f * i + 1500.0f, 7500.0f, 0.0f);
				float Dist = FVector::Dist(AgentPos, PointPos);
				float Score = 1.0f / (Dist + 100.0f); // Prefer closer points

				if (Score > BestScore)
				{
					BestScore = Score;
					BestTarget = PointPos;
				}
			}
		}

		Option.TargetPosition = BestTarget;
		Option.Confidence = 0.8f; // Defend objectives have high confidence
		break;
	}

	case EStrategyType::Support:
	{
		// Target weakest ally for healing/support
		float MinHealth = 1.0f;
		FVector BestTarget = AgentPos; // Default: stay at current position

		for (int32 i = 0; i < 5; ++i)
		{
			if (i == AgentIndex) continue; // Skip self

			if (TeamState.FriendlyAlive[i] && TeamState.FriendlyHealths[i] < MinHealth)
			{
				MinHealth = TeamState.FriendlyHealths[i];
				BestTarget = TeamState.FriendlyPositions[i];
			}
		}

		Option.TargetPosition = BestTarget;
		Option.Confidence = MinHealth < 0.5f ? 0.9f : 0.6f; // High confidence if ally is low
		break;
	}

	default:
		Option.TargetPosition = AgentPos; // Hold position
		break;
	}

	return Option;
}

//========================================
// Aggregate (Agent → Team)
//========================================

FTeamState UTeamWorldModel::AggregateToTeamState(
	const TArray<FObservation>& Predictions,
	const FTeamState& OriginalState) const
{
	FTeamState NextState = OriginalState; // Copy original for map/enemy data

	// Validate input
	if (Predictions.Num() != 5)
	{
		UE_LOG(LogTemp, Error, TEXT("TeamWorldModel: AggregateToTeamState received %d predictions (expected 5)"),
			Predictions.Num());
		return NextState;
	}

	// Extract friendly agent states from predictions
	for (int32 i = 0; i < 5; ++i)
	{
		const FObservation& Pred = Predictions[i];

		if (Pred.Features.Num() < 6)
		{
			UE_LOG(LogTemp, Warning, TEXT("TeamWorldModel: Prediction %d has insufficient features"), i);
			continue;
		}

		// Position (features 0-2, denormalized)
		NextState.FriendlyPositions[i] = FVector(
			Pred.Features[0] * 10000.0f,
			Pred.Features[1] * 10000.0f,
			Pred.Features[2] * 1000.0f
		);

		// Health (feature 3)
		NextState.FriendlyHealths[i] = Pred.Features[3];

		// Cooldown (feature 4)
		NextState.FriendlyCooldowns[i] = Pred.Features[4];

		// Alive status (feature 5)
		NextState.FriendlyAlive[i] = Pred.Features[5] > 0.5f;
	}

	// Apply confidence decay to enemy positions (fog of war)
	if (bUseEnemyUncertainty)
	{
		for (int32 i = 0; i < 5; ++i)
		{
			NextState.EnemyConfidences[i] = FMath::Max(
				0.0f,
				OriginalState.EnemyConfidences[i] - EnemyConfidenceDecayRate
			);
		}
	}

	// Enemy positions, health, alive status copied from original
	// (world model doesn't predict enemy behavior - that's opponent modeling)

	// Map state copied from original (capture points don't change in 0.5s timestep)
	// In full implementation, this could be updated based on agent presence

	return NextState;
}

FTeamReward UTeamWorldModel::AggregateRewards(
	const TArray<FCompositeReward>& IndividualRewards,
	const TArray<EStrategyType>& Strategies) const
{
	FTeamReward TeamRwd;

	// Validate input
	if (IndividualRewards.Num() != 5 || Strategies.Num() != 5)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamWorldModel: Invalid reward aggregation input"));
		return TeamRwd;
	}

	// 1. Win probability: Average across agents
	float WinProbSum = 0.0f;
	for (const FCompositeReward& Rwd : IndividualRewards)
	{
		WinProbSum += Rwd.WinProb;
	}
	TeamRwd.WinProb = WinProbSum / 5.0f;

	// 2. Team health delta: Sum of individual changes
	float HealthDeltaSum = 0.0f;
	for (const FCompositeReward& Rwd : IndividualRewards)
	{
		HealthDeltaSum += Rwd.HealthDelta;
	}
	TeamRwd.TeamHealthDelta = HealthDeltaSum;

	// 3. Objective score: Strategy-weighted average
	// Defenders weight objectives higher (1.5×), Assault moderate (1.2×), Support assists (0.8×)
	float ObjScoreSum = 0.0f;
	float WeightSum = 0.0f;

	for (int32 i = 0; i < 5; ++i)
	{
		float Weight = 1.0f;
		switch (Strategies[i])
		{
		case EStrategyType::Assault:
			Weight = 1.2f;
			break;
		case EStrategyType::Defend:
			Weight = 1.5f;
			break;
		case EStrategyType::Support:
			Weight = 0.8f;
			break;
		}

		ObjScoreSum += IndividualRewards[i].ObjectiveScore * Weight;
		WeightSum += Weight;
	}

	TeamRwd.ObjectiveScore = WeightSum > 0.0f ? ObjScoreSum / WeightSum : 0.0f;

	// 4. Diversity bonus
	TeamRwd.DiversityBonus = CalculateDiversityBonus(Strategies);

	// 5. Scalarize
	TeamRwd.Scalarize();

	return TeamRwd;
}

float UTeamWorldModel::CalculateDiversityBonus(const TArray<EStrategyType>& Strategies) const
{
	if (Strategies.Num() != 5)
	{
		return 0.0f;
	}

	// Count unique strategies
	TSet<EStrategyType> UniqueStrategies;
	for (EStrategyType Strat : Strategies)
	{
		UniqueStrategies.Add(Strat);
	}

	int32 UniqueCount = UniqueStrategies.Num();

	// Return bonus based on diversity
	if (UniqueCount >= 3)
	{
		return 1.0f; // High diversity
	}
	else if (UniqueCount == 2)
	{
		return 0.5f; // Moderate diversity
	}
	else
	{
		return 0.0f; // No diversity (all same strategy)
	}
}
