// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/Training/TeamDataCollector.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/BufferArchive.h"

UTeamDataCollector::UTeamDataCollector()
	: bIsRecording(false)
	, OutputDirectory(TEXT("TrainingData/TeamTransitions"))
	, OutputFormat(TEXT("CSV"))
	, MaxSamplesPerFile(10000)
	, AutoSaveIntervalSeconds(300.0f) // 5 minutes
	, CurrentMatchID(0)
	, TotalMatchesRecorded(0)
	, TimeSinceLastSave(0.0f)
	, FileCounter(0)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 1.0f; // Check auto-save every second
}

void UTeamDataCollector::BeginPlay()
{
	Super::BeginPlay();

	// Create output directory if it doesn't exist
	const FString FullPath = FPaths::ProjectContentDir() / OutputDirectory;
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*FullPath))
	{
		PlatformFile.CreateDirectoryTree(*FullPath);
		UE_LOG(LogTemp, Log, TEXT("TeamDataCollector: Created output directory: %s"), *FullPath);
	}
}

void UTeamDataCollector::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Auto-save functionality
	if (bIsRecording && AutoSaveIntervalSeconds > 0.0f)
	{
		TimeSinceLastSave += DeltaTime;

		if (TimeSinceLastSave >= AutoSaveIntervalSeconds && CollectedSamples.Num() > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("TeamDataCollector: Auto-saving %d samples..."), CollectedSamples.Num());
			SaveToDisk();
			ClearSamples();
			TimeSinceLastSave = 0.0f;
		}
	}
}

void UTeamDataCollector::BeginRecording(int32 InMatchID)
{
	CurrentMatchID = InMatchID > 0 ? InMatchID : FMath::RandRange(1000, 9999);
	bIsRecording = true;
	TimeSinceLastSave = 0.0f;

	UE_LOG(LogTemp, Log, TEXT("TeamDataCollector: Started recording (MatchID: %d)"), CurrentMatchID);
}

int32 UTeamDataCollector::EndRecording(bool bSaveImmediately)
{
	bIsRecording = false;
	const int32 SampleCount = CollectedSamples.Num();

	if (bSaveImmediately && SampleCount > 0)
	{
		SaveToDisk();
	}

	TotalMatchesRecorded++;

	UE_LOG(LogTemp, Log, TEXT("TeamDataCollector: Stopped recording (Collected: %d samples)"), SampleCount);

	return SampleCount;
}

void UTeamDataCollector::RecordTransition(
	const FTeamState& CurrentState,
	ETacticalPlay TacticalPlay,
	const FTeamState& NextState,
	const FCompositeReward& Reward)
{
	if (!bIsRecording)
	{
		return;
	}

	FTeamTransitionSample Sample;
	Sample.CurrentState = CurrentState;
	Sample.TacticalPlay = TacticalPlay;
	Sample.NextState = NextState;
	Sample.Reward = Reward;
	Sample.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	Sample.MatchID = CurrentMatchID;

	CollectedSamples.Add(Sample);

	// Auto-split if max samples reached
	if (CollectedSamples.Num() >= MaxSamplesPerFile)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamDataCollector: Max samples reached, auto-saving..."));
		SaveToDisk();
		ClearSamples();
	}

	UE_LOG(LogTemp, VeryVerbose, TEXT("TeamDataCollector: Recorded transition (Total: %d)"), CollectedSamples.Num());
}

bool UTeamDataCollector::SaveToDisk(const FString& Filename)
{
	if (CollectedSamples.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamDataCollector: No samples to save"));
		return false;
	}

	const FString OutputFilename = Filename.IsEmpty() ? GenerateFilename() : Filename;
	const FString FullPath = FPaths::ProjectContentDir() / OutputDirectory / OutputFilename;

	bool bSuccess = false;

	if (OutputFormat.Equals(TEXT("Binary"), ESearchCase::IgnoreCase))
	{
		bSuccess = SaveToBinary(FullPath);
	}
	else // Default to CSV
	{
		bSuccess = SaveToCSV(FullPath);
	}

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("TeamDataCollector: Saved %d samples to %s"),
			CollectedSamples.Num(), *FullPath);
		FileCounter++;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TeamDataCollector: Failed to save to %s"), *FullPath);
	}

	return bSuccess;
}

void UTeamDataCollector::ClearSamples()
{
	CollectedSamples.Empty();
	UE_LOG(LogTemp, Verbose, TEXT("TeamDataCollector: Cleared sample buffer"));
}

