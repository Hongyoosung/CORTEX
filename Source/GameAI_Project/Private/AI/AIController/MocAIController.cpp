// File: AI/CortexAIController.cpp

#include "CortexAIController.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Damage.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"

ACortexAIController::ACortexAIController(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Components 생성
    AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));
    SetPerceptionComponent(*AIPerception);
    
    BehaviorTreeComp = CreateDefaultSubobject<UBehaviorTreeComponent>(TEXT("BehaviorTree"));
    BlackboardComp = CreateDefaultSubobject<UBlackboardComponent>(TEXT("Blackboard"));
    
    // CORTEX Components
    PolicyExecutor = CreateDefaultSubobject<UCortexPolicyExecutor>(TEXT("PolicyExecutor"));
    MCTSPlanner = CreateDefaultSubobject<UCortexMCTSPlanner>(TEXT("MCTSPlanner"));
    WorldModel = CreateDefaultSubobject<UCortexWorldModel>(TEXT("WorldModel"));
    ValueNetwork = CreateDefaultSubobject<UCortexValueNetwork>(TEXT("ValueNetwork"));
    EQSApplicator = CreateDefaultSubobject<UEQSDynamicWeightApplicator>(TEXT("EQSApplicator"));
    EventMonitor = CreateDefaultSubobject<UCortexEventMonitor>(TEXT("EventMonitor"));
    
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

void ACortexAIController::BeginPlay()
{
    Super::BeginPlay();
    
    // ONNX 모델 로드
    PolicyExecutor->LoadModel(TEXT("Content/AI/Models/policy_weights.onnx"));
    WorldModel->LoadModel(TEXT("Content/AI/Models/world_model.onnx"));
    ValueNetwork->LoadModel(TEXT("Content/AI/Models/value_net.onnx"));
    
    UE_LOG(LogCortex, Log, TEXT("CORTEX AI Controller initialized"));
}

void ACortexAIController::OnPossess(APawn* InPawn)
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

void ACortexAIController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    TimeSinceLastReplan += DeltaTime;
    
    // 1. 상태 관측
    FObservation NewObservation = GatherObservation();
    
    // 2. Replanning 필요성 체크
    if (ShouldReplan(NewObservation))
    {
        UE_LOG(LogCortex, Warning, TEXT("Replanning triggered"));
        
        // MCTS로 새 전략 선택 (15ms budget)
        CurrentOption = PlanNewStrategy();
        TimeSinceLastReplan = 0.0f;
        
        // Blackboard 업데이트 → BT가 자동 반응
        FEQSWeightParameters NewWeights = GetCurrentEQSWeights();
        UpdateBlackboard(CurrentOption, NewWeights);
    }
    
    CurrentObservation = NewObservation;
    
    // 디버그 시각화
    if (CVarCortexDebug.GetValueOnGameThread())
    {
        DrawDebugInfo();
    }
}

FObservation ACortexAIController::GatherObservation()
{
    FObservation Obs;
    
    APawn* MyPawn = GetPawn();
    if (!MyPawn) return Obs;
    
    // 기본 정보
    Obs.SelfPosition = MyPawn->GetActorLocation();
    Obs.SelfRotation = MyPawn->GetActorRotation();
    Obs.SelfHealth = Cast<ACortexCharacter>(MyPawn)->GetHealth();
    
    // Perception 기반 적 정보
    TArray<AActor*> PerceivedEnemies;
    AIPerception->GetCurrentlyPerceivedActors(
        UAISense_Sight::StaticClass(), 
        PerceivedEnemies
    );
    
    for (int32 i = 0; i < FMath::Min(PerceivedEnemies.Num(), 3); ++i)
    {
        Obs.EnemyPositions[i] = PerceivedEnemies[i]->GetActorLocation();
        Obs.EnemyHealths[i] = Cast<ACortexCharacter>(PerceivedEnemies[i])->GetHealth();
    }
    Obs.NumVisibleEnemies = PerceivedEnemies.Num();
    
    // 거점 정보 (GameMode에서 가져오기)
    // ... (기존 문서와 동일)
    
    return Obs;
}

bool ACortexAIController::ShouldReplan(const FObservation& NewObservation)
{
    // EventMonitor에게 위임
    return EventMonitor->CheckReplanningTriggers(
        CurrentObservation, 
        NewObservation, 
        TimeSinceLastReplan
    );
}

FTacticalOption ACortexAIController::PlanNewStrategy()
{
    // MCTS 실행 (15ms budget)
    return MCTSPlanner->FindBestOption(
        CurrentObservation,
        WorldModel,
        ValueNetwork,
        15.0f  // milliseconds
    );
}

FEQSWeightParameters ACortexAIController::GetCurrentEQSWeights()
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

void ACortexAIController::UpdateBlackboard(
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

void ACortexAIController::OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors)
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
