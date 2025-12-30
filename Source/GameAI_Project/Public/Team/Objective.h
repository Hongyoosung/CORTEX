// Objective.h - Base class for strategic objectives
// Part of Combat System Refactoring v3.0

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Objective.generated.h"

/**
 * Objective types for strategic planning (v4.0 - Simplified to 4 core strategies)
 * Strategic layer assigns high-level intent, tactical layer (RL) determines execution
 */
UENUM(BlueprintType)
enum class EObjectiveType : uint8
{
	None    UMETA(DisplayName = "None"),      // No objective assigned
    Assault UMETA(DisplayName = "Assault"),   // Offensive: Push toward enemy/objective
    Defend  UMETA(DisplayName = "Defend"),    // Defensive: Hold position/objective
    Support UMETA(DisplayName = "Support"),   // Auxiliary: Provide cover/assistance
    Retreat UMETA(DisplayName = "Retreat")    // Fallback: Disengage and reposition
};

/**
 * Objective status for tracking
 */
UENUM(BlueprintType)
enum class EObjectiveStatus : uint8
{
    Inactive    UMETA(DisplayName = "Inactive"),    // Not started
    Active      UMETA(DisplayName = "Active"),      // Currently executing
    Completed   UMETA(DisplayName = "Completed"),   // Successfully finished
    Failed      UMETA(DisplayName = "Failed"),      // Failed to complete
    Cancelled   UMETA(DisplayName = "Cancelled")    // Cancelled by leader
};

/**
 * Base class for strategic objectives
 * Objectives define WHAT agents should do (strategic layer)
 * Tactical layer (RL policy) determines HOW to execute
 * v4.0: Made concrete (no subclasses needed)
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API UObjective : public UObject
{
    GENERATED_BODY()

public:
    UObjective();

    //========================================
    // Lifecycle methods
    //========================================
    UFUNCTION(BlueprintCallable, Category = "Objective")
    virtual void Activate();

    UFUNCTION(BlueprintCallable, Category = "Objective")
    virtual void Deactivate();

    UFUNCTION(BlueprintCallable, Category = "Objective")
    virtual void Cancel();

    UFUNCTION(BlueprintCallable, Category = "Objective")
    virtual void Tick(float DeltaTime);

    //========================================
    // Status queries
    //========================================
    UFUNCTION(BlueprintPure, Category = "Objective")
    virtual bool IsActive() const { return Status == EObjectiveStatus::Active; }

    UFUNCTION(BlueprintPure, Category = "Objective")
    virtual bool IsCompleted() const { return Status == EObjectiveStatus::Completed; }

    UFUNCTION(BlueprintPure, Category = "Objective")
    virtual bool IsFailed() const { return Status == EObjectiveStatus::Failed; }

    UFUNCTION(BlueprintPure, Category = "Objective")
    virtual float GetProgress() const { return Progress; }

    //========================================
    // Reward calculation for MCTS/RL
    //========================================
    UFUNCTION(BlueprintPure, Category = "Objective")
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
    // Helper to check if objective timed out
    //========================================
    bool HasTimedOut() const;


public:
    //========================================
    // Core properties
    //========================================
    UPROPERTY(BlueprintReadWrite, Category = "Objective")
    EObjectiveType Type;

    UPROPERTY(BlueprintReadWrite, Category = "Objective")
    TObjectPtr<AActor> TargetActor = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "Objective")
    FVector TargetLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "Objective")
    int32 Priority = 5;  // 0-10, higher = more important

    UPROPERTY(BlueprintReadWrite, Category = "Objective")
    float TimeLimit = 0.0f;  // 0 = no limit (seconds)

    UPROPERTY(BlueprintReadWrite, Category = "Objective")
    TArray<TObjectPtr<AActor>> AssignedAgents;

    // State tracking
    UPROPERTY(BlueprintReadOnly, Category = "Objective")
    EObjectiveStatus Status = EObjectiveStatus::Inactive;

    UPROPERTY(BlueprintReadOnly, Category = "Objective")
    float Progress = 0.0f;  // 0.0-1.0

    UPROPERTY(BlueprintReadOnly, Category = "Objective")
    float TimeActive = 0.0f;  // Time since activation

    UPROPERTY(BlueprintReadOnly, Category = "Objective")
    float TimeRemaining = 0.0f;  // Time until timeout
};
