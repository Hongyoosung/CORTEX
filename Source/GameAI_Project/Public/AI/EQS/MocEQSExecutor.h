
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "Types/EQSTypes.h"
#include "MocEQSExecutor.generated.h"

class UEnvQuery;
class AMocCharacter;

/**
 * Executes EQS queries with dynamic RL-generated weights.
 *
 * Pipeline: RL Policy → 8-dim Weights → EQS Query → Best Location → Navigation
 *
 * Key Features:
 * - Applies strategy-specific weights to EQS tests
 * - Guarantees navigable results via NavMesh integration
 * - 48-sample circular generation (1500 unit radius)
 */
UCLASS(ClassGroup=(MOC), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UMocEQSExecutor : public UActorComponent
{
	GENERATED_BODY()

public:
	UMocEQSExecutor();

protected:
	virtual void BeginPlay() override;

public:
	/**
	 * Execute tactical movement query with RL weights
	 *
	 * @param Weights - 8-dimensional weights from RL policy output
	 * @param OnComplete - Callback with best location (FVector::ZeroVector if failed)
	 */
	void ExecuteTacticalQuery(
		const FEQSWeightParameters& Weights,
		FQueryFinishedSignature OnComplete
	);


	/**
	 * Set the EQS query template to use
	 * Default: EQ_TacticalMovement (configured in Blueprint)
	 */
	void SetQueryTemplate(UEnvQuery* NewTemplate);



protected:
	/** EQS Query Template - configured in editor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS")
	UEnvQuery* TacticalMovementQuery;

	/** Cached owner character */
	UPROPERTY()
	AMocCharacter* OwnerCharacter;

	/** Current query ID for tracking async queries */
	int32 CurrentQueryID = INDEX_NONE;

	/** Last computed best location (for debugging) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Debug")
	FVector LastBestLocation;

	/** Last query execution time (ms) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Debug")
	float LastQueryDuration = 0.0f;

private:
	/**
	 * Apply weights to query instance dynamically
	 * This modifies the test weights at runtime based on RL output
	 */
	void ApplyWeightsToQuery(FEnvQueryInstance& QueryInstance, const FEQSWeightParameters& Weights);

	/**
	 * Callback for async query completion
	 */
	void OnQueryFinished(TSharedPtr<FEnvQueryResult> Result);
};
