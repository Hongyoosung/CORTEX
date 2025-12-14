// ObjectiveManager.cpp - Manages strategic objectives for team

#include "Team/ObjectiveManager.h"
#include "Team/Objectives/EliminateObjective.h"
#include "Team/Objectives/CaptureObjective.h"
#include "Team/Objectives/DefendObjective.h"
#include "Team/Objectives/SupportAllyObjective.h"
#include "Team/Objectives/FormationMoveObjective.h"
#include "Team/Objectives/RetreatObjective.h"
#include "Team/Objectives/RescueAllyObjective.h"

UObjectiveManager::UObjectiveManager()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f;  // Tick objectives every 0.1s
}

void UObjectiveManager::BeginPlay()
{
    Super::BeginPlay();
}

void UObjectiveManager::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    TickObjectives(DeltaTime);
}

UObjective* UObjectiveManager::CreateObjective(EObjectiveType Type, AActor* Target, const FVector& Location, int32 Priority)
{
    UObjective* NewObjective = nullptr;

    switch (Type)
    {
        case EObjectiveType::Eliminate:
            NewObjective = CreateObjectiveOfType<UEliminateObjective>(Type);
            break;
        case EObjectiveType::CaptureObjective:
            NewObjective = CreateObjectiveOfType<UCaptureObjective>(Type);
            break;
        case EObjectiveType::DefendObjective:
            NewObjective = CreateObjectiveOfType<UDefendObjective>(Type);
            break;
        case EObjectiveType::SupportAlly:
            NewObjective = CreateObjectiveOfType<USupportAllyObjective>(Type);
            break;
        case EObjectiveType::FormationMove:
            NewObjective = CreateObjectiveOfType<UFormationMoveObjective>(Type);
            break;
        case EObjectiveType::Retreat:
            NewObjective = CreateObjectiveOfType<URetreatObjective>(Type);
            break;
        case EObjectiveType::RescueAlly:
            NewObjective = CreateObjectiveOfType<URescueAllyObjective>(Type);
            break;
        case EObjectiveType::None:
            // None type is valid but creates no objective
            return nullptr;
        default:
            UE_LOG(LogTemp, Warning, TEXT("ObjectiveManager: Unsupported objective type"));
            return nullptr;
    }

    if (NewObjective)
    {
        NewObjective->TargetActor = Target;
        NewObjective->TargetLocation = Location;
        NewObjective->Priority = Priority;
        Objectives.Add(NewObjective);
    }

    return NewObjective;
}

UObjective* UObjectiveManager::CreateEliminateObjective(AActor* Target, int32 Priority)
{
    return CreateObjective(EObjectiveType::Eliminate, Target, FVector::ZeroVector, Priority);
}

UObjective* UObjectiveManager::CreateCaptureObjective(const FVector& Location, int32 Priority)
{
    return CreateObjective(EObjectiveType::CaptureObjective, nullptr, Location, Priority);
}

UObjective* UObjectiveManager::CreateDefendObjective(const FVector& Location, int32 Priority)
{
    return CreateObjective(EObjectiveType::DefendObjective, nullptr, Location, Priority);
}

UObjective* UObjectiveManager::CreateSupportObjective(AActor* AllyTarget, int32 Priority)
{
    return CreateObjective(EObjectiveType::SupportAlly, AllyTarget, FVector::ZeroVector, Priority);
}

UObjective* UObjectiveManager::CreateRescueObjective(AActor* WoundedAlly, int32 Priority)
{
    return CreateObjective(EObjectiveType::RescueAlly, WoundedAlly, FVector::ZeroVector, Priority);
}

void UObjectiveManager::AssignAgentsToObjective(UObjective* Objective, const TArray<AActor*>& Agents)
{
    if (!IsValid(Objective))
    {
        return;
    }

    // Remove agents from previous objectives
    for (AActor* Agent : Agents)
    {
        UnassignAgentFromObjective(Agent);
    }

    // Assign to new objective
    Objective->AssignedAgents = Agents;

    // Update mapping
    for (AActor* Agent : Agents)
    {
        if (IsValid(Agent))
        {
            AgentObjectiveMap.Add(Agent, Objective);
        }
    }
}

void UObjectiveManager::UnassignAgentFromObjective(AActor* Agent)
{
    if (!IsValid(Agent))
    {
        return;
    }

    // Find agent's current objective
    if (TObjectPtr<UObjective>* ObjectivePtr = AgentObjectiveMap.Find(Agent))
    {
        UObjective* CurrentObjective = ObjectivePtr->Get();
        if (IsValid(CurrentObjective))
        {
            CurrentObjective->AssignedAgents.Remove(Agent);
        }
    }

    AgentObjectiveMap.Remove(Agent);
}

void UObjectiveManager::ClearAllAssignments()
{
    for (const TObjectPtr<UObjective>& ObjectivePtr : Objectives)
    {
        UObjective* Objective = ObjectivePtr.Get();
        if (Objective != nullptr && IsValid(Objective))
        {
            Objective->AssignedAgents.Empty();
        }
    }
    AgentObjectiveMap.Empty();
}

void UObjectiveManager::ActivateObjective(UObjective* Objective)
{
    if (IsValid(Objective))
    {
        Objective->Activate();
    }
}

