
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "Types/DEEQSTypes.h"
#include "DEEQSExecutor.generated.h"

class UEnvQuery;
class ADECharacter;
struct FEnvQueryRequest;


DECLARE_DELEGATE_OneParam(FOnEQSQueryComplete, TOptional<FVector>);

/**
 * Executes EQS queries with dynamic RL-generated weights.
 *
 * Pipeline: RL Policy → 7-dim Weights → EQS Query → Best Location → Navigation
 *
 * Key Features:
 * - Applies strategy-specific weights to EQS tests via named parameters
 * - Guarantees navigable results via NavMesh integration
 * - 48-sample circular generation
 */
UCLASS(ClassGroup=(MOC), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UDEEQSExecutor : public UActorComponent
{
	GENERATED_BODY()

public:
	UDEEQSExecutor();

protected:
	virtual void BeginPlay() override;

public:
	/**
	 * Execute tactical movement query asynchronously (for BT/inference mode)
	 *
	 * @param Weights - 7-dimensional weights from RL policy output
	 * @param OnComplete - Callback with best location result
	 */
	void ExecuteTacticalQuery(
		const FDEEQSWeightParameters& Weights,
		FOnEQSQueryComplete OnComplete
	);

	/**
	 * Execute tactical movement query synchronously (for training mode)
	 *
	 * @param Weights - 7-dimensional weights from RL policy output
	 * @return Best tactical location, or empty if query failed
	 */
	TOptional<FVector> ExecuteSynchronousQuery(const FDEEQSWeightParameters& Weights);

	/**
	 * Set the EQS query template to use
	 * Default: EQ_TacticalMovement (configured in Blueprint)
	 */
	void SetQueryTemplate(UEnvQuery* NewTemplate);

	/** EQS Query Template - configured in editor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS")
	UEnvQuery* TacticalMovementQuery;

	/** EQS search radius (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EQS")
	float SearchRadius = 2000.0f;

protected:
	/** Cached owner character */
	UPROPERTY()
	ADECharacter* OwnerCharacter;

	/** Last computed best location (for debugging) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Debug")
	FVector LastBestLocation;

	/** Last query execution time (ms) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Debug")
	float LastQueryDuration = 0.0f;

private:
	/** Weight scale factor applied to RL outputs */
	static constexpr float WeightScale = 2.0f;

	/** Apply weight parameters to a query request */
	void ApplyWeightsToRequest(FEnvQueryRequest& Request, const FDEEQSWeightParameters& Weights);

	/** Extract best location from query result */
	TOptional<FVector> ExtractBestLocation(TSharedPtr<FEnvQueryResult> Result);

	/** Stored callback for async query */
	FOnEQSQueryComplete PendingCallback;

	/** Callback for async query completion */
	void OnQueryFinished(TSharedPtr<FEnvQueryResult> Result);
};
