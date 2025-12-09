// Copyright Epic Games, Inc. All Rights Reserved.

#include "RL/RLReplayBuffer.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"

URLReplayBuffer::URLReplayBuffer()
	: MaxCapacity(100000)
	, bUsePrioritizedReplay(false)
	, PriorityAlpha(0.6f)
	, ImportanceSamplingBeta(0.4f)
	, PriorityEpsilon(0.01f)
	, WritePosition(0)
{
}

// ========================================
// Buffer Management
// ========================================

void URLReplayBuffer::AddExperience(const FRLExperience& Experience)
{
	float Priority = CalculateInitialPriority();
	AddExperienceWithPriority(Experience, Priority);
}

void URLReplayBuffer::AddExperienceWithPriority(const FRLExperience& Experience, float Priority)
{
	if (Experiences.Num() < MaxCapacity)
	{
		// Buffer not full, add to end
		Experiences.Add(Experience);
		Priorities.Add(Priority);
	}
	else
	{
		// Buffer full, overwrite oldest (circular buffer)
		Experiences[WritePosition] = Experience;
		Priorities[WritePosition] = Priority;

		// Update write position
		WritePosition = (WritePosition + 1) % MaxCapacity;
	}

	// Update sum tree if using PER
	if (bUsePrioritizedReplay)
	{
		UpdateSumTree(Experiences.Num() - 1);
	}
}

TArray<FRLExperience> URLReplayBuffer::SampleBatch(int32 BatchSize)
{
	TArray<FRLExperience> Batch;

	if (Experiences.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("URLReplayBuffer: Cannot sample from empty buffer"));
		return Batch;
	}

	if (BatchSize > Experiences.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("URLReplayBuffer: Batch size (%d) larger than buffer size (%d), sampling all"),
			BatchSize, Experiences.Num());
		BatchSize = Experiences.Num();
	}

	// Random sampling (uniform)
	TSet<int32> SampledIndices;
	while (SampledIndices.Num() < BatchSize)
	{
		int32 RandomIndex = FMath::RandRange(0, Experiences.Num() - 1);
		SampledIndices.Add(RandomIndex);
	}

	// Collect experiences
	for (int32 Index : SampledIndices)
	{
		Batch.Add(Experiences[Index]);
	}

	return Batch;
}

TArray<FRLExperience> URLReplayBuffer::SampleBatchPrioritized(int32 BatchSize, TArray<float>& OutImportanceWeights)
{
	TArray<FRLExperience> Batch;
	OutImportanceWeights.Empty();

	if (!bUsePrioritizedReplay)
	{
		UE_LOG(LogTemp, Warning, TEXT("URLReplayBuffer: Prioritized sampling disabled, using uniform sampling"));
		return SampleBatch(BatchSize);
	}

	if (Experiences.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("URLReplayBuffer: Cannot sample from empty buffer"));
		return Batch;
	}

	if (BatchSize > Experiences.Num())
	{
		BatchSize = Experiences.Num();
	}

	// Calculate total priority
	float TotalPriority = 0.0f;
	for (float Priority : Priorities)
	{
		TotalPriority += FMath::Pow(Priority, PriorityAlpha);
	}

	// Sample indices based on priority
	TSet<int32> SampledIndices;
	while (SampledIndices.Num() < BatchSize)
	{
		int32 Index = SampleProportionalIndex();
		SampledIndices.Add(Index);
	}

	// Collect experiences and calculate importance weights
	float MaxWeight = -MAX_FLT;
	TArray<float> RawWeights;

	for (int32 Index : SampledIndices)
	{
		Batch.Add(Experiences[Index]);

		float Weight = CalculateImportanceWeight(Index, TotalPriority);
		RawWeights.Add(Weight);

		if (Weight > MaxWeight)
		{
			MaxWeight = Weight;
		}
	}

	// Normalize importance weights by max weight
	for (float RawWeight : RawWeights)
	{
		OutImportanceWeights.Add(RawWeight / MaxWeight);
	}

	return Batch;
}