void UObjectiveManager::DeactivateObjective(UObjective* Objective)
{
    if (IsValid(Objective))
    {
        Objective->Deactivate();
    }
}

void UObjectiveManager::CancelObjective(UObjective* Objective)
{
    if (IsValid(Objective))
    {
        Objective->Cancel();

        // Unassign all agents
        for (AActor* Agent : Objective->AssignedAgents)
        {
            AgentObjectiveMap.Remove(Agent);
        }
        Objective->AssignedAgents.Empty();
    }
}

void UObjectiveManager::RemoveObjective(UObjective* Objective)
{
    if (IsValid(Objective))
    {
        // Unassign agents
        CancelObjective(Objective);

        // Remove from list
        Objectives.Remove(Objective);
    }
}

void UObjectiveManager::ClearCompletedObjectives()
{
    TArray<UObjective*> ToRemove;

    for (const TObjectPtr<UObjective>& ObjectivePtr : Objectives)
    {
        UObjective* Objective = ObjectivePtr.Get();
        if (Objective != nullptr && IsValid(Objective) && (Objective->IsCompleted() || Objective->IsFailed()))
        {
            ToRemove.Add(Objective);
        }
    }

    for (UObjective* Objective : ToRemove)
    {
        if (Objective != nullptr)
        {
            RemoveObjective(Objective);
        }
    }
}

TArray<UObjective*> UObjectiveManager::GetActiveObjectives() const
{
    TArray<UObjective*> ActiveObjectives;

    for (const TObjectPtr<UObjective>& ObjectivePtr : Objectives)
    {
        UObjective* Objective = ObjectivePtr.Get();
        if (Objective != nullptr && IsValid(Objective) && Objective->IsActive())
        {
            ActiveObjectives.Add(Objective);
        }
    }

    return ActiveObjectives;
}

TArray<UObjective*> UObjectiveManager::GetAllObjectives() const
{
    return Objectives;
}

UObjective* UObjectiveManager::GetAgentObjective(AActor* Agent) const
{
    if (const TObjectPtr<UObjective>* ObjectivePtr = AgentObjectiveMap.Find(Agent))
    {
        return *ObjectivePtr;
    }
    return nullptr;
}

TArray<AActor*> UObjectiveManager::GetObjectiveAgents(UObjective* Objective) const
{
    if (IsValid(Objective))
    {
        return Objective->AssignedAgents;
    }
    return TArray<AActor*>();
}

int32 UObjectiveManager::GetActiveObjectiveCount() const
{
    return GetActiveObjectives().Num();
}

UObjective* UObjectiveManager::GetHighestPriorityObjective() const
{
    UObjective* Highest = nullptr;
    int32 HighestPriority = -1;

    for (UObjective* Objective : GetActiveObjectives())
    {
        if (IsValid(Objective) && Objective->Priority > HighestPriority)
        {
            Highest = Objective;
            HighestPriority = Objective->Priority;
        }
    }

    return Highest;
}

float UObjectiveManager::CalculateTotalTeamReward() const
{
    float TotalReward = 0.0f;

    for (const TObjectPtr<UObjective>& ObjectivePtr : Objectives)
    {
        UObjective* Objective = ObjectivePtr.Get();
        if (Objective != nullptr && IsValid(Objective))
        {
            TotalReward += Objective->CalculateStrategicReward();
        }
    }

    return TotalReward;
}

template<typename T>
T* UObjectiveManager::CreateObjectiveOfType(EObjectiveType Type)
{
    T* NewObjective = NewObject<T>(this);
    if (NewObjective)
    {
        NewObjective->Type = Type;
    }
    return NewObjective;
}

void UObjectiveManager::TickObjectives(float DeltaTime)
{
    // CRITICAL: Copy array before iteration to avoid "Array has changed during ranged-for iteration" error
    // Objectives can modify the array during Tick() (e.g., spawning sub-objectives, removing themselves)
    TArray<TObjectPtr<UObjective>> ObjectivesCopy = Objectives;

    for (const TObjectPtr<UObjective>& ObjectivePtr : ObjectivesCopy)
    {
        UObjective* Objective = ObjectivePtr.Get();
        if (Objective != nullptr && IsValid(Objective) && Objective->IsActive())
        {
            Objective->Tick(DeltaTime);
        }
    }

    // Periodically clean up completed objectives
    static float CleanupTimer = 0.0f;
    CleanupTimer += DeltaTime;
    if (CleanupTimer >= 5.0f)
    {
        CleanupObjectives();
        CleanupTimer = 0.0f;
    }
}

void UObjectiveManager::CleanupObjectives()
{
    // Remove invalid, completed, or failed objectives
    TArray<UObjective*> ToRemove;

    // CRITICAL: Same nullptr safety as TickObjectives
    for (const TObjectPtr<UObjective>& ObjectivePtr : Objectives)
    {
        UObjective* Objective = ObjectivePtr.Get();
        if (Objective == nullptr || !IsValid(Objective) || !Objective->IsActive())
        {
            // Remove invalid or inactive objectives
            ToRemove.Add(Objective);
        }
    }

    for (UObjective* Objective : ToRemove)
    {
        if (Objective != nullptr && IsValid(Objective))
        {
            RemoveObjective(Objective);
        }
        else
        {
            // Just remove from array if already invalid
            Objectives.Remove(Objective);
        }
    }
}
