// Objective.cpp - Base class for strategic objectives

#include "Team/Objective.h"
#include "GameFramework/Actor.h"

UObjective::UObjective()
{
    Type = EObjectiveType::Assault;
    Priority = 5;
    TimeLimit = 0.0f;
    Status = EObjectiveStatus::Inactive;
    Progress = 0.0f;
    TimeActive = 0.0f;
    TimeRemaining = 0.0f;
}

void UObjective::Activate()
{
    if (Status == EObjectiveStatus::Inactive)
    {
        Status = EObjectiveStatus::Active;
        TimeActive = 0.0f;
        TimeRemaining = TimeLimit;
        Progress = 0.0f;
    }
}

void UObjective::Deactivate()
{
    if (Status == EObjectiveStatus::Active)
    {
        Status = EObjectiveStatus::Inactive;
    }
}

void UObjective::Cancel()
{
    Status = EObjectiveStatus::Cancelled;
    Progress = 0.0f;
}

void UObjective::Tick(float DeltaTime)
{
    if (Status != EObjectiveStatus::Active)
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
        Status = EObjectiveStatus::Failed;
        return;
    }

    // Update progress (override in subclasses)
    UpdateProgress(DeltaTime);

    // Check completion/failure conditions
    if (CheckCompletion())
    {
        Status = EObjectiveStatus::Completed;
        Progress = 1.0f;
    }
    else if (CheckFailure())
    {
        Status = EObjectiveStatus::Failed;
    }
}

float UObjective::CalculateStrategicReward() const
{
    // Base reward calculation
    // Subclasses should override for specific reward logic

    if (Status == EObjectiveStatus::Completed)
    {
        return 50.0f;  // Base completion reward
    }

    if (Status == EObjectiveStatus::Failed)
    {
        return -25.0f;  // Failure penalty
    }

    // Partial credit based on progress
    return Progress * 25.0f;
}

bool UObjective::CheckCompletion()
{
    // Override in subclasses
    return false;
}

bool UObjective::CheckFailure()
{
    // Override in subclasses
    // Base implementation: timeout is only failure condition
    return HasTimedOut();
}

void UObjective::UpdateProgress(float DeltaTime)
{
    // Override in subclasses to calculate actual progress
}

bool UObjective::IsTargetValid() const
{
    return TargetActor != nullptr && IsValid(TargetActor);
}

bool UObjective::HasTimedOut() const
{
    return TimeLimit > 0.0f && TimeRemaining <= 0.0f;
}
