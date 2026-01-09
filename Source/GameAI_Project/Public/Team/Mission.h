// Mission.h - Base class for strategic missions
// Part of Combat System Refactoring v7.0
// Renamed from Objective.h to clarify distinction from ObjectiveActor (physical bases)

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Mission.generated.h"

/**
 * Mission types for strategic planning (v7.0 - Clarified naming)
 * Strategic layer assigns high-level intent, tactical layer (RL) determines execution
 *
 * v7.0 NAMING CHANGE: Objective → Mission
 * - Mission: Strategic goal assigned by MCTS (abstract)
 * - Objective: Physical base/capture point (AObjectiveActor)
 */
UENUM(BlueprintType)
enum class EMissionType : uint8
{
	None    UMETA(DisplayName = "None"),      // No mission assigned
    Assault UMETA(DisplayName = "Assault"),   // Offensive: Push toward enemy/objective
    Defend  UMETA(DisplayName = "Defend"),    // Defensive: Hold position/objective
    Support UMETA(DisplayName = "Support"),   // Auxiliary: Provide cover/assistance
    Retreat UMETA(DisplayName = "Retreat")    // Fallback: Disengage and reposition
};

/**
 * Mission status for tracking
 */
UENUM(BlueprintType)
enum class EMissionStatus : uint8
{
    Inactive    UMETA(DisplayName = "Inactive"),    // Not started
    Active      UMETA(DisplayName = "Active"),      // Currently executing
    Completed   UMETA(DisplayName = "Completed"),   // Successfully finished
    Failed      UMETA(DisplayName = "Failed"),      // Failed to complete
    Cancelled   UMETA(DisplayName = "Cancelled")    // Cancelled by leader
};

/**
 * Base class for strategic missions
 * Missions define WHAT agents should do (strategic layer)
 * Tactical layer (RL policy) determines HOW to execute
 * v7.0: Renamed from UObjective to distinguish from AObjectiveActor (physical bases)
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API UMission : public UObject
{
    GENERATED_BODY()

public:
    UMission();

    //========================================
    // Lifecycle methods
    //========================================
    UFUNCTION(BlueprintCallable, Category = "Mission")
    virtual void Activate();

    UFUNCTION(BlueprintCallable, Category = "Mission")
    virtual void Deactivate();

    UFUNCTION(BlueprintCallable, Category = "Mission")
    virtual void Cancel();

    UFUNCTION(BlueprintCallable, Category = "Mission")
    virtual void Tick(float DeltaTime);

    //========================================
    // Status queries
    //========================================
    UFUNCTION(BlueprintPure, Category = "Mission")
    virtual bool IsActive() const { return Status == EMissionStatus::Active; }

    UFUNCTION(BlueprintPure, Category = "Mission")
    virtual bool IsCompleted() const { return Status == EMissionStatus::Completed; }

    UFUNCTION(BlueprintPure, Category = "Mission")
    virtual bool IsFailed() const { return Status == EMissionStatus::Failed; }

    UFUNCTION(BlueprintPure, Category = "Mission")
    virtual float GetProgress() const { return Progress; }

    //========================================
    // Reward calculation for MCTS/RL
    //========================================
    UFUNCTION(BlueprintPure, Category = "Mission")
    virtual float CalculateStrategicReward() const;

    //========================================
    // Override these in subclasses
    //========================================
    virtual bool CheckCompletion();
    virtual bool CheckFailure();
    virtual void UpdateProgress(float DeltaTime);


protected:
    //========================================
    // Helper to check if target actor is still valid
    //========================================
    bool IsTargetValid() const;

    //========================================
    // Helper to check if mission timed out
    //========================================
    bool HasTimedOut() const;


public:
    //========================================
    // Core properties
    //========================================
    UPROPERTY(BlueprintReadWrite, Category = "Mission")
    EMissionType Type;

    UPROPERTY(BlueprintReadWrite, Category = "Mission")
    TObjectPtr<AActor> TargetActor = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "Mission")
    FVector TargetLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "Mission")
    int32 Priority = 5;  // 0-10, higher = more important

    UPROPERTY(BlueprintReadWrite, Category = "Mission")
    float TimeLimit = 0.0f;  // 0 = no limit (seconds)

    UPROPERTY(BlueprintReadWrite, Category = "Mission")
    TArray<TObjectPtr<AActor>> AssignedAgents;

    // State tracking
    UPROPERTY(BlueprintReadOnly, Category = "Mission")
    EMissionStatus Status = EMissionStatus::Inactive;

    UPROPERTY(BlueprintReadOnly, Category = "Mission")
    float Progress = 0.0f;  // 0.0-1.0

    UPROPERTY(BlueprintReadOnly, Category = "Mission")
    float TimeActive = 0.0f;  // Time since activation

    UPROPERTY(BlueprintReadOnly, Category = "Mission")
    float TimeRemaining = 0.0f;  // Time until timeout
};
