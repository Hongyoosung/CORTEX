

#include "AI/AIController/MocAIController.h"
#include "AI/EQS/EQSWeightParameters.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Damage.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"


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
    EQSApplicator = CreateDefaultSubobject<UEQSDynamicWeightApplicator>(TEXT("EQSApplicator"));

    // Removed components (moved to ASquadManager in v10.2):
    // - MCTSPlanner → ASquadManager::TeamMCTSPlanner
    // - WorldModel → ASquadManager::TeamWorldModel
    // - ValueNetwork → Centralized evaluation
    // - EventMonitor → ASquadManager::OnCriticalEvent()
    
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
    AIPerception->OnPerceptionUpdated.AddDynamic(this, &ACortexAIController::OnPerceptionUpdated);
    AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &ACortexAIController::OnTargetPerceptionUpdated);
    
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

    UE_LOG(LogCortex, Log, TEXT("MOC AI Controller initialized (v10.2 executor mode)"));
}

void AMocAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    
    // Blackboard 및 BehaviorTree 시작
    if (BehaviorTree)
    {
        UseBlackboard(BehaviorTree->BlackboardAsset, BlackboardComp);
        RunBehaviorTree(BehaviorTree);
        
        UE_LOG(LogCortex, Log, TEXT("Behavior Tree started"));
    }
    
    // 초기 전략 설정 (Defend)
    CurrentOption = FTacticalOption::CreateDefensive(GetPawn()->GetActorLocation());
    UpdateBlackboard(CurrentOption, TacticalProfiles::DEFENSIVE);
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

        // Generate EQS weights from policy (still used for spatial reasoning)
        if (PolicyExecutor)
        {
            FEQSWeightParameters Weights = PolicyExecutor->InferWeights(CurrentStrategy);
            UpdateBlackboardWeights(Weights);
        }
    }

    // Debug visualization
    if (CVarCortexDebug.GetValueOnGameThread())
    {
        DrawDebugInfo();
    }
}



FEQSWeightParameters AMocAIController::GetCurrentEQSWeights()
{
    // RL Policy에 상태 전달
    FRLObservation PolicyInput = {
        .BaseState = CurrentObservation,
        .CurrentOption = CurrentOption.Strategy,
        .TargetPosition = CurrentOption.TargetLocation,
        .OptionDuration = CurrentOption.PlannedDuration
    };
    
    // ONNX inference
    return PolicyExecutor->PredictEQSWeights(PolicyInput);
}

void AMocAIController::UpdateBlackboard(
    const FTacticalOption& Option,
    const FEQSWeightParameters& Weights
)
{
    if (!BlackboardComp) return;

    // 전략 정보
    BlackboardComp->SetValueAsEnum(TEXT("CurrentStrategy"),
        static_cast<uint8>(Option.Strategy));
    BlackboardComp->SetValueAsVector(TEXT("TargetLocation"),
        Option.TargetLocation);

    // EQS 가중치
    UpdateBlackboardWeights(Weights);
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
