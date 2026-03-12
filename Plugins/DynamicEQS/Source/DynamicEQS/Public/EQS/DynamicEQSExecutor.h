// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EQS/DynamicEQSWeightParameters.h"
#include "DynamicEQSExecutor.generated.h"


struct FEnvQueryRequest;


/** Delegate called when an async EQS query completes with an optional best location. */
DECLARE_DELEGATE_OneParam(FOnDynamicEQSQueryComplete, TOptional<FVector>);

/**
 * UDynamicEQSExecutor
 * ActorComponent that owns an EQS query template and drives it with
 * a runtime-supplied weight vector. The caller sets weights via
 * SetWeights(), then calls ExecuteQuery() (async) or ExecuteQuerySynchronous().
 */
UCLASS(Blueprintable, ClassGroup = (DynamicEQS), meta = (BlueprintSpawnableComponent))
class DYNAMICEQS_API UDynamicEQSExecutor : public UActorComponent
{
	GENERATED_BODY()

public:
	UDynamicEQSExecutor();

	// -------------------------------------------------------------------------
	// Configuration
	// -------------------------------------------------------------------------

	/** EQS query asset to run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Executor")
	TObjectPtr<UEnvQuery> QueryTemplate;

	/** Radius hint passed to the query generator (if it supports it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Executor")
	float SearchRadius = 2000.0f;

	/** Global multiplier applied to all weights before submitting to EQS. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Executor")
	float WeightScaleFactor = 1.0f;

	/**
	 * Optional semantic names for each weight slot.
	 * If set, Weight[i] is submitted to EQS as WeightParamNames[i] instead of "Weight<i>".
	 * Leave empty to use the generic "Weight0", "Weight1", ... naming.
	 * Must match the float parameter names configured in the EQS query asset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DynamicEQS|Executor")
	TArray<FName> WeightParamNames;

	// -------------------------------------------------------------------------
	// Runtime API
	// -------------------------------------------------------------------------

	/** Replace the current weight vector. */
	UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Executor")
	void SetWeights(const FDynamicEQSWeightParameters& InWeights);

	/** Returns the best location produced by the last completed query. */
	UFUNCTION(BlueprintCallable, Category = "DynamicEQS|Executor")
	FVector GetLastResult() const;

	/** Run the query asynchronously; OnComplete is called on the game thread. */
	void ExecuteQuery(FOnDynamicEQSQueryComplete OnComplete);

	/**
	 * Run the query synchronously (blocks until EQS finishes).
	 * Intended for use during training where frame timing is less critical.
	 * Returns the best location, or an unset TOptional on failure.
	 */
	TOptional<FVector> ExecuteQuerySynchronous();

protected:
	virtual void BeginPlay() override;

private:
	/** Current weight parameters. */
	FDynamicEQSWeightParameters CurrentWeights;

	/** Cached result from the most recent query. */
	FVector LastQueryResult = FVector::ZeroVector;

	/** Pending async callback. */
	FOnDynamicEQSQueryComplete PendingCallback;

	/** Internal EQS finish handler. */
	void OnQueryFinished(TSharedPtr<FEnvQueryResult> Result);

	/** Apply CurrentWeights to a query request using WeightParamNames (if set) or "Weight<i>" fallback. */
	void ApplyWeightsToRequest(FEnvQueryRequest& Request);
};
