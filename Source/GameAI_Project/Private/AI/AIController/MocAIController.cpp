

#include "AI/AIController/MocAIController.h"
#include "AI/Policy/MocPolicyExecutor.h"
#include "Characters/MocCharacter.h"
#include "Types/EQSTypes.h"
#include "Types/ObservationTypes.h"
#include "Types/StrategyTypes.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Damage.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "Kismet/GameplayStatics.h"


AMocAIController::AMocAIController(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Components 생성
    AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));
    SetPerceptionComponent(*AIPerception);
    
    BehaviorTreeComp = CreateDefaultSubobject<UBehaviorTreeComponent>(TEXT("BehaviorTree"));
    BlackboardComp = CreateDefaultSubobject<UBlackboardComponent>(TEXT("Blackboard"));
    
    // CORTEX Components (v10.2 - reduced to essential executor components)
    PolicyExecutor = CreateDefaultSubobject<UMocPolicyExecutor>(TEXT("PolicyExecutor"));

    // Removed components (moved to ASquadManager in v10.2):
    // - MCTSPlanner → ASquadManager::TeamMCTSPlanner
    // - WorldModel → ASquadManager::MocTeamWorldModel
    // - ValueNetwork → Centralized evaluation
    // - EventMonitor → ASquadManager::OnCriticalEvent()
    // - EQSDynamicWeightApplicator → Inlined in CreateDynamicEQSQuery()
    
    // Perception 설정
    SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
    SightConfig->SightRadius = 3000.0f;           // 30m 시야
    SightConfig->LoseSightRadius = 3500.0f;
    SightConfig->PeripheralVisionAngleDegrees = 90.0f;
    SightConfig->DetectionByAffiliation.bDetectEnemies = true;
    SightConfig->DetectionByAffiliation.bDetectNeutrals = false;
    SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
    
    HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
    HearingConfig->HearingRange = 2000.0f;        // 20m 청각
    HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
    
    DamageConfig = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("DamageConfig"));
    
    AIPerception->ConfigureSense(*SightConfig);
    AIPerception->ConfigureSense(*HearingConfig);
    AIPerception->ConfigureSense(*DamageConfig);
    AIPerception->SetDominantSense(SightConfig->GetSenseImplementation());
    
    // Perception 콜백 바인딩
    AIPerception->OnPerceptionUpdated.AddDynamic(this, &AMocAIController::OnPerceptionUpdated);
    AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &AMocAIController::OnTargetPerceptionUpdated);
    
    // Tick 활성화
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.0167f;  // 60Hz
    
    TimeSinceLastReplan = 0.0f;
}

void AMocAIController::BeginPlay()
{
    Super::BeginPlay();

    // ONNX 모델 로드 (v10.2: Policy only, no world model/value network)
    PolicyExecutor->LoadModel(TEXT("Content/AI/Models/policy_weights.onnx"));

    UE_LOG(LogTemp, Log, TEXT("MOC AI Controller initialized (v10.2 executor mode)"));
}

void AMocAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    
    // Blackboard 및 BehaviorTree 시작
    if (BehaviorTree)
    {
        UseBlackboard(BehaviorTree->BlackboardAsset, BlackboardComp);
        RunBehaviorTree(BehaviorTree);

        UE_LOG(LogTemp, Log, TEXT("Behavior Tree started"));
    }
    
    // 초기 전략 설정 (Defend) - v10.2: 대기 상태, Squad Commander가 명령 발령
    CurrentOption = FTacticalOption(EStrategyType::Defend, 5.0f);

    // Default defensive weights (초기화용)
    FEQSWeightParameters DefaultWeights;
    DefaultWeights.EnemyObjectiveProximity = -0.7f;
    DefaultWeights.AllyObjectiveProximity = 0.9f;
    DefaultWeights.CoverDensity = 0.8f;
    DefaultWeights.EnemyVisibility = 0.3f;
    DefaultWeights.AllyProximity = 0.7f;
    DefaultWeights.CombatRange = -0.4f;
    DefaultWeights.PickupProximity = 0.2f;
    DefaultWeights.HeightAdvantage = 0.7f;

    // 초기 전략 및 가중치 설정
    if (BlackboardComp)
    {
        BlackboardComp->SetValueAsEnum(TEXT("CurrentStrategy"),
            static_cast<uint8>(CurrentOption.Strategy));
        UpdateBlackboardWeights(DefaultWeights);
    }
}

