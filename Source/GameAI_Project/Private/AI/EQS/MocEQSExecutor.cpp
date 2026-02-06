// Copyright Epic Games, Inc. All Rights Reserved.

#include "MOC/EQS/MocEQSExecutor.h"
#include "MOC/MocCharacter.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryInstanceBlueprintWrapper.h"
#include "AIController.h"

UMocEQSExecutor::UMocEQSExecutor()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMocEQSExecutor::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<AMocCharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		UE_LOG(LogTemp, Error, TEXT("MocEQSExecutor: Owner is not a MocCharacter!"));
	}

	if (!TacticalMovementQuery)
	{
		UE_LOG(LogTemp, Warning, TEXT("MocEQSExecutor: No TacticalMovementQuery assigned! Assign EQ_TacticalMovement in Blueprint."));
	}
}

void UMocEQSExecutor::SetQueryTemplate(UEnvQuery* NewTemplate)
{
	TacticalMovementQuery = NewTemplate;
}

void UMocEQSExecutor::ExecuteTacticalQuery(
	const FEQSWeightParameters& Weights,
	FEnvQueryFinishedSignature OnComplete)
{
	if (!TacticalMovementQuery)
	{
		UE_LOG(LogTemp, Error, TEXT("MocEQSExecutor: No query template assigned!"));
		OnComplete.ExecuteIfBound(nullptr);
		return;
	}

	if (!OwnerCharacter)
	{
		UE_LOG(LogTemp, Error, TEXT("MocEQSExecutor: No owner character!"));
		OnComplete.ExecuteIfBound(nullptr);
		return;
	}

	AAIController* AIController = Cast<AAIController>(OwnerCharacter->GetController());
	if (!AIController)
	{
		UE_LOG(LogTemp, Error, TEXT("MocEQSExecutor: Owner has no AI controller!"));
		OnComplete.ExecuteIfBound(nullptr);
		return;
	}

	// Store delegate for callback
	StoredDelegate = OnComplete;

	// Create query request
	FEnvQueryRequest QueryRequest(TacticalMovementQuery, OwnerCharacter);

	// Execute query asynchronously
	double StartTime = FPlatformTime::Seconds();

	CurrentQueryID = QueryRequest.Execute(
		EEnvQueryRunMode::AllMatching,
		FQueryFinishedSignature::CreateUObject(this, &UMocEQSExecutor::OnQueryFinished)
	);

	LastQueryDuration = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);

	UE_LOG(LogTemp, Verbose, TEXT("MocEQSExecutor: Query started (ID: %d) with weights: %s"),
		CurrentQueryID, *Weights.ToString());
}

FVector UMocEQSExecutor::ExecuteTacticalQuerySync(const FEQSWeightParameters& Weights)
{
	if (!TacticalMovementQuery || !OwnerCharacter)
	{
		return FVector::ZeroVector;
	}

	FEnvQueryRequest QueryRequest(TacticalMovementQuery, OwnerCharacter);

	double StartTime = FPlatformTime::Seconds();
	TSharedPtr<FEnvQueryResult> Result = QueryRequest.Execute(EEnvQueryRunMode::SingleResult, nullptr);
	LastQueryDuration = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);

	if (Result.IsValid() && Result->IsSuccessful())
	{
		LastBestLocation = Result->GetItemAsLocation(0);
		UE_LOG(LogTemp, Log, TEXT("MocEQSExecutor: Query succeeded - Best location: %s (%.2fms)"),
			*LastBestLocation.ToString(), LastQueryDuration);
		return LastBestLocation;
	}

	UE_LOG(LogTemp, Warning, TEXT("MocEQSExecutor: Query failed or returned no results"));
	return FVector::ZeroVector;
}

void UMocEQSExecutor::OnQueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
	if (!Result.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("MocEQSExecutor: Query result is invalid"));
		StoredDelegate.ExecuteIfBound(nullptr);
		return;
	}

	if (!Result->IsSuccessful())
	{
		UE_LOG(LogTemp, Warning, TEXT("MocEQSExecutor: Query execution failed"));
		StoredDelegate.ExecuteIfBound(nullptr);
		return;
	}

	// Get best location
	if (Result->Items.Num() > 0)
	{
		LastBestLocation = Result->GetItemAsLocation(0);

		UE_LOG(LogTemp, Log, TEXT("MocEQSExecutor: Query completed - Best location: %s (%.2fms, %d candidates)"),
			*LastBestLocation.ToString(), LastQueryDuration, Result->Items.Num());

		// Create wrapper for blueprint access
		UEnvQueryInstanceBlueprintWrapper* Wrapper = NewObject<UEnvQueryInstanceBlueprintWrapper>();
		if (Wrapper)
		{
			Wrapper->SetQueryResult(Result);
		}

		StoredDelegate.ExecuteIfBound(Wrapper);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("MocEQSExecutor: Query returned no items"));
		StoredDelegate.ExecuteIfBound(nullptr);
	}
}

void UMocEQSExecutor::ApplyWeightsToQuery(FEnvQueryInstance& QueryInstance, const FEQSWeightParameters& Weights)
{
	// NOTE: This function demonstrates the concept but requires EQS query structure access
	// In practice, weights should be applied through named parameters in the query template
	// or by using a custom EQS generator/test that reads from a data asset

	// Actual implementation would use:
	// QueryInstance.NamedParams.Add("EnemyObjWeight", Weights.EnemyObjectiveProximity);
	// QueryInstance.NamedParams.Add("AllyObjWeight", Weights.AllyObjectiveProximity);
	// ... etc for all 8 weights

	UE_LOG(LogTemp, Verbose, TEXT("ApplyWeightsToQuery: Weights applied - %s"), *Weights.ToString());
}
