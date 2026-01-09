// MissionManager.h - Manages strategic missions for team
// v7.0: Renamed from ObjectiveManager to clarify distinction from ObjectiveActor

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Team/Mission.h"
#include "MissionManager.generated.h"

/**
 * Manages the lifecycle of strategic missions
 * Tracks which agents are assigned to which missions
 * Provides queries for active missions and agent assignments
 *
 * v7.0 NAMING: Mission = strategic goal (abstract), Objective = physical base (AObjectiveActor)
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UMissionManager : public UActorComponent
{
    GENERATED_BODY()

public:
    UMissionManager();

    // Component lifecycle
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // Mission creation
    UFUNCTION(BlueprintCallable, Category = "Mission")
    UMission* CreateMission(EMissionType Type, AActor* Target, const FVector& Location, int32 Priority = 5);

    UFUNCTION(BlueprintCallable, Category = "Mission")
    UMission* CreateEliminateMission(AActor* Target, int32 Priority = 7);

    UFUNCTION(BlueprintCallable, Category = "Mission")
    UMission* CreateCaptureMission(const FVector& Location, int32 Priority = 8);

    UFUNCTION(BlueprintCallable, Category = "Mission")
    UMission* CreateDefendMission(const FVector& Location, int32 Priority = 7);

    UFUNCTION(BlueprintCallable, Category = "Mission")
    UMission* CreateSupportMission(AActor* AllyTarget, int32 Priority = 6);

    UFUNCTION(BlueprintCallable, Category = "Mission")
    UMission* CreateRescueMission(AActor* WoundedAlly, int32 Priority = 7);

    // Agent assignment
    UFUNCTION(BlueprintCallable, Category = "Mission")
    void AssignAgentsToMission(UMission* Mission, const TArray<AActor*>& Agents);

    UFUNCTION(BlueprintCallable, Category = "Mission")
    void UnassignAgentFromMission(AActor* Agent);

    UFUNCTION(BlueprintCallable, Category = "Mission")
    void ClearAllAssignments();

    // Mission lifecycle
    UFUNCTION(BlueprintCallable, Category = "Mission")
    void ActivateMission(UMission* Mission);

    UFUNCTION(BlueprintCallable, Category = "Mission")
    void DeactivateMission(UMission* Mission);

    UFUNCTION(BlueprintCallable, Category = "Mission")
    void CancelMission(UMission* Mission);

    UFUNCTION(BlueprintCallable, Category = "Mission")
    void RemoveMission(UMission* Mission);

    UFUNCTION(BlueprintCallable, Category = "Mission")
    void ClearCompletedMissions();

    // Queries
    UFUNCTION(BlueprintPure, Category = "Mission")
    TArray<UMission*> GetActiveMissions() const;

    UFUNCTION(BlueprintPure, Category = "Mission")
    TArray<UMission*> GetAllMissions() const;

    UFUNCTION(BlueprintPure, Category = "Mission")
    UMission* GetAgentMission(AActor* Agent) const;

    UFUNCTION(BlueprintPure, Category = "Mission")
    TArray<AActor*> GetMissionAgents(UMission* Mission) const;

    UFUNCTION(BlueprintPure, Category = "Mission")
    int32 GetActiveMissionCount() const;

    UFUNCTION(BlueprintPure, Category = "Mission")
    UMission* GetHighestPriorityMission() const;

    // Rewards
    UFUNCTION(BlueprintPure, Category = "Mission")
    float CalculateTotalTeamReward() const;

    //========================================
    // v7.0: ObjectiveActor Integration
    // Note: These methods find physical objectives (bases), not missions
    //========================================

    /** Find all objective actors (physical bases) in the world */
    UFUNCTION(BlueprintCallable, Category = "Objective")
    TArray<class AObjectiveActor*> FindAllObjectiveActors() const;

    /** Find friendly objective actor (OwnerTeamID matches) */
    UFUNCTION(BlueprintCallable, Category = "Objective")
    AObjectiveActor* FindFriendlyObjective(int32 TeamID) const;

    /** Find hostile objective actor (OwnerTeamID doesn't match) */
    UFUNCTION(BlueprintCallable, Category = "Objective")
    AObjectiveActor* FindHostileObjective(int32 TeamID) const;

protected:
    virtual void BeginPlay() override;

private:
    // All missions (active and inactive)
    UPROPERTY()
    TArray<TObjectPtr<UMission>> Missions;

    // Agent to mission mapping
    UPROPERTY()
    TMap<TObjectPtr<AActor>, TObjectPtr<UMission>> AgentMissionMap;

    // Update all active missions
    void TickMissions(float DeltaTime);

    // Clean up completed/failed missions
    void CleanupMissions();
};
