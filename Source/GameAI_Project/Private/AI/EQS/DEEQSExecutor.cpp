

#include "AI/EQS/DEEQSExecutor.h"
#include "Characters/DECharacter.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "Misc/Optional.h"


UDEEQSExecutor::UDEEQSExecutor()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDEEQSExecutor::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<ADECharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		UE_LOG(LogTemp, Error, TEXT("DEEQSExecutor: Owner is not a DECharacter!"));
	}

	if (!TacticalMovementQuery)
	{
		UE_LOG(LogTemp, Warning, TEXT("DEEQSExecutor: No TacticalMovementQuery assigned! Assign EQ_TacticalMovement in Blueprint."));
	}
}

void UDEEQSExecutor::SetQueryTemplate(UEnvQuery* NewTemplate)
{
	TacticalMovementQuery = NewTemplate;
}

void UDEEQSExecutor::ApplyWeightsToRequest(FEnvQueryRequest& Request, const FDEEQSWeightParameters& Weights)
{
	Request.SetFloatParam(TEXT("EnemyObjectiveProximity"), Weights.EnemyObjectiveProximity * WeightScale);
	Request.SetFloatParam(TEXT("AllyObjectiveProximity"), Weights.AllyObjectiveProximity * WeightScale);
	Request.SetFloatParam(TEXT("CoverDensity"), Weights.CoverDensity * WeightScale);
	Request.SetFloatParam(TEXT("EnemyVisibility"), Weights.EnemyVisibility * WeightScale);
	Request.SetFloatParam(TEXT("AllyProximity"), Weights.AllyProximity * WeightScale);
	Request.SetFloatParam(TEXT("CombatRange"), Weights.CombatRange * WeightScale);
	Request.SetFloatParam(TEXT("SearchRadius"), SearchRadius);
}

TOptional<FVector> UDEEQSExecutor::ExtractBestLocation(TSharedPtr<FEnvQueryResult> Result)
{
	if (!Result.IsValid() || !Result->IsSuccessful() || Result->Items.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("DEEQSExecutor: Query failed or returned no items"));
		return {};
	}

	LastBestLocation = Result->GetItemAsLocation(0);

	UE_LOG(LogTemp, Verbose, TEXT("DEEQSExecutor: Best location: %s (%d candidates)"),
		*LastBestLocation.ToString(), Result->Items.Num());

	return LastBestLocation;
}

TOptional<FVector> UDEEQSExecutor::ExecuteSynchronousQuery(const FDEEQSWeightParameters& Weights)
{
	if (!TacticalMovementQuery || !OwnerCharacter)
	{
		UE_LOG(LogTemp, Error, TEXT("[DEEQSExecutor] %s: Cannot execute query — Template=%s Owner=%s"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("?"),
			TacticalMovementQuery ? *TacticalMovementQuery->GetName() : TEXT("NULL"),
			OwnerCharacter ? *OwnerCharacter->GetName() : TEXT("NULL"));
		return {};
	}

	UEnvQueryManager* EQSManager = UEnvQueryManager::GetCurrent(GetWorld());
	if (!EQSManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[DEEQSExecutor] %s: EQS Manager not available"), *OwnerCharacter->GetName());
		return {};
	}

	FEnvQueryRequest QueryRequest(TacticalMovementQuery, OwnerCharacter);
	ApplyWeightsToRequest(QueryRequest, Weights);

	static int32 QueryCount = 0;
	double StartTime = FPlatformTime::Seconds();
	TSharedPtr<FEnvQueryResult> Result = EQSManager->RunInstantQuery(QueryRequest, EEnvQueryRunMode::AllMatching);
	LastQueryDuration = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);

	// Log first 5 queries per executor to confirm EQS is running and returning results
	QueryCount++;
	if (QueryCount <= 5)
	{
		const bool bSuccess = Result.IsValid() && Result->IsSuccessful();
		UE_LOG(LogTemp, Warning, TEXT("[DEEQSExecutor] %s Query#%d: Template=%s Items=%d Success=%s Duration=%.2fms Weights=[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]"),
			*OwnerCharacter->GetName(), QueryCount,
			*TacticalMovementQuery->GetName(),
			bSuccess ? Result->Items.Num() : 0,
			bSuccess ? TEXT("YES") : TEXT("NO"),
			LastQueryDuration,
			Weights.EnemyObjectiveProximity, Weights.AllyObjectiveProximity,
			Weights.CoverDensity, Weights.EnemyVisibility,
			Weights.AllyProximity, Weights.CombatRange);
		if (bSuccess && Result->Items.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[DEEQSExecutor] %s Query#%d: BestLocation=%s"),
				*OwnerCharacter->GetName(), QueryCount,
				*Result->GetItemAsLocation(0).ToString());
		}
	}

	return ExtractBestLocation(Result);
}

void UDEEQSExecutor::ExecuteTacticalQuery(
	const FDEEQSWeightParameters& Weights,
	FOnEQSQueryComplete OnComplete)
{
	if (!TacticalMovementQuery || !OwnerCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("DEEQSExecutor: Cannot execute query - missing template or owner"));
		OnComplete.ExecuteIfBound({});
		return;
	}

	PendingCallback = OnComplete;

	FEnvQueryRequest QueryRequest(TacticalMovementQuery, OwnerCharacter);
	ApplyWeightsToRequest(QueryRequest, Weights);

	double StartTime = FPlatformTime::Seconds();
	QueryRequest.Execute(
		EEnvQueryRunMode::AllMatching,
		FQueryFinishedSignature::CreateUObject(this, &UDEEQSExecutor::OnQueryFinished)
	);
	LastQueryDuration = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);
}

void UDEEQSExecutor::OnQueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
	TOptional<FVector> Location = ExtractBestLocation(Result);
	PendingCallback.ExecuteIfBound(Location);
}
