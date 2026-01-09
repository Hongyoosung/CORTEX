// MissionManager.cpp - Manages strategic missions for team
// v7.0: Renamed from ObjectiveManager to clarify distinction from ObjectiveActor

#include "Team/MissionManager.h"
#include "Team/ObjectiveActor.h"
#include "EngineUtils.h"

UMissionManager::UMissionManager()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f;  // Tick missions every 0.1s
}

void UMissionManager::BeginPlay()
{
    Super::BeginPlay();
}

void UMissionManager::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    TickMissions(DeltaTime);
}

UMission* UMissionManager::CreateMission(EMissionType Type, AActor* Target, const FVector& Location, int32 Priority)
{
    // v7.0: Use base UMission class for all types
    if (Type == EMissionType::None)
    {
        return nullptr; // None type creates no mission
    }

    // Create base mission
    UMission* NewMission = NewObject<UMission>(this);
    if (NewMission)
    {
        NewMission->Type = Type;
        NewMission->TargetActor = Target;
        NewMission->TargetLocation = Location;
        NewMission->Priority = Priority;

        // Set default priorities by type
        switch (Type)
        {
            case EMissionType::Assault:
                NewMission->Priority = (Priority > 0) ? Priority : 8;
                break;
            case EMissionType::Defend:
                NewMission->Priority = (Priority > 0) ? Priority : 7;
                break;
            case EMissionType::Support:
                NewMission->Priority = (Priority > 0) ? Priority : 6;
                break;
            case EMissionType::Retreat:
                NewMission->Priority = (Priority > 0) ? Priority : 6;
                break;
            default:
                NewMission->Priority = 5;
                break;
        }

        Missions.Add(NewMission);
    }

    return NewMission;
}

// Helper methods for common mission types
UMission* UMissionManager::CreateEliminateMission(AActor* Target, int32 Priority)
{
    return CreateMission(EMissionType::Assault, Target, FVector::ZeroVector, Priority);
}

UMission* UMissionManager::CreateCaptureMission(const FVector& Location, int32 Priority)
{
    return CreateMission(EMissionType::Assault, nullptr, Location, Priority);
}

UMission* UMissionManager::CreateDefendMission(const FVector& Location, int32 Priority)
{
    return CreateMission(EMissionType::Defend, nullptr, Location, Priority);
}

UMission* UMissionManager::CreateSupportMission(AActor* AllyTarget, int32 Priority)
{
    return CreateMission(EMissionType::Support, AllyTarget, FVector::ZeroVector, Priority);
}

UMission* UMissionManager::CreateRescueMission(AActor* WoundedAlly, int32 Priority)
{
    return CreateMission(EMissionType::Support, WoundedAlly, FVector::ZeroVector, Priority);
}

void UMissionManager::AssignAgentsToMission(UMission* Mission, const TArray<AActor*>& Agents)
{
    if (!IsValid(Mission))
    {
        return;
    }

    // Remove agents from previous missions
    for (AActor* Agent : Agents)
    {
        UnassignAgentFromMission(Agent);
    }

    // Assign to new mission
    Mission->AssignedAgents = Agents;

    // Update mapping
    for (AActor* Agent : Agents)
    {
        if (IsValid(Agent))
        {
            AgentMissionMap.Add(Agent, Mission);
        }
    }
}

void UMissionManager::UnassignAgentFromMission(AActor* Agent)
{
    if (!IsValid(Agent))
    {
        return;
    }

    // Find agent's current mission
    if (TObjectPtr<UMission>* MissionPtr = AgentMissionMap.Find(Agent))
    {
        UMission* CurrentMission = MissionPtr->Get();
        if (IsValid(CurrentMission))
        {
            CurrentMission->AssignedAgents.Remove(Agent);
        }
    }

    AgentMissionMap.Remove(Agent);
}

void UMissionManager::ClearAllAssignments()
{
    for (const TObjectPtr<UMission>& MissionPtr : Missions)
    {
        UMission* Mission = MissionPtr.Get();
        if (Mission != nullptr && IsValid(Mission))
        {
            Mission->AssignedAgents.Empty();
        }
    }
    AgentMissionMap.Empty();
}

void UMissionManager::ActivateMission(UMission* Mission)
{
    if (IsValid(Mission))
    {
        Mission->Activate();
    }
}

void UMissionManager::DeactivateMission(UMission* Mission)
{
    if (IsValid(Mission))
    {
        Mission->Deactivate();
    }
}

void UMissionManager::CancelMission(UMission* Mission)
{
    if (IsValid(Mission))
    {
        Mission->Cancel();

        // Unassign all agents
        for (AActor* Agent : Mission->AssignedAgents)
        {
            AgentMissionMap.Remove(Agent);
        }
        Mission->AssignedAgents.Empty();
    }
}

void UMissionManager::RemoveMission(UMission* Mission)
{
    if (IsValid(Mission))
    {
        // Unassign agents
        CancelMission(Mission);

        // Remove from list
        Missions.Remove(Mission);
    }
}

void UMissionManager::ClearCompletedMissions()
{
    TArray<UMission*> ToRemove;

    for (const TObjectPtr<UMission>& MissionPtr : Missions)
    {
        UMission* Mission = MissionPtr.Get();
        if (Mission != nullptr && IsValid(Mission) && (Mission->IsCompleted() || Mission->IsFailed()))
        {
            ToRemove.Add(Mission);
        }
    }

    for (UMission* Mission : ToRemove)
    {
        if (Mission != nullptr)
        {
            RemoveMission(Mission);
        }
    }
}