FObservation AMocAIController::BuildObservationFromPerception()
{
    FObservation Obs;

    AMocCharacter* MyChar = Cast<AMocCharacter>(GetPawn());
    if (!MyChar)
    {
        return Obs; // Return default-initialized observation
    }

    // ========================================
    // Self State (10-dim)
    // ========================================
    Obs.Position = MyChar->GetActorLocation();
    Obs.Health = MyChar->GetHealthPercentage();
    Obs.Velocity = MyChar->GetVelocity();
    Obs.WeaponCooldown = MyChar->GetWeaponCooldown_Implementation();
    Obs.CurrentStrategy = MyChar->GetCommandedStrategy();
    Obs.bIsAlive = MyChar->IsAlive();

    // ========================================
    // Gather Team Information (Allies + Enemies)
    // ========================================
    TArray<AActor*> AllCharacters;
    UGameplayStatics::GetAllActorsOfClass(
        GetWorld(),
        AMocCharacter::StaticClass(),
        AllCharacters
    );

    // Initialize arrays
    Obs.AllyPositions.Reserve(4);
    Obs.AllyHealths.Reserve(4);
    Obs.AllyStrategies.Reserve(4);
    Obs.EnemyPositions.Reserve(5);
    Obs.EnemyVisible.Reserve(5);

    int32 AllyIndex = 0;
    int32 EnemyIndex = 0;
    const int32 MyTeamID = MyChar->GetTeamID();

    for (AActor* Actor : AllCharacters)
    {
        AMocCharacter* OtherChar = Cast<AMocCharacter>(Actor);
        if (!OtherChar || OtherChar == MyChar)
        {
            continue;
        }

        if (OtherChar->GetTeamID() == MyTeamID)
        {
            // ========================================
            // Ally (max 4)
            // ========================================
            if (AllyIndex < 4)
            {
                Obs.AllyPositions.Add(OtherChar->GetActorLocation());
                Obs.AllyHealths.Add(OtherChar->GetHealthPercentage());
                Obs.AllyStrategies.Add(OtherChar->GetCommandedStrategy());
                AllyIndex++;
            }
        }
        else
        {
            // ========================================
            // Enemy (max 5)
            // ========================================
            if (EnemyIndex < 5)
            {
                Obs.EnemyPositions.Add(OtherChar->GetActorLocation());

                // Visibility check: Line of sight within vision range (3000 units = 30m)
                FVector ToEnemy = OtherChar->GetActorLocation() - MyChar->GetActorLocation();
                float Distance = ToEnemy.Size();
                bool bVisible = false;

                if (Distance < 3000.0f) // Match SightConfig radius from constructor
                {
                    FHitResult HitResult;
                    FCollisionQueryParams QueryParams;
                    QueryParams.AddIgnoredActor(MyChar);
                    QueryParams.AddIgnoredActor(OtherChar);

                    // Line trace from eye height to enemy eye height
                    bVisible = !GetWorld()->LineTraceSingleByChannel(
                        HitResult,
                        MyChar->GetActorLocation() + FVector(0, 0, 90), // Eye height offset
                        OtherChar->GetActorLocation() + FVector(0, 0, 90),
                        ECC_Visibility,
                        QueryParams
                    );
                }

                Obs.EnemyVisible.Add(bVisible);
                EnemyIndex++;
            }
        }
    }

    // Pad arrays to fixed size if needed
    while (Obs.AllyPositions.Num() < 4)
    {
        Obs.AllyPositions.Add(FVector::ZeroVector);
        Obs.AllyHealths.Add(0.0f);
        Obs.AllyStrategies.Add(EStrategyType::Assault);
    }

    while (Obs.EnemyPositions.Num() < 5)
    {
        Obs.EnemyPositions.Add(FVector::ZeroVector);
        Obs.EnemyVisible.Add(false);
    }

    // ========================================
    // Map State (2-dim)
    // ========================================
    // TODO: Implement capture point balance calculation from game mode
    Obs.CapturePointBalance = 0;

    // TODO: Get actual time remaining from game mode
    Obs.TimeRemaining = 1.0f;

    return Obs;
}

