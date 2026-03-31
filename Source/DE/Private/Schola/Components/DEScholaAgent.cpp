#include "Schola/Components/DEScholaAgent.h"
#include "Characters/DEAgent.h"


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
    // In Inference mode, seed InferencePolicyObject with the Strike policy
    // (default class) before the parent wires up the stepper.
    if (AgentMode == EDynamicEQSAgentMode::Inference)
    {
        InferencePolicyObject = GetPolicyForClass(CommandedClass);
    }

    Super::BeginPlay();

    // Verify owner is DEAgent
    ADEAgent* OwnerCharacter = Cast<ADEAgent>(GetOwner());
    if (!OwnerCharacter)
    {
        UE_LOG(LogTemp, Error, TEXT("[DEScholaAgent] Owner is not ADEAgent! Agent will not function correctly."));
        return;
    }

    // Log mode for debugging
    const FString ModeStr = (AgentMode == EDynamicEQSAgentMode::Training) ? TEXT("Training (Python RLlib)") : TEXT("Inference (Local ONNX)");
    UE_LOG(LogTemp, Log, TEXT("[DEScholaAgent] Initialized for Agent %d in %s mode"),
        OwnerCharacter->AgentID, *ModeStr);
}

void UDEScholaAgent::UpdateCommandedClass(EDEClassType NewClass)
{
    if (CommandedClass != NewClass)
    {
        CommandedClass = NewClass;

        // In Inference mode, hot-swap to the model trained for this class.
        if (AgentMode == EDynamicEQSAgentMode::Inference)
        {
            UObject* NewPolicy = GetPolicyForClass(NewClass);
            if (NewPolicy)
            {
                SwapInferencePolicy(NewPolicy);
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[DEScholaAgent] No policy assigned for class %s — inference unchanged."),
                    *UEnum::GetValueAsString(NewClass));
            }
        }

        ADEAgent* OwnerCharacter = Cast<ADEAgent>(GetOwner());
        if (OwnerCharacter)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[DEScholaAgent] Agent %d class → %s"),
                OwnerCharacter->AgentID,
                *UEnum::GetValueAsString(NewClass));
        }
    }
}

void UDEScholaAgent::ResetAgent()
{
    // Class will be reassigned by DESquadManager at episode start.
    // Do not force Strike here — let UpdateCommandedClass handle the swap.
    ADEAgent* OwnerCharacter = Cast<ADEAgent>(GetOwner());
    if (OwnerCharacter)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[DEScholaAgent] Agent %d reset"), OwnerCharacter->AgentID);
    }
}

UObject* UDEScholaAgent::GetPolicyForClass(EDEClassType Class) const
{
    switch (Class)
    {
    case EDEClassType::Strike: return StrikePolicyObject.Get();
    case EDEClassType::Vanguard:  return VanguardPolicyObject.Get();
    case EDEClassType::Support: return SupportPolicyObject.Get();
    default:                       return StrikePolicyObject.Get();
    }
}