// Copyright Epic Games, Inc. All Rights Reserved.

#include "Schola/Logging/MocTransitionLogger.h"
#include "Characters/MocCharacter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

UMocTransitionLogger::UMocTransitionLogger()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMocTransitionLogger::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<AMocCharacter>(GetOwner());

	// Create output directory if it doesn't exist
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString FullPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() + OutputDirectory);

	if (!PlatformFile.DirectoryExists(*FullPath))
	{
		PlatformFile.CreateDirectoryTree(*FullPath);
		UE_LOG(LogTemp, Log, TEXT("MocTransitionLogger: Created directory %s"), *FullPath);
	}

	TransitionBuffer.Reserve(FlushBatchSize);
}

void UMocTransitionLogger::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Flush any remaining transitions
	if (TransitionBuffer.Num() > 0)
	{
		FlushToDisk();
	}

	// Close file handle
	if (FileHandle)
	{
		delete FileHandle;
		FileHandle = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void UMocTransitionLogger::LogTransition(
	const FObservation& State,
	EStrategyType OptionType,
	const FVector& TargetPosition,
	float OptionDuration,
	const TArray<float>& EQSWeights,
	float Reward,
	const FObservation& NextState,
	bool bDone)
{
	if (!bLoggingEnabled)
	{
		return;
	}

	FMocTransition Transition;
	Transition.State = State;
	Transition.OptionType = OptionType;
	Transition.TargetPosition = TargetPosition;
	Transition.OptionDuration = OptionDuration;
	Transition.EQSWeights = EQSWeights;
	Transition.Reward = Reward;
	Transition.NextState = NextState;
	Transition.bDone = bDone;
	Transition.Timestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	Transition.AgentID = OwnerCharacter ? OwnerCharacter->AgentID : -1;
	Transition.TeamID = OwnerCharacter ? OwnerCharacter->GetTeamID_Implementation() : -1;
	Transition.EpisodeID = CurrentEpisodeID;

	TransitionBuffer.Add(Transition);
	TotalTransitions++;
	EpisodeTransitions++;

	// Flush if buffer full
	if (TransitionBuffer.Num() >= FlushBatchSize)
	{
		FlushToDisk();
	}

	UE_LOG(LogTemp, VeryVerbose, TEXT("MocTransitionLogger: Logged transition %d (Episode %d, Agent %d)"),
		TotalTransitions, CurrentEpisodeID, Transition.AgentID);
}

void UMocTransitionLogger::StartEpisode()
{
	CurrentEpisodeID++;
	EpisodeTransitions = 0;

	UE_LOG(LogTemp, Log, TEXT("MocTransitionLogger: Started episode %d"), CurrentEpisodeID);
}

void UMocTransitionLogger::EndEpisode(bool bTeamWon)
{
	// Flush remaining transitions
	if (TransitionBuffer.Num() > 0)
	{
		FlushToDisk();
	}

	UE_LOG(LogTemp, Log, TEXT("MocTransitionLogger: Ended episode %d (%s, %d transitions)"),
		CurrentEpisodeID,
		bTeamWon ? TEXT("Victory") : TEXT("Defeat"),
		EpisodeTransitions);
}

void UMocTransitionLogger::FlushToDisk()
{
	if (TransitionBuffer.Num() == 0)
	{
		return;
	}

	WriteToCSV(TransitionBuffer);
	TransitionBuffer.Empty();

	UE_LOG(LogTemp, Verbose, TEXT("MocTransitionLogger: Flushed %d transitions to disk"), TransitionBuffer.Num());
}

void UMocTransitionLogger::WriteToCSV(const TArray<FMocTransition>& Transitions)
{
	FString FilePath = GetCSVFilePath();

	// Check if file exists to determine if we need header
	bool bFileExists = FPlatformFileManager::Get().GetPlatformFile().FileExists(*FilePath);

	// Build CSV content
	FString CSVContent;

	// Write header if new file
	if (!bFileExists)
	{
		CSVContent += TEXT("EpisodeID,AgentID,TeamID,Timestamp,");

		// State columns (52-dim)
		for (int32 i = 0; i < 52; ++i)
		{
			CSVContent += FString::Printf(TEXT("State_%d,"), i);
		}

		CSVContent += TEXT("OptionType,TargetX,TargetY,TargetZ,OptionDuration,");

		// EQS weights (7-dim)
		for (int32 i = 0; i < 7; ++i)
		{
			CSVContent += FString::Printf(TEXT("EQSWeight_%d,"), i);
		}

		CSVContent += TEXT("Reward,");

		// NextState columns (52-dim)
		for (int32 i = 0; i < 52; ++i)
		{
			CSVContent += FString::Printf(TEXT("NextState_%d,"), i);
		}

		CSVContent += TEXT("Done\n");
	}

	// Write data rows
	for (const FMocTransition& Trans : Transitions)
	{
		// Episode metadata
		CSVContent += FString::Printf(TEXT("%d,%d,%d,%.3f,"),
			Trans.EpisodeID,
			Trans.AgentID,
			Trans.TeamID,
			Trans.Timestamp);

		// State
		CSVContent += SerializeObservation(Trans.State) + TEXT(",");

		// Option
		CSVContent += FString::Printf(TEXT("%d,"), static_cast<int32>(Trans.OptionType));

		// Target position
		CSVContent += FString::Printf(TEXT("%.2f,%.2f,%.2f,"),
			Trans.TargetPosition.X,
			Trans.TargetPosition.Y,
			Trans.TargetPosition.Z);

		// Option duration
		CSVContent += FString::Printf(TEXT("%.2f,"), Trans.OptionDuration);

		// EQS weights
		for (int32 i = 0; i < Trans.EQSWeights.Num(); ++i)
		{
			CSVContent += FString::Printf(TEXT("%.4f,"), Trans.EQSWeights[i]);
		}

		// Pad if less than 8 weights
		for (int32 i = Trans.EQSWeights.Num(); i < 8; ++i)
		{
			CSVContent += TEXT("0.0,");
		}

		// Reward
		CSVContent += FString::Printf(TEXT("%.4f,"), Trans.Reward);

		// Next state
		CSVContent += SerializeObservation(Trans.NextState) + TEXT(",");

		// Done
		CSVContent += FString::Printf(TEXT("%d\n"), Trans.bDone ? 1 : 0);
	}

	// Append to file
	FFileHelper::SaveStringToFile(
		CSVContent,
		*FilePath,
		FFileHelper::EEncodingOptions::AutoDetect,
		&IFileManager::Get(),
		FILEWRITE_Append
	);
}

FString UMocTransitionLogger::SerializeObservation(const FObservation& Obs) const
{
	TArray<float> ObsArray = Obs.ToArray();
	FString Result;

	for (int32 i = 0; i < ObsArray.Num(); ++i)
	{
		Result += FString::Printf(TEXT("%.4f"), ObsArray[i]);
		if (i < ObsArray.Num() - 1)
		{
			Result += TEXT(",");
		}
	}

	// Pad to 52 dimensions if necessary
	while (ObsArray.Num() < 52)
	{
		Result += TEXT(",0.0");
		ObsArray.Add(0.0f);
	}

	return Result;
}

FString UMocTransitionLogger::GetCSVFilePath() const
{
	FString FileName = FString::Printf(TEXT("moc_transitions_episode_%d.csv"), CurrentEpisodeID);
	FString FullPath = FPaths::ProjectDir() + OutputDirectory + TEXT("/") + FileName;
	return FPaths::ConvertRelativePathToFull(FullPath);
}
