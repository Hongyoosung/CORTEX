#include "Schola/Components/ScholaMocAgent.h"
#include "Characters/MocCharacter.h"


UScholaMocAgent::UScholaMocAgent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Default configuration
    PrimaryComponentTick.bCanEverTick = false; // Schola handles Think/Act ticking
    bWantsInitializeComponent = true;
}

void UScholaMocAgent::BeginPlay()
{
    Super::BeginPlay();

    // Verify owner is MocCharacter
    AMocCharacter* OwnerCharacter = Cast<AMocCharacter>(GetOwner());
    if (!OwnerCharacter)
    {
        UE_LOG(LogTemp, Error, TEXT("[ScholaMocAgent] Owner is not AMocCharacter! Agent will not function correctly."));
        return;
    }

    // Log mode for debugging
    const FString ModeStr = (CurrentMode == EAgentMode::Training) ? TEXT("Training (Python RLlib)") : TEXT("Inference (Local ONNX)");
    UE_LOG(LogTemp, Log, TEXT("[ScholaMocAgent] Initialized for Agent %d in %s mode"),
        OwnerCharacter->AgentID, *ModeStr);

    // Log training override status (Phase 1)
    if (bUseTrainingStrategyOverride)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ScholaMocAgent] Agent %d: TRAINING OVERRIDE ENABLED - Strategy locked to %s"),
            OwnerCharacter->AgentID,
            *UEnum::GetValueAsString(TrainingStrategyOverride));
        UE_LOG(LogTemp, Warning, TEXT("[ScholaMocAgent] SquadManager commands will be IGNORED. Disable this for Phase 3!"));
    }

    // Schola's Initialize() is called by parent BeginPlay()
    // This sets up Observers, Policy, Brain, Actuators
    // No additional setup needed - proper separation of concerns
}

void UScholaMocAgent::UpdateCommandedStrategy(EStrategyType NewStrategy)
{
    if (CommandedStrategy != NewStrategy)
    {
        CommandedStrategy = NewStrategy;

        // Log strategy change for debugging
        AMocCharacter* OwnerCharacter = Cast<AMocCharacter>(GetOwner());
        if (OwnerCharacter)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[ScholaMocAgent] Agent %d received strategy: %s"),
                OwnerCharacter->AgentID,
                *UEnum::GetValueAsString(NewStrategy));
        }

        // Strategy is now available to Observers via GetCommandedStrategy()
        // Observers will include it in the next observation
        // Policy will use it to condition EQS weight output
    }
}

void UScholaMocAgent::ResetAgent()
{
    // Reset commanded strategy to default
    CommandedStrategy = EStrategyType::Assault;

    // Note: Schola's internal state (observation buffers, action history, etc.)
    // is managed by the parent UInferenceComponent and ScholaEnvironment.
    // We only need to reset MOC-specific state here.

    AMocCharacter* OwnerCharacter = Cast<AMocCharacter>(GetOwner());
    if (OwnerCharacter)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[ScholaMocAgent] Agent %d reset - strategy set to Assault"),
            OwnerCharacter->AgentID);
    }
}