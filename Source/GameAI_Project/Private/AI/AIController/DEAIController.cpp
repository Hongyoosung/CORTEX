

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
    SightConfig->DetectionByAffiliation.bDetectNeutrals = true;   // DECharacter uses custom TeamID, not IGenericTeamAgentInterface → all actors appear Neutral to UE5
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

    // Tick must stay enabled — AAIController::Tick() drives UpdateControlRotation()
    // which rotates the pawn toward the focus point (enemy target).
    PrimaryActorTick.bCanEverTick = true;
}

void ADEAIController::BeginPlay()
{
    Super::BeginPlay();
    UE_LOG(LogTemp, Log, TEXT("[DEAIC] BeginPlay — Controller: %s"), *GetName());
}

void ADEAIController::OnPossess(APawn* InPawn)
{
    UE_LOG(LogTemp, Warning, TEXT("[DEAIC] ===== OnPossess BEGIN ====="));
    UE_LOG(LogTemp, Warning, TEXT("[DEAIC]   Controller : %s"), *GetName());
    UE_LOG(LogTemp, Warning, TEXT("[DEAIC]   Pawn       : %s"), InPawn ? *InPawn->GetName() : TEXT("NULL"));
    UE_LOG(LogTemp, Warning, TEXT("[DEAIC]   BT Asset   : %s"), BehaviorTreeAsset ? *BehaviorTreeAsset->GetName() : TEXT("NULL — assign in BP_AIC!"));

    Super::OnPossess(InPawn);

    // --- Blackboard component check ---
    UBlackboardComponent* BB = GetBlackboardComponent();
    UE_LOG(LogTemp, Warning, TEXT("[DEAIC]   BB (after Super::OnPossess): %s"), BB ? *BB->GetName() : TEXT("NULL"));

    // --- BehaviorTreeComponent check ---
    UBrainComponent* Brain = GetBrainComponent();
    UE_LOG(LogTemp, Warning, TEXT("[DEAIC]   BrainComponent: %s  (class: %s)"),
        Brain ? *Brain->GetName() : TEXT("NULL"),
        Brain ? *Brain->GetClass()->GetName() : TEXT("N/A"));

    if (!BehaviorTreeAsset)
    {
        UE_LOG(LogTemp, Error, TEXT("[DEAIC] ✗ BehaviorTreeAsset is NULL — BT will NOT run! Assign BT_DEAgent in BP_AIC defaults."));
        return;
    }

    // --- BB asset match check ---
    if (BehaviorTreeAsset->BlackboardAsset)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DEAIC]   BT expects BB: %s"), *BehaviorTreeAsset->BlackboardAsset->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[DEAIC]   BT has no BlackboardAsset set — BB keys will be missing!"));
    }

    bool bSuccess = RunBehaviorTree(BehaviorTreeAsset);
    UE_LOG(LogTemp, Warning, TEXT("[DEAIC]   RunBehaviorTree: %s"), bSuccess ? TEXT("✓ SUCCESS") : TEXT("✗ FAILED"));

    if (bSuccess)
    {
        // Verify BT is actually active after start
        UBrainComponent* BrainAfter = GetBrainComponent();
        bool bBTRunning = BrainAfter && BrainAfter->IsRunning();
        UE_LOG(LogTemp, Warning, TEXT("[DEAIC]   BT running after start: %s"), bBTRunning ? TEXT("✓ YES") : TEXT("✗ NO — check BB asset mismatch"));

        UBlackboardComponent* BBAfter = GetBlackboardComponent();
        UE_LOG(LogTemp, Warning, TEXT("[DEAIC]   BB valid after start  : %s"), BBAfter ? TEXT("✓ YES") : TEXT("✗ NO"));
    }

    UE_LOG(LogTemp, Warning, TEXT("[DEAIC] ===== OnPossess END ====="));
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
    if (!BB)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DEAIC] OnPerceptionUpdated — BB is NULL (BT not running?), skipping."));
        return;
    }

    // Filter by EnvID and TeamID for parallel environment isolation
    ADECharacter* Self = Cast<ADECharacter>(GetPawn());
    AActor* BestEnemy = nullptr;

    if (Self)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DEAIC] OnPerceptionUpdated: Self=%s EnvID=%d TeamID=%d — %d actors in sight"),
            *GetName(), Self->GetEnvID_Implementation(), Self->GetTeamID_Implementation(), PerceivedActors.Num());

        for (AActor* Actor : PerceivedActors)
        {
            ADECharacter* Other = Cast<ADECharacter>(Actor);
            UE_LOG(LogTemp, Warning, TEXT("[DEAIC]   Perceived: %s (DECharacter=%s EnvID=%d TeamID=%d)"),
                *Actor->GetName(),
                Other ? TEXT("YES") : TEXT("NO"),
                Other ? Other->GetEnvID_Implementation() : -1,
                Other ? Other->GetTeamID_Implementation() : -1);

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
    }
    else
    {
        BB->SetValueAsBool(TEXT("HasTarget"), false);
    }
}

void ADEAIController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
}
