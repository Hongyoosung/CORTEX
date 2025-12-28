// ObjectiveManager.h - Manages strategic objectives for team

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Team/Objective.h"
#include "ObjectiveManager.generated.h"

/**
 * Manages the lifecycle of strategic objectives
 * Tracks which agents are assigned to which objectives
 * Provides queries for active objectives and agent assignments
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UObjectiveManager : public UActorComponent
{
    GENERATED_BODY()

public:
    UObjectiveManager();

    // Component lifecycle
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // Objective creation
    UFUNCTION(BlueprintCallable, Category = "Objective")
    UObjective* CreateObjective(EObjectiveType Type, AActor* Target, const FVector& Location, int32 Priority = 5);

    UFUNCTION(BlueprintCallable, Category = "Objective")
    UObjective* CreateEliminateObjective(AActor* Target, int32 Priority = 7);

    UFUNCTION(BlueprintCallable, Category = "Objective")
    UObjective* CreateCaptureObjective(const FVector& Location, int32 Priority = 8);

    UFUNCTION(BlueprintCallable, Category = "Objective")
    UObjective* CreateDefendObjective(const FVector& Location, int32 Priority = 7);

    UFUNCTION(BlueprintCallable, Category = "Objective")
    UObjective* CreateSupportObjective(AActor* AllyTarget, int32 Priority = 6);

    UFUNCTION(BlueprintCallable, Category = "Objective")
    UObjective* CreateRescueObjective(AActor* WoundedAlly, int32 Priority = 7);

    // Agent assignment
    UFUNCTION(BlueprintCallable, Category = "Objective")
    void AssignAgentsToObjective(UObjective* Objective, const TArray<AActor*>& Agents);

    UFUNCTION(BlueprintCallable, Category = "Objective")
    void UnassignAgentFromObjective(AActor* Agent);

    UFUNCTION(BlueprintCallable, Category = "Objective")
    void ClearAllAssignments();

    // Objective lifecycle
    UFUNCTION(BlueprintCallable, Category = "Objective")
    void ActivateObjective(UObjective* Objective);

    UFUNCTION(BlueprintCallable, Category = "Objective")
    void DeactivateObjective(UObjective* Objective);

    UFUNCTION(BlueprintCallable, Category = "Objective")
    void CancelObjective(UObjective* Objective);

    UFUNCTION(BlueprintCallable, Category = "Objective")
    void RemoveObjective(UObjective* Objective);

    UFUNCTION(BlueprintCallable, Category = "Objective")
    void ClearCompletedObjectives();

    // Queries
    UFUNCTION(BlueprintPure, Category = "Objective")
    TArray<UObjective*> GetActiveObjectives() const;

    UFUNCTION(BlueprintPure, Category = "Objective")
    TArray<UObjective*> GetAllObjectives() const;

    UFUNCTION(BlueprintPure, Category = "Objective")
    UObjective* GetAgentObjective(AActor* Agent) const;

    UFUNCTION(BlueprintPure, Category = "Objective")
    TArray<AActor*> GetObjectiveAgents(UObjective* Objective) const;

    UFUNCTION(BlueprintPure, Category = "Objective")
    int32 GetActiveObjectiveCount() const;

    UFUNCTION(BlueprintPure, Category = "Objective")
    UObjective* GetHighestPriorityObjective() const;

    // Rewards
    UFUNCTION(BlueprintPure, Category = "Objective")
    float CalculateTotalTeamReward() const;

protected:
    virtual void BeginPlay() override;

private:
    // All objectives (active and inactive)
    UPROPERTY()
    TArray<TObjectPtr<UObjective>> Objectives;

    // Agent to objective mapping
    UPROPERTY()
    TMap<TObjectPtr<AActor>, TObjectPtr<UObjective>> AgentObjectiveMap;

    // v4.0: Removed CreateObjectiveOfType<T> template (no subclasses)

    // Update all active objectives
    void TickObjectives(float DeltaTime);

    // Clean up completed/failed objectives
    void CleanupObjectives();
};
