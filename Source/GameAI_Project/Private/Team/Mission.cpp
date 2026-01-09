// Mission.cpp - Base class for strategic missions
// v7.0: Renamed from Objective.cpp to distinguish from AObjectiveActor

#include "Team/Mission.h"
#include "GameFramework/Actor.h"

UMission::UMission()
{
    Type = EMissionType::Assault;
    Priority = 5;
    TimeLimit = 0.0f;
    Status = EMissionStatus::Inactive;
    Progress = 0.0f;
    TimeActive = 0.0f;
    TimeRemaining = 0.0f;
}

void UMission::Activate()
{
    if (Status == EMissionStatus::Inactive)
    {
        Status = EMissionStatus::Active;
        TimeActive = 0.0f;
        TimeRemaining = TimeLimit;
        Progress = 0.0f;
    }
}

void UMission::Deactivate()
{
    if (Status == EMissionStatus::Active)
    {
        Status = EMissionStatus::Inactive;
    }
}

void UMission::Cancel()
{
    Status = EMissionStatus::Cancelled;
    Progress = 0.0f;
}

void UMission::Tick(float DeltaTime)
{
    if (Status != EMissionStatus::Active)
    {
        return;
    }

    // Update time tracking
    TimeActive += DeltaTime;
    if (TimeLimit > 0.0f)
    {
        TimeRemaining -= DeltaTime;
    }

    // Check for timeout
    if (HasTimedOut())
    {
        Status = EMissionStatus::Failed;
        return;
    }

    // Update progress (override in subclasses)
    UpdateProgress(DeltaTime);

    // Check completion/failure conditions
    if (CheckCompletion())
    {
        Status = EMissionStatus::Completed;
        Progress = 1.0f;
    }
    else if (CheckFailure())
    {
        Status = EMissionStatus::Failed;
    }
}

float UMission::CalculateStrategicReward() const
{
    // Base reward calculation
    // Subclasses should override for specific reward logic

    if (Status == EMissionStatus::Completed)
    {
        return 50.0f;  // Base completion reward
    }

    if (Status == EMissionStatus::Failed)
    {
        return -25.0f;  // Failure penalty
    }

    // Partial credit based on progress
    return Progress * 25.0f;
}

bool UMission::CheckCompletion()
{
    // Override in subclasses
    return false;
}

bool UMission::CheckFailure()
{
    // Override in subclasses
    // Base implementation: timeout is only failure condition
    return HasTimedOut();
}

void UMission::UpdateProgress(float DeltaTime)
{
    // Override in subclasses to calculate actual progress
}

bool UMission::IsTargetValid() const
{
    return TargetActor != nullptr && IsValid(TargetActor);
}

bool UMission::HasTimedOut() const
{
    return TimeLimit > 0.0f && TimeRemaining <= 0.0f;
}
