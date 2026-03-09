

#include "AI/AIController/DEAIController.h"
#include "Characters/DECharacter.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Damage.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"


ADEAIController::ADEAIController(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Components 생성
    AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));
    SetPerceptionComponent(*AIPerception);

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
    AIPerception->OnPerceptionUpdated.AddDynamic(this, &ADEAIController::OnPerceptionUpdated);
    AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &ADEAIController::OnTargetPerceptionUpdated);

    // Tick 비활성화 — EQS weight inference는 ScholaDEAgent가 담당
    PrimaryActorTick.bCanEverTick = false;
}

void ADEAIController::BeginPlay()
{
    Super::BeginPlay();
    UE_LOG(LogTemp, Log, TEXT("[DEAIC] BeginPlay — Controller: %s"), *GetName());
}

void ADEAIController::OnPossess(APawn* InPawn)
{
    UE_LOG(LogTemp, Log, TEXT("[DEAIC] OnPossess BEGIN — Pawn: %s"), InPawn ? *InPawn->GetName() : TEXT("NULL"));

    Super::OnPossess(InPawn);

    UE_LOG(LogTemp, Log, TEXT("[DEAIC] OnPossess AFTER Super — BehaviorTreeAsset: %s"),
        BehaviorTreeAsset ? *BehaviorTreeAsset->GetName() : TEXT("NULL (not assigned!)"));

    if (BehaviorTreeAsset)
    {
        bool bSuccess = RunBehaviorTree(BehaviorTreeAsset);
        UE_LOG(LogTemp, Log, TEXT("[DEAIC] RunBehaviorTree result: %s"), bSuccess ? TEXT("SUCCESS") : TEXT("FAILED"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[DEAIC] BehaviorTreeAsset is NULL — assign it in BP_DEAIC!"));
    }
}

void ADEAIController::OnUnPossess()
{
    UE_LOG(LogTemp, Warning, TEXT("[DEAIC] OnUnPossess called on %s — BT will stop!"), *GetName());
    Super::OnUnPossess();
}

void ADEAIController::OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors)
{
    // 적 감지 시 즉시 Blackboard 업데이트
    TArray<AActor*> PerceivedActors;
    AIPerception->GetCurrentlyPerceivedActors(
        UAISense_Sight::StaticClass(),
        PerceivedActors
    );

    UBlackboardComponent* BB = GetBlackboardComponent();

    // Filter by EnvID and TeamID for parallel environment isolation
    ADECharacter* Self = Cast<ADECharacter>(GetPawn());
    AActor* BestEnemy = nullptr;

    if (Self)
    {
        for (AActor* Actor : PerceivedActors)
        {
            ADECharacter* Other = Cast<ADECharacter>(Actor);
            if (Other && Other->GetEnvID_Implementation() == Self->GetEnvID_Implementation() && Other->GetTeamID_Implementation() != Self->GetTeamID_Implementation())
            {
                BestEnemy = Other;
                break;
            }
        }
    }

    if (BestEnemy)
    {
        BB->SetValueAsObject(TEXT("TargetEnemy"), BestEnemy);
        BB->SetValueAsBool(TEXT("HasTarget"), true);
        SetFocus(BestEnemy);
    }
    else
    {
        BB->SetValueAsBool(TEXT("HasTarget"), false);
        ClearFocus(EAIFocusPriority::Gameplay);
    }
}

void ADEAIController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
}