void URLReplayBuffer::UpdatePriorities(const TArray<int32>& Indices, const TArray<float>& NewPriorities)
{
	if (!bUsePrioritizedReplay)
	{
		return;
	}

	if (Indices.Num() != NewPriorities.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("URLReplayBuffer: Indices and priorities array size mismatch"));
		return;
	}

	// Update priorities
	for (int32 i = 0; i < Indices.Num(); i++)
	{
		int32 Index = Indices[i];
		if (Index >= 0 && Index < Priorities.Num())
		{
			Priorities[Index] = FMath::Max(NewPriorities[i], PriorityEpsilon);
		}
	}

	// Rebuild sum tree
	RebuildSumTree();
}

void URLReplayBuffer::Clear()
{
	Experiences.Empty();
	Priorities.Empty();
	SumTree.Empty();
	WritePosition = 0;

	UE_LOG(LogTemp, Log, TEXT("URLReplayBuffer: Cleared all experiences"));
}

void URLReplayBuffer::RemoveOldest(int32 Count)
{
	if (Count <= 0 || Experiences.Num() == 0)
	{
		return;
	}

	Count = FMath::Min(Count, Experiences.Num());

	Experiences.RemoveAt(0, Count);
	Priorities.RemoveAt(0, Count);

	WritePosition = FMath::Max(0, WritePosition - Count);

	if (bUsePrioritizedReplay)
	{
		RebuildSumTree();
	}

	UE_LOG(LogTemp, Log, TEXT("URLReplayBuffer: Removed %d oldest experiences"), Count);
}

// ========================================
// Export / Import
// ========================================

