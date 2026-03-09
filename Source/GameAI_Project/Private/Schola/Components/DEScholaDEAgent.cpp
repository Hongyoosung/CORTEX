#include "Schola/Components/DEScholaAgent.h"
#include "Characters/DECharacter.h"


UDEScholaAgent::UDEScholaAgent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Default configuration
    PrimaryComponentTick.bCanEverTick = false; // Schola handles Think/Act ticking
    bWantsInitializeComponent = true;
}

void UDEScholaAgent::BeginPlay()
{
    Super::BeginPlay();

    // Verify owner is DECharacter
    ADECharacter* OwnerCharacter = Cast<ADECharacter>(GetOwner());
    if (!OwnerCharacter)
    {
        UE_LOG(LogTemp, Error, TEXT("[DEScholaAgent] Owner is not ADECharacter! Agent will not function correctly."));
        return;
    }

    // Log mode for debugging
    const FString ModeStr = (CurrentMode == EDEAgentMode::Training) ? TEXT("Training (Python RLlib)") : TEXT("Inference (Local ONNX)");
    UE_LOG(LogTemp, Log, TEXT("[DEScholaAgent] Initialized for Agent %d in %s mode"),
        OwnerCharacter->AgentID, *ModeStr);

    // Log training override status (Phase 1)
    if (bUseTrainingStrategyOverride)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DEScholaAgent] Agent %d: TRAINING OVERRIDE ENABLED - Strategy locked to %s"),
            OwnerCharacter->AgentID,
            *UEnum::GetValueAsString(TrainingStrategyOverride));
        UE_LOG(LogTemp, Warning, TEXT("[DEScholaAgent] DESquadManager commands will be IGNORED. Disable this for Phase 3!"));
    }

    // Schola's Initialize() is called by parent BeginPlay()
    // This sets up Observers, Policy, Brain, Actuators
    // No additional setup needed - proper separation of concerns
}

void UDEScholaAgent::UpdateCommandedStrategy(EDEStrategyType NewStrategy)
{
    if (CommandedStrategy != NewStrategy)
    {
        CommandedStrategy = NewStrategy;

        // Log strategy change for debugging
        ADECharacter* OwnerCharacter = Cast<ADECharacter>(GetOwner());
        if (OwnerCharacter)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[DEScholaAgent] Agent %d received strategy: %s"),
                OwnerCharacter->AgentID,
                *UEnum::GetValueAsString(NewStrategy));
        }

        // Strategy is now available to Observers via GetCommandedStrategy()
        // Observers will include it in the next observation
        // Policy will use it to condition EQS weight output
    }
}

void UDEScholaAgent::ResetAgent()
{
    // Reset commanded strategy to default
    CommandedStrategy = EDEStrategyType::Assault;

    // Note: Schola's internal state (observation buffers, action history, etc.)
    // is managed by the parent UInferenceComponent and DEScholaEnvironment.
    // We only need to reset specific state here.

    ADECharacter* OwnerCharacter = Cast<ADECharacter>(GetOwner());
    if (OwnerCharacter)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[DEScholaAgent] Agent %d reset - strategy set to Assault"),
            OwnerCharacter->AgentID);
    }
}