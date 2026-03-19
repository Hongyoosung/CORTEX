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
    // In Inference mode, seed InferencePolicyObject with the Assault policy
    // (default strategy) before the parent wires up the stepper.
    if (AgentMode == EDynamicEQSAgentMode::Inference)
    {
        InferencePolicyObject = GetPolicyForStrategy(CommandedStrategy);
    }

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
}

void UDEScholaAgent::UpdateCommandedStrategy(EDEStrategyType NewStrategy)
{
    if (CommandedStrategy != NewStrategy)
    {
        CommandedStrategy = NewStrategy;

        // In Inference mode, hot-swap to the model trained for this strategy.
        if (AgentMode == EDynamicEQSAgentMode::Inference)
        {
            UObject* NewPolicy = GetPolicyForStrategy(NewStrategy);
            if (NewPolicy)
            {
                SwapInferencePolicy(NewPolicy);
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[DEScholaAgent] No policy assigned for strategy %s — inference unchanged."),
                    *UEnum::GetValueAsString(NewStrategy));
            }
        }

        ADECharacter* OwnerCharacter = Cast<ADECharacter>(GetOwner());
        if (OwnerCharacter)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[DEScholaAgent] Agent %d strategy → %s"),
                OwnerCharacter->AgentID,
                *UEnum::GetValueAsString(NewStrategy));
        }
    }
}

void UDEScholaAgent::ResetAgent()
{
    // Strategy will be reassigned by DESquadManager at episode start.
    // Do not force Assault here — let UpdateCommandedStrategy handle the swap.
    ADECharacter* OwnerCharacter = Cast<ADECharacter>(GetOwner());
    if (OwnerCharacter)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[DEScholaAgent] Agent %d reset"), OwnerCharacter->AgentID);
    }
}

UObject* UDEScholaAgent::GetPolicyForStrategy(EDEStrategyType Strategy) const
{
    switch (Strategy)
    {
    case EDEStrategyType::Assault: return AssaultPolicyObject.Get();
    case EDEStrategyType::Defend:  return DefendPolicyObject.Get();
    case EDEStrategyType::Support: return SupportPolicyObject.Get();
    default:                       return AssaultPolicyObject.Get();
    }
}