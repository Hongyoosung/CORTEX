

#include "AI/EQS/MocEQSExecutor.h"
#include "Characters/MocCharacter.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "Misc/Optional.h"


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

void UMocEQSExecutor::ApplyWeightsToRequest(FEnvQueryRequest& Request, const FEQSWeightParameters& Weights)
{
	Request.SetFloatParam(TEXT("EnemyObjectiveProximity"), Weights.EnemyObjectiveProximity * WeightScale);
	Request.SetFloatParam(TEXT("AllyObjectiveProximity"), Weights.AllyObjectiveProximity * WeightScale);
	Request.SetFloatParam(TEXT("CoverDensity"), Weights.CoverDensity * WeightScale);
	Request.SetFloatParam(TEXT("EnemyVisibility"), Weights.EnemyVisibility * WeightScale);
	Request.SetFloatParam(TEXT("AllyProximity"), Weights.AllyProximity * WeightScale);
	Request.SetFloatParam(TEXT("CombatRange"), Weights.CombatRange * WeightScale);
	Request.SetFloatParam(TEXT("SearchRadius"), SearchRadius);
}

TOptional<FVector> UMocEQSExecutor::ExtractBestLocation(TSharedPtr<FEnvQueryResult> Result)
{
	if (!Result.IsValid() || !Result->IsSuccessful() || Result->Items.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("MocEQSExecutor: Query failed or returned no items"));
		return {};
	}

	LastBestLocation = Result->GetItemAsLocation(0);

	UE_LOG(LogTemp, Verbose, TEXT("MocEQSExecutor: Best location: %s (%d candidates)"),
		*LastBestLocation.ToString(), Result->Items.Num());

	return LastBestLocation;
}

TOptional<FVector> UMocEQSExecutor::ExecuteSynchronousQuery(const FEQSWeightParameters& Weights)
{
	if (!TacticalMovementQuery || !OwnerCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("MocEQSExecutor: Cannot execute query - missing template or owner"));
		return {};
	}

	UEnvQueryManager* EQSManager = UEnvQueryManager::GetCurrent(GetWorld());
	if (!EQSManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("MocEQSExecutor: EQS Manager not available"));
		return {};
	}

	FEnvQueryRequest QueryRequest(TacticalMovementQuery, OwnerCharacter);
	ApplyWeightsToRequest(QueryRequest, Weights);

	double StartTime = FPlatformTime::Seconds();
	TSharedPtr<FEnvQueryResult> Result = EQSManager->RunInstantQuery(QueryRequest, EEnvQueryRunMode::AllMatching);
	LastQueryDuration = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);

	return ExtractBestLocation(Result);
}

void UMocEQSExecutor::ExecuteTacticalQuery(
	const FEQSWeightParameters& Weights,
	FOnEQSQueryComplete OnComplete)
{
	if (!TacticalMovementQuery || !OwnerCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("MocEQSExecutor: Cannot execute query - missing template or owner"));
		OnComplete.ExecuteIfBound({});
		return;
	}

	PendingCallback = OnComplete;

	FEnvQueryRequest QueryRequest(TacticalMovementQuery, OwnerCharacter);
	ApplyWeightsToRequest(QueryRequest, Weights);

	double StartTime = FPlatformTime::Seconds();
	QueryRequest.Execute(
		EEnvQueryRunMode::AllMatching,
		FQueryFinishedSignature::CreateUObject(this, &UMocEQSExecutor::OnQueryFinished)
	);
	LastQueryDuration = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);
}

void UMocEQSExecutor::OnQueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
	TOptional<FVector> Location = ExtractBestLocation(Result);
	PendingCallback.ExecuteIfBound(Location);
}