FString UTeamDataCollector::GenerateFilename() const
{
	const FDateTime Now = FDateTime::Now();
	return FString::Printf(TEXT("TeamData_Match%d_%s_%03d.%s"),
		CurrentMatchID,
		*Now.ToString(TEXT("%Y%m%d_%H%M%S")),
		FileCounter,
		OutputFormat.Equals(TEXT("Binary"), ESearchCase::IgnoreCase) ? TEXT("bin") : TEXT("csv")
	);
}

bool UTeamDataCollector::SaveToCSV(const FString& Filepath)
{
	FString CSVContent;

	// Header row
	CSVContent += TEXT("MatchID,Timestamp,TacticalPlay,");

	// Current state columns (60-dim)
	for (int32 i = 0; i < 60; ++i)
	{
		CSVContent += FString::Printf(TEXT("CurState_%d,"), i);
	}

	// Next state columns (60-dim)
	for (int32 i = 0; i < 60; ++i)
	{
		CSVContent += FString::Printf(TEXT("NextState_%d,"), i);
	}

	// Reward columns
	CSVContent += TEXT("WinProb,HealthDelta,ObjectiveScore\n");

	// Data rows
	for (const FTeamTransitionSample& Sample : CollectedSamples)
	{
		// Match ID and timestamp
		CSVContent += FString::Printf(TEXT("%d,%.3f,%d,"),
			Sample.MatchID,
			Sample.Timestamp,
			static_cast<int32>(Sample.TacticalPlay)
		);

		// Current state tensor
		TArray<float> CurTensor = Sample.CurrentState.ToTensor();
		for (int32 i = 0; i < 60; ++i)
		{
			const float Value = i < CurTensor.Num() ? CurTensor[i] : 0.0f;
			CSVContent += FString::Printf(TEXT("%.6f,"), Value);
		}

		// Next state tensor
		TArray<float> NextTensor = Sample.NextState.ToTensor();
		for (int32 i = 0; i < 60; ++i)
		{
			const float Value = i < NextTensor.Num() ? NextTensor[i] : 0.0f;
			CSVContent += FString::Printf(TEXT("%.6f,"), Value);
		}

		// Reward
		CSVContent += FString::Printf(TEXT("%.6f,%.6f,%.6f\n"),
			Sample.Reward.WinProb,
			Sample.Reward.HealthDelta,
			Sample.Reward.ObjectiveScore
		);
	}

	return FFileHelper::SaveStringToFile(CSVContent, *Filepath, FFileHelper::EEncodingOptions::AutoDetect);
}

bool UTeamDataCollector::SaveToBinary(const FString& Filepath)
{
	FBufferArchive Archive;

	// Write header
	int32 Version = 1;
	int32 NumSamples = CollectedSamples.Num();
	Archive << Version;
	Archive << NumSamples;

	// Write samples
	for (FTeamTransitionSample& Sample : CollectedSamples)
	{
		// Serialize tensors and metadata
		Archive << Sample.MatchID;
		Archive << Sample.Timestamp;

		uint8 TacticalPlayByte = static_cast<uint8>(Sample.TacticalPlay);
		Archive << TacticalPlayByte;

		// Current state tensor
		TArray<float> CurTensor = Sample.CurrentState.ToTensor();
		Archive << CurTensor;

		// Next state tensor
		TArray<float> NextTensor = Sample.NextState.ToTensor();
		Archive << NextTensor;

		// Reward
		Archive << Sample.Reward.WinProb;
		Archive << Sample.Reward.HealthDelta;
		Archive << Sample.Reward.ObjectiveScore;
	}

	// Write to disk
	return FFileHelper::SaveArrayToFile(Archive, *Filepath);
}

FString UTeamDataCollector::TeamStateToCSV(const FTeamState& State) const
{
	TArray<float> Tensor = State.ToTensor();
	FString CSV;

	for (int32 i = 0; i < Tensor.Num(); ++i)
	{
		CSV += FString::Printf(TEXT("%.6f,"), Tensor[i]);
	}

	return CSV;
}

FString UTeamDataCollector::RewardToCSV(const FCompositeReward& Reward) const
{
	return FString::Printf(TEXT("%.6f,%.6f,%.6f"),
		Reward.WinProb,
		Reward.HealthDelta,
		Reward.ObjectiveScore
	);
}