bool URLReplayBuffer::ExportToJSON(const FString& FilePath)
{
	if (Experiences.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("URLReplayBuffer: No experiences to export"));
		return false;
	}

	// Create JSON array
	TArray<TSharedPtr<FJsonValue>> ExperiencesArray;

	for (int32 i = 0; i < Experiences.Num(); i++)
	{
		const FRLExperience& Exp = Experiences[i];
		TSharedPtr<FJsonObject> ExpObject = MakeShareable(new FJsonObject());

		// State (71 features)
		TArray<TSharedPtr<FJsonValue>> StateArray;
		TArray<float> StateFeatures = Exp.State.ToFeatureVector();
		for (float Feature : StateFeatures)
		{
			StateArray.Add(MakeShareable(new FJsonValueNumber(Feature)));
		}
		ExpObject->SetArrayField(TEXT("state"), StateArray);

		// Action (serialize FTacticalAction as JSON object)
		TSharedPtr<FJsonObject> ActionObject = MakeShareable(new FJsonObject());
		ActionObject->SetNumberField(TEXT("move_x"), Exp.Action.MoveDirection.X);
		ActionObject->SetNumberField(TEXT("move_y"), Exp.Action.MoveDirection.Y);
		ActionObject->SetNumberField(TEXT("move_speed"), Exp.Action.MoveSpeed);
		ActionObject->SetNumberField(TEXT("look_x"), Exp.Action.LookDirection.X);
		ActionObject->SetNumberField(TEXT("look_y"), Exp.Action.LookDirection.Y);
		ActionObject->SetBoolField(TEXT("fire"), Exp.Action.bFire);
		ActionObject->SetBoolField(TEXT("crouch"), Exp.Action.bCrouch);
		ActionObject->SetBoolField(TEXT("use_ability"), Exp.Action.bUseAbility);
		ExpObject->SetObjectField(TEXT("action"), ActionObject);

		// Reward
		ExpObject->SetNumberField(TEXT("reward"), Exp.Reward);

		// Next state (71 features)
		TArray<TSharedPtr<FJsonValue>> NextStateArray;
		TArray<float> NextStateFeatures = Exp.NextState.ToFeatureVector();
		for (float Feature : NextStateFeatures)
		{
			NextStateArray.Add(MakeShareable(new FJsonValueNumber(Feature)));
		}
		ExpObject->SetArrayField(TEXT("next_state"), NextStateArray);

		// Terminal
		ExpObject->SetBoolField(TEXT("terminal"), Exp.bTerminal);

		// Timestamp
		ExpObject->SetNumberField(TEXT("timestamp"), Exp.Timestamp);

		// Priority (if PER enabled)
		if (bUsePrioritizedReplay && i < Priorities.Num())
		{
			ExpObject->SetNumberField(TEXT("priority"), Priorities[i]);
		}

		ExperiencesArray.Add(MakeShareable(new FJsonValueObject(ExpObject)));
	}

	// Create root object with metadata
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject());
	RootObject->SetArrayField(TEXT("experiences"), ExperiencesArray);
	RootObject->SetNumberField(TEXT("total_experiences"), Experiences.Num());
	RootObject->SetNumberField(TEXT("max_capacity"), MaxCapacity);
	RootObject->SetBoolField(TEXT("prioritized_replay"), bUsePrioritizedReplay);
	RootObject->SetNumberField(TEXT("average_reward"), GetAverageReward());

	// Serialize to JSON
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	if (!FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
	{
		UE_LOG(LogTemp, Error, TEXT("URLReplayBuffer: Failed to serialize to JSON"));
		return false;
	}

	// Write to file
	if (!FFileHelper::SaveStringToFile(OutputString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("URLReplayBuffer: Failed to write to file: %s"), *FilePath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("URLReplayBuffer: Exported %d experiences to %s"),
		Experiences.Num(), *FilePath);

	return true;
}

int32 URLReplayBuffer::ImportFromJSON(const FString& FilePath)
{
	// Load file
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("URLReplayBuffer: Failed to load file: %s"), *FilePath);
		return 0;
	}

	// Parse JSON
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("URLReplayBuffer: Failed to parse JSON"));
		return 0;
	}

	// Get experiences array
	const TArray<TSharedPtr<FJsonValue>>* ExperiencesArray;
	if (!RootObject->TryGetArrayField(TEXT("experiences"), ExperiencesArray))
	{
		UE_LOG(LogTemp, Error, TEXT("URLReplayBuffer: No experiences array in JSON"));
		return 0;
	}

	// Clear existing experiences
	Clear();

	// Import experiences
	int32 ImportedCount = 0;
	for (const TSharedPtr<FJsonValue>& ExpValue : *ExperiencesArray)
	{
		TSharedPtr<FJsonObject> ExpObject = ExpValue->AsObject();
		if (!ExpObject.IsValid())
		{
			continue;
		}

		FRLExperience Experience;

		// Parse observation
		// Note: Observation deserialization not needed for real-time RLlib training
		if (ExpObject->HasTypedField<EJson::Array>(TEXT("observation")))
		{
			const TArray<TSharedPtr<FJsonValue>>& ObsArray = ExpObject->GetArrayField(TEXT("observation"));
			if (ObsArray.Num() == 71)  // Expected observation size
			{
				UE_LOG(LogTemp, Warning, TEXT("URLReplayBuffer: Observation deserialization not implemented (not needed for real-time training)"));
			}
		}

		// Parse action
		if (ExpObject->HasTypedField<EJson::Object>(TEXT("action")))
		{
			TSharedPtr<FJsonObject> ActionObj = ExpObject->GetObjectField(TEXT("action"));
			// Parse FTacticalAction fields
			Experience.Action.MoveDirection.X = ActionObj->GetNumberField(TEXT("move_x"));
			Experience.Action.MoveDirection.Y = ActionObj->GetNumberField(TEXT("move_y"));
			Experience.Action.MoveSpeed = ActionObj->GetNumberField(TEXT("move_speed"));
			Experience.Action.LookDirection.X = ActionObj->GetNumberField(TEXT("look_x"));
			Experience.Action.LookDirection.Y = ActionObj->GetNumberField(TEXT("look_y"));
			Experience.Action.bFire = ActionObj->GetBoolField(TEXT("fire"));
			Experience.Action.bCrouch = ActionObj->GetBoolField(TEXT("crouch"));
			Experience.Action.bUseAbility = ActionObj->GetBoolField(TEXT("use_ability"));
		}

		// Parse reward, next_observation, done
		Experience.Reward = ExpObject->GetNumberField(TEXT("reward"));
		Experience.bTerminal = ExpObject->GetBoolField(TEXT("done"));

		if (ExpObject->HasTypedField<EJson::Array>(TEXT("next_observation")))
		{
			const TArray<TSharedPtr<FJsonValue>>& NextObsArray = ExpObject->GetArrayField(TEXT("next_observation"));
			if (NextObsArray.Num() == 71)
			{
				UE_LOG(LogTemp, Warning, TEXT("URLReplayBuffer: NextObservation deserialization not implemented (not needed for real-time training)"));
			}
		}

		AddExperience(Experience);
		ImportedCount++;
	}

	UE_LOG(LogTemp, Log, TEXT("URLReplayBuffer: Imported %d experiences from %s"),
		ImportedCount, *FilePath);

	return ImportedCount;
}

