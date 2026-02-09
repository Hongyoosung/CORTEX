#include "Schola/Components/ScholaMocAgent.h"
#include "Characters/MocCharacter.h"
#include "Common/LogSchola.h"

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
        UE_LOG(LogSchola, Error, TEXT("[ScholaMocAgent] Owner is not AMocCharacter! Agent will not function correctly."));
        return;
    }

    // Log mode for debugging
    const FString ModeStr = (CurrentMode == EAgentMode::Training) ? TEXT("Training (Python RLlib)") : TEXT("Inference (Local ONNX)");
    UE_LOG(LogSchola, Log, TEXT("[ScholaMocAgent] Initialized for Agent %d in %s mode"),
        OwnerCharacter->AgentID, *ModeStr);

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
            UE_LOG(LogSchola, Verbose, TEXT("[ScholaMocAgent] Agent %d received strategy: %s"),
                OwnerCharacter->AgentID,
                *UEnum::GetValueAsString(NewStrategy));
        }

        // Strategy is now available to Observers via GetCommandedStrategy()
        // Observers will include it in the next observation
        // Policy will use it to condition EQS weight output
    }
}