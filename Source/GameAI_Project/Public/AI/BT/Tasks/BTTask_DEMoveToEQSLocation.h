#pragma once

#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "Types/DEEQSTypes.h"
#include "BTTask_DEMoveToEQSLocation.generated.h"


/**
 * BTTask_MoveToEQSLocation
 *
 * Inference-mode movement task. Reads EQS weights from Blackboard,
 * runs a weighted EQS query, then commands MoveToLocation.
 *
 * Uses bCreateNodeInstance=true so each BT component gets its own task
 * instance — member variables are safe for per-agent state.
 *
 * Stays InProgress indefinitely. TickTask polls the PathFollowingComponent:
 * when movement is idle and no EQS query is pending, issues a fresh query.
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API UBTTask_DEMoveToEQSLocation : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_DEMoveToEQSLocation()
	{
		bNotifyTick = true;
		bCreateNodeInstance = true;
	}

	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory
	) override
	{
		AAIController* AIController = Cast<AAIController>(OwnerComp.GetAIOwner());
		if (!AIController || !QueryTemplate || !OwnerComp.GetBlackboardComponent())
		{
			UE_LOG(LogTemp, Error, TEXT("[DEMoveToEQS] ExecuteTask: missing AIController, QueryTemplate, or Blackboard"));
			return EBTNodeResult::Failed;
		}

		CachedOwnerComp = &OwnerComp;
		bQueryPending = false;
		MoveRequestID = FAIRequestID::InvalidRequest;
		QueryCount = 0;
		AlreadyAtGoalCount = 0;

		IssueEQSQuery();

		return EBTNodeResult::InProgress;
	}

	virtual void TickTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory,
		float DeltaSeconds
	) override
	{
		if (bQueryPending)
		{
			return;
		}

		AAIController* AIController = Cast<AAIController>(OwnerComp.GetAIOwner());
		UPathFollowingComponent* PFC = AIController ? AIController->GetPathFollowingComponent() : nullptr;
		if (!PFC)
		{
			return;
		}

		const bool bIsMoving = (PFC->GetStatus() == EPathFollowingStatus::Moving)
		                     && MoveRequestID.IsValid()
		                     && (PFC->GetCurrentRequestId() == MoveRequestID);

		if (!bIsMoving)
		{
			MoveRequestID = FAIRequestID::InvalidRequest;
			IssueEQSQuery();
		}
	}

	virtual EBTNodeResult::Type AbortTask(
		UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory
	) override
	{
		AAIController* AIController = Cast<AAIController>(OwnerComp.GetAIOwner());
		if (AIController)
		{
			AIController->StopMovement();
		}
		CachedOwnerComp = nullptr;
		bQueryPending = false;
		MoveRequestID = FAIRequestID::InvalidRequest;
		return EBTNodeResult::Aborted;
	}

	/** EQS query template — assign in BT asset */
	UPROPERTY(EditAnywhere, Category = "EQS")
	UEnvQuery* QueryTemplate = nullptr;

	/** Acceptance radius for MoveToLocation (UE units) */
	UPROPERTY(EditAnywhere, Category = "EQS")
	float AcceptanceRadius = 150.0f;

	/** Minimum distance a target must be from agent to issue MoveTo.
	 *  Targets closer than this are skipped to prevent AlreadyAtGoal loops. */
	UPROPERTY(EditAnywhere, Category = "EQS")
	float MinMoveDistance = 200.0f;

private:
	void IssueEQSQuery()
	{
		if (!CachedOwnerComp) return;

		AAIController* AIController = Cast<AAIController>(CachedOwnerComp->GetAIOwner());
		UBlackboardComponent* BB = CachedOwnerComp->GetBlackboardComponent();
		if (!AIController || !BB || !QueryTemplate)
		{
			return;
		}

		FDEEQSWeightParameters Weights;
		Weights.EnemyObjectiveProximity = BB->GetValueAsFloat(TEXT("Weight_EnemyObj"));
		Weights.AllyObjectiveProximity  = BB->GetValueAsFloat(TEXT("Weight_AllyObj"));
		Weights.CoverDensity            = BB->GetValueAsFloat(TEXT("Weight_Cover"));
		Weights.EnemyVisibility         = BB->GetValueAsFloat(TEXT("Weight_EnemyVis"));
		Weights.AllyProximity           = BB->GetValueAsFloat(TEXT("Weight_AllyProx"));
		Weights.CombatRange             = BB->GetValueAsFloat(TEXT("Weight_Range"));

		QueryCount++;
		if (QueryCount == 1 || QueryCount % 50 == 0)
		{
			FString EntityName = AIController->GetPawn() ? AIController->GetPawn()->GetName() : TEXT("Unknown");
			UE_LOG(LogTemp, Warning,
				TEXT("[DEMoveToEQS] Query #%d [%s]: EO=%.2f AO=%.2f C=%.2f EV=%.2f AP=%.2f R=%.2f"),
				QueryCount, *EntityName,
				Weights.EnemyObjectiveProximity, Weights.AllyObjectiveProximity, Weights.CoverDensity,
				Weights.EnemyVisibility, Weights.AllyProximity, Weights.CombatRange);
		}

		auto Normalize = [](float W) { return FMath::Clamp(W * 2.0f, -2.0f, 2.0f); };

		UObject* OwnerPawn = Cast<UObject>(AIController->GetPawn());
		FEnvQueryRequest QueryRequest(QueryTemplate, OwnerPawn);
		QueryRequest.SetFloatParam(TEXT("EnemyObjectiveProximity"), Normalize(Weights.EnemyObjectiveProximity));
		QueryRequest.SetFloatParam(TEXT("AllyObjectiveProximity"),  Normalize(Weights.AllyObjectiveProximity));
		QueryRequest.SetFloatParam(TEXT("CoverDensity"),            Normalize(Weights.CoverDensity));
		QueryRequest.SetFloatParam(TEXT("EnemyVisibility"),         Normalize(Weights.EnemyVisibility));
		QueryRequest.SetFloatParam(TEXT("AllyProximity"),           Normalize(Weights.AllyProximity));
		QueryRequest.SetFloatParam(TEXT("CombatRange"),             Normalize(Weights.CombatRange));

		bQueryPending = true;
		FQueryFinishedSignature Delegate;
		Delegate.BindUObject(this, &UBTTask_DEMoveToEQSLocation::OnQueryFinished);
		QueryRequest.Execute(EEnvQueryRunMode::SingleResult, Delegate);
	}

	void OnQueryFinished(TSharedPtr<FEnvQueryResult> Result)
	{
		bQueryPending = false;

		if (!CachedOwnerComp)
		{
			return;
		}

		if (!Result || !Result->IsSuccessful())
		{
			UE_LOG(LogTemp, Warning, TEXT("[DEMoveToEQS] EQS returned no result (query #%d)"), QueryCount);
			return;
		}

		AAIController* AIController = Cast<AAIController>(CachedOwnerComp->GetAIOwner());
		if (!AIController || !AIController->GetPawn())
		{
			return;
		}

		const FVector TargetLocation = Result->GetItemAsLocation(0);
		const FVector CurrentLocation = AIController->GetPawn()->GetActorLocation();
		const float DistToTarget = FVector::Dist(CurrentLocation, TargetLocation);

		// Skip targets that are too close — they cause AlreadyAtGoal loops
		if (DistToTarget < MinMoveDistance)
		{
			AlreadyAtGoalCount++;
			if (AlreadyAtGoalCount == 1 || AlreadyAtGoalCount % 20 == 0)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[DEMoveToEQS] Target too close (%.0f < %.0f) for [%s] — skipping (count=%d)"),
					DistToTarget, MinMoveDistance,
					*AIController->GetPawn()->GetName(), AlreadyAtGoalCount);
			}
			return;
		}

		AlreadyAtGoalCount = 0;

		FAIMoveRequest MoveReq(TargetLocation);
		MoveReq.SetAcceptanceRadius(AcceptanceRadius);
		MoveReq.SetUsePathfinding(true);

		FPathFollowingRequestResult MoveResult = AIController->MoveTo(MoveReq);

		if (MoveResult.Code == EPathFollowingRequestResult::RequestSuccessful)
		{
			MoveRequestID = MoveResult.MoveId;
		}
		else if (MoveResult.Code == EPathFollowingRequestResult::AlreadyAtGoal)
		{
			AlreadyAtGoalCount++;
			if (AlreadyAtGoalCount == 1 || AlreadyAtGoalCount % 20 == 0)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[DEMoveToEQS] AlreadyAtGoal for [%s] (dist=%.0f, count=%d)"),
					*AIController->GetPawn()->GetName(), DistToTarget, AlreadyAtGoalCount);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[DEMoveToEQS] MoveTo failed (Code=%d) for [%s]"),
				static_cast<int32>(MoveResult.Code), *AIController->GetPawn()->GetName());
		}
	}

	UPROPERTY()
	TObjectPtr<UBehaviorTreeComponent> CachedOwnerComp = nullptr;

	FAIRequestID MoveRequestID = FAIRequestID::InvalidRequest;
	bool bQueryPending = false;
	int32 QueryCount = 0;
	int32 AlreadyAtGoalCount = 0;
};