// ========================================
// Statistics
// ========================================

float URLReplayBuffer::GetUsagePercentage() const
{
	if (MaxCapacity == 0)
	{
		return 0.0f;
	}

	return (static_cast<float>(Experiences.Num()) / MaxCapacity) * 100.0f;
}

float URLReplayBuffer::GetAverageReward() const
{
	if (Experiences.Num() == 0)
	{
		return 0.0f;
	}

	float TotalReward = 0.0f;
	for (const FRLExperience& Exp : Experiences)
	{
		TotalReward += Exp.Reward;
	}

	return TotalReward / Experiences.Num();
}

int32 URLReplayBuffer::GetTerminalCount() const
{
	int32 Count = 0;
	for (const FRLExperience& Exp : Experiences)
	{
		if (Exp.bTerminal)
		{
			Count++;
		}
	}
	return Count;
}

TArray<int32> URLReplayBuffer::GetActionDistribution() const
{
	// NOTE: Action distribution not applicable for continuous action space (FTacticalAction)
	// This method was designed for discrete actions and is kept for compatibility
	TArray<int32> Distribution;

	// Return empty distribution for continuous action space
	// TODO: Consider implementing action statistics for continuous actions (e.g., avg values per dimension)

	return Distribution;
}

// ========================================
// Helper Functions
// ========================================

float URLReplayBuffer::CalculateInitialPriority() const
{
	if (!bUsePrioritizedReplay || Priorities.Num() == 0)
	{
		return 1.0f;
	}

	// Use max priority to ensure new experiences are sampled
	float MaxPriority = PriorityEpsilon;
	for (float Priority : Priorities)
	{
		if (Priority > MaxPriority)
		{
			MaxPriority = Priority;
		}
	}

	return MaxPriority;
}

void URLReplayBuffer::UpdateSumTree(int32 Index)
{
	if (!bUsePrioritizedReplay || Index < 0 || Index >= Priorities.Num())
	{
		return;
	}

	// Efficient sum tree update: only update affected nodes
	// Sum tree is stored as cumulative sums, so we need to update from Index onwards

	if (SumTree.Num() != Priorities.Num())
	{
		// Tree not initialized, rebuild from scratch
		RebuildSumTree();
		return;
	}

	// Recalculate cumulative sum from the updated index
	float PrevSum = (Index > 0) ? SumTree[Index - 1] : 0.0f;

	for (int32 i = Index; i < Priorities.Num(); ++i)
	{
		PrevSum += FMath::Pow(Priorities[i], PriorityAlpha);
		SumTree[i] = PrevSum;
	}
}

void URLReplayBuffer::RebuildSumTree()
{
	if (!bUsePrioritizedReplay)
	{
		return;
	}

	// Simple sum tree: just store cumulative priorities
	SumTree.Empty();
	float CumulativeSum = 0.0f;

	for (float Priority : Priorities)
	{
		CumulativeSum += FMath::Pow(Priority, PriorityAlpha);
		SumTree.Add(CumulativeSum);
	}
}

int32 URLReplayBuffer::SampleProportionalIndex() const
{
	if (SumTree.Num() == 0 || Priorities.Num() == 0)
	{
		return FMath::RandRange(0, FMath::Max(0, Experiences.Num() - 1));
	}

	// Sample from [0, TotalPriority)
	float TotalPriority = SumTree.Last();
	float RandomValue = FMath::FRand() * TotalPriority;

	// Binary search in sum tree
	int32 Left = 0;
	int32 Right = SumTree.Num() - 1;

	while (Left < Right)
	{
		int32 Mid = (Left + Right) / 2;
		if (SumTree[Mid] < RandomValue)
		{
			Left = Mid + 1;
		}
		else
		{
			Right = Mid;
		}
	}

	return FMath::Clamp(Left, 0, Experiences.Num() - 1);
}

float URLReplayBuffer::CalculateImportanceWeight(int32 Index, float TotalPriority) const
{
	if (Index < 0 || Index >= Priorities.Num() || TotalPriority <= 0.0f)
	{
		return 1.0f;
	}

	float Priority = FMath::Pow(Priorities[Index], PriorityAlpha);
	float Probability = Priority / TotalPriority;
	float Weight = FMath::Pow(Experiences.Num() * Probability, -ImportanceSamplingBeta);

	return Weight;
}
