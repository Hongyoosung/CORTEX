#include "Schola/Components/DEScholaAgent.h"
#include "Characters/DECharacter.h"


UDEScholaAgent::UDEScholaAgent()
    : Super()
{
    // Default configuration
    // Tick is required for Inference mode (Stepper->Step each frame).
    // bStartWithTickEnabled is false; BeginPlay enables it only in Inference mode.
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
    const FString ModeStr = (AgentMode == EDynamicEQSAgentMode::Training) ? TEXT("Training (Python RLlib)") : TEXT("Inference (Local ONNX)");
    UE_LOG(LogTemp, Log, TEXT("[DEScholaAgent] Initialized for Agent %d in %s mode"),
        OwnerCharacter->AgentID, *ModeStr);


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