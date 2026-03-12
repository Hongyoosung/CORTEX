// Copyright Epic Games, Inc. All Rights Reserved.

#include "EQS/DynamicEQSExecutor.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

UDynamicEQSExecutor::UDynamicEQSExecutor()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDynamicEQSExecutor::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Log, TEXT("DynamicEQSExecutor: Ready on '%s'."), *GetOwner()->GetName());
}

void UDynamicEQSExecutor::ApplyWeightsToRequest(FEnvQueryRequest& Request)
{
	const TArray<float>& WeightArray = CurrentWeights.Weights;
	for (int32 i = 0; i < WeightArray.Num(); ++i)
	{
		FName ParamName = (WeightParamNames.IsValidIndex(i) && !WeightParamNames[i].IsNone())
			? WeightParamNames[i]
			: *FString::Printf(TEXT("Weight%d"), i);

		Request.SetFloatParam(ParamName, WeightArray[i] * WeightScaleFactor);
	}
}

void UDynamicEQSExecutor::SetWeights(const FDynamicEQSWeightParameters& InWeights)
{
	CurrentWeights = InWeights;
}

FVector UDynamicEQSExecutor::GetLastResult() const
{
	return LastQueryResult;
}

void UDynamicEQSExecutor::ExecuteQuery(FOnDynamicEQSQueryComplete OnComplete)
{
	if (!QueryTemplate)
	{
		UE_LOG(LogTemp, Warning, TEXT("DynamicEQSExecutor: No QueryTemplate set on '%s'."), *GetOwner()->GetName());
		OnComplete.ExecuteIfBound(TOptional<FVector>());
		return;
	}

	UEnvQueryManager* QueryManager = UEnvQueryManager::GetCurrent(GetWorld());
	if (!QueryManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("DynamicEQSExecutor: Could not get UEnvQueryManager."));
		OnComplete.ExecuteIfBound(TOptional<FVector>());
		return;
	}

	PendingCallback = OnComplete;

	FEnvQueryRequest QueryRequest(QueryTemplate, GetOwner());

	ApplyWeightsToRequest(QueryRequest);

	QueryRequest.Execute(EEnvQueryRunMode::SingleResult, this,
		&UDynamicEQSExecutor::OnQueryFinished);
}

TOptional<FVector> UDynamicEQSExecutor::ExecuteQuerySynchronous()
{
	if (!QueryTemplate)
	{
		UE_LOG(LogTemp, Warning, TEXT("DynamicEQSExecutor: No QueryTemplate set (sync query)."));
		return TOptional<FVector>();
	}

	UEnvQueryManager* QueryManager = UEnvQueryManager::GetCurrent(GetWorld());
	if (!QueryManager)
	{
		return TOptional<FVector>();
	}

	FEnvQueryRequest QueryRequest(QueryTemplate, GetOwner());
	ApplyWeightsToRequest(QueryRequest);

	TSharedPtr<FEnvQueryResult> Result = QueryManager->RunInstantQuery(QueryRequest,
		EEnvQueryRunMode::SingleResult);

	if (Result.IsValid() && Result->IsSuccessful() && Result->Items.Num() > 0)
	{
		FVector BestLoc = Result->GetItemAsLocation(0);
		LastQueryResult = BestLoc;
		return TOptional<FVector>(BestLoc);
	}

	return TOptional<FVector>();
}

void UDynamicEQSExecutor::OnQueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
	TOptional<FVector> OutLoc;

	if (Result.IsValid() && Result->IsSuccessful() && Result->Items.Num() > 0)
	{
		FVector BestLoc = Result->GetItemAsLocation(0);
		LastQueryResult = BestLoc;
		OutLoc = BestLoc;
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("DynamicEQSExecutor: Query returned no results."));
	}

	PendingCallback.ExecuteIfBound(OutLoc);
	PendingCallback.Unbind();
}