TArray<UMission*> UMissionManager::GetActiveMissions() const
{
    TArray<UMission*> ActiveMissions;

    for (const TObjectPtr<UMission>& MissionPtr : Missions)
    {
        UMission* Mission = MissionPtr.Get();
        if (Mission != nullptr && IsValid(Mission) && Mission->IsActive())
        {
            ActiveMissions.Add(Mission);
        }
    }

    return ActiveMissions;
}

TArray<UMission*> UMissionManager::GetAllMissions() const
{
    TArray<UMission*> Result;
    for (const TObjectPtr<UMission>& MissionPtr : Missions)
    {
        if (UMission* Mission = MissionPtr.Get())
        {
            Result.Add(Mission);
        }
    }
    return Result;
}

UMission* UMissionManager::GetAgentMission(AActor* Agent) const
{
    if (const TObjectPtr<UMission>* MissionPtr = AgentMissionMap.Find(Agent))
    {
        return *MissionPtr;
    }
    return nullptr;
}

TArray<AActor*> UMissionManager::GetMissionAgents(UMission* Mission) const
{
    if (IsValid(Mission))
    {
        return Mission->AssignedAgents;
    }
    return TArray<AActor*>();
}

int32 UMissionManager::GetActiveMissionCount() const
{
    return GetActiveMissions().Num();
}

UMission* UMissionManager::GetHighestPriorityMission() const
{
    UMission* Highest = nullptr;
    int32 HighestPriority = -1;

    for (UMission* Mission : GetActiveMissions())
    {
        if (IsValid(Mission) && Mission->Priority > HighestPriority)
        {
            Highest = Mission;
            HighestPriority = Mission->Priority;
        }
    }

    return Highest;
}

float UMissionManager::CalculateTotalTeamReward() const
{
    float TotalReward = 0.0f;

    for (const TObjectPtr<UMission>& MissionPtr : Missions)
    {
        UMission* Mission = MissionPtr.Get();
        if (Mission != nullptr && IsValid(Mission))
        {
            TotalReward += Mission->CalculateStrategicReward();
        }
    }

    return TotalReward;
}

void UMissionManager::TickMissions(float DeltaTime)
{
    // CRITICAL: Copy array before iteration to avoid "Array has changed during ranged-for iteration" error
    // Missions can modify the array during Tick() (e.g., spawning sub-missions, removing themselves)
    TArray<TObjectPtr<UMission>> MissionsCopy = Missions;

    for (const TObjectPtr<UMission>& MissionPtr : MissionsCopy)
    {
        UMission* Mission = MissionPtr.Get();
        if (Mission != nullptr && IsValid(Mission) && Mission->IsActive())
        {
            Mission->Tick(DeltaTime);
        }
    }

    // Periodically clean up completed missions
    static float CleanupTimer = 0.0f;
    CleanupTimer += DeltaTime;
    if (CleanupTimer >= 5.0f)
    {
        CleanupMissions();
        CleanupTimer = 0.0f;
    }
}

void UMissionManager::CleanupMissions()
{
    // Remove invalid, completed, failed, or cancelled missions
    // CRITICAL: Do NOT remove Inactive missions (they might be newly created, awaiting activation)
    TArray<UMission*> ToRemove;

    // CRITICAL: Same nullptr safety as TickMissions
    for (const TObjectPtr<UMission>& MissionPtr : Missions)
    {
        UMission* Mission = MissionPtr.Get();
        if (Mission == nullptr || !IsValid(Mission))
        {
            // Remove invalid missions
            ToRemove.Add(Mission);
        }
        else if (Mission->IsCompleted() || Mission->IsFailed() || Mission->Status == EMissionStatus::Cancelled)
        {
            // Remove finished missions only (not Inactive)
            ToRemove.Add(Mission);
        }
    }

    for (UMission* Mission : ToRemove)
    {
        if (Mission != nullptr && IsValid(Mission))
        {
            RemoveMission(Mission);
        }
        else
        {
            // Just remove from array if already invalid
            Missions.Remove(Mission);
        }
    }
}

//========================================
// v7.0: ObjectiveActor Integration
// Note: These methods find physical objectives (bases), not missions
//========================================

TArray<AObjectiveActor*> UMissionManager::FindAllObjectiveActors() const
{
    TArray<AObjectiveActor*> FoundActors;

    if (!GetWorld())
    {
        return FoundActors;
    }

    // Iterate all actors of type AObjectiveActor in the world
    for (TActorIterator<AObjectiveActor> It(GetWorld()); It; ++It)
    {
        AObjectiveActor* ObjectiveActor = *It;
        if (ObjectiveActor && IsValid(ObjectiveActor))
        {
            FoundActors.Add(ObjectiveActor);
        }
    }

    return FoundActors;
}

AObjectiveActor* UMissionManager::FindFriendlyObjective(int32 TeamID) const
{
    TArray<AObjectiveActor*> AllObjectives = FindAllObjectiveActors();

    for (AObjectiveActor* Objective : AllObjectives)
    {
        if (Objective && Objective->IsFriendlyTo(TeamID))
        {
            return Objective;
        }
    }

    return nullptr;
}

AObjectiveActor* UMissionManager::FindHostileObjective(int32 TeamID) const
{
    TArray<AObjectiveActor*> AllObjectives = FindAllObjectiveActors();

    for (AObjectiveActor* Objective : AllObjectives)
    {
        if (Objective && Objective->IsHostileTo(TeamID))
        {
            return Objective;
        }
    }

    return nullptr;
}