void AMocAIController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // v10.2: Agents receive commands from Squad Commander
    // No local MCTS planning

    AMocCharacter* MyChar = Cast<AMocCharacter>(GetPawn());
    if (MyChar)
    {
        // Get commanded strategy (set by SquadManager)
        EStrategyType CurrentStrategy = MyChar->GetCommandedStrategy();

        // Update Blackboard for Behavior Tree
        if (BlackboardComp)
        {
            BlackboardComp->SetValueAsEnum(TEXT("CurrentStrategy"),
                static_cast<uint8>(CurrentStrategy));
        }

        // Generate EQS weights from policy with local state adaptation (v10.2)
        if (PolicyExecutor)
        {
            // Build local observation from perception data
            FObservation LocalObs = BuildObservationFromPerception();

            // Cache for future reference
            CurrentObservation = LocalObs;

            // Generate weights using commanded strategy + local state
            FEQSWeightParameters Weights = PolicyExecutor->InferWeights(
                CurrentStrategy,
                LocalObs
            );

            UpdateBlackboardWeights(Weights);
        }
    }
}
void AMocAIController::UpdateBlackboardWeights(const FEQSWeightParameters& Weights)
{
    if (!BlackboardComp) return;

    // EQS 가중치 (각각 개별 키로 저장)
    BlackboardComp->SetValueAsFloat(TEXT("Weight_EnemyObj"),
        Weights.EnemyObjectiveProximity);
    BlackboardComp->SetValueAsFloat(TEXT("Weight_AllyObj"),
        Weights.AllyObjectiveProximity);
    BlackboardComp->SetValueAsFloat(TEXT("Weight_Cover"),
        Weights.CoverDensity);
    BlackboardComp->SetValueAsFloat(TEXT("Weight_Visibility"),
        Weights.EnemyVisibility);
    BlackboardComp->SetValueAsFloat(TEXT("Weight_AllyProx"),
        Weights.AllyProximity);
    BlackboardComp->SetValueAsFloat(TEXT("Weight_Range"),
        Weights.CombatRange);
    BlackboardComp->SetValueAsFloat(TEXT("Weight_Pickup"),
        Weights.PickupProximity);
    BlackboardComp->SetValueAsFloat(TEXT("Weight_Height"),
        Weights.HeightAdvantage);
}

FEnvQueryRequest AMocAIController::CreateDynamicEQSQuery(const FEQSWeightParameters& Weights) const
{
    if (!EQS_TacticalMovement)
    {
        return FEnvQueryRequest();
    }

    UObject* OwnerObject = Cast<UObject>(GetPawn());

    FEnvQueryRequest QueryRequest(EQS_TacticalMovement, OwnerObject);

    // Normalize RL output [-1, 1] to EQS scale [-2, 2]
    // Game AI Pro recommended range for EQS weights
    auto Normalize = [](float RLOutput) {
        return FMath::Clamp(RLOutput * 2.0f, -2.0f, 2.0f);
        };

    QueryRequest.SetFloatParam(TEXT("EnemyObjectiveWeight"),
        Normalize(Weights.EnemyObjectiveProximity));
    QueryRequest.SetFloatParam(TEXT("AllyObjectiveWeight"),
        Normalize(Weights.AllyObjectiveProximity));
    QueryRequest.SetFloatParam(TEXT("CoverDensityWeight"),
        Normalize(Weights.CoverDensity));
    QueryRequest.SetFloatParam(TEXT("EnemyVisibilityWeight"),
        Normalize(Weights.EnemyVisibility));
    QueryRequest.SetFloatParam(TEXT("AllyProximityWeight"),
        Normalize(Weights.AllyProximity));
    QueryRequest.SetFloatParam(TEXT("CombatRangeWeight"),
        Normalize(Weights.CombatRange));
    QueryRequest.SetFloatParam(TEXT("PickupWeight"),
        Normalize(Weights.PickupProximity));
    QueryRequest.SetFloatParam(TEXT("HeightWeight"),
        Normalize(Weights.HeightAdvantage));

    return QueryRequest;
}

void AMocAIController::OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors)
{
    // 적 감지 시 즉시 Blackboard 업데이트
    TArray<AActor*> Enemies;
    AIPerception->GetCurrentlyPerceivedActors(
        UAISense_Sight::StaticClass(), 
        Enemies
    );
    
    if (Enemies.Num() > 0)
    {
        BlackboardComp->SetValueAsObject(TEXT("TargetEnemy"), Enemies[0]);
        BlackboardComp->SetValueAsBool(TEXT("HasTarget"), true);
    }
    else
    {
        BlackboardComp->SetValueAsBool(TEXT("HasTarget"), false);
    }
}

void AMocAIController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
}
