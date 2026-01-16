// ObjectiveActor.h - Durability-based objective system (v7.0)
// Part of Objective System Refactoring - replaces type-based Defend/Capture distinction

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ObjectiveActor.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class UMaterialInstanceDynamic;

/**
 * v7.0 Durability-Based Objective System
 *
 * Unified objective actor with capture volume and durability mechanics.
 * Replaces type-based system (Defend/Capture) with team ownership model.
 *
 * Key Features:
 * - Durability system (0-100%, declines with hostile agents in volume)
 * - Team ownership (OwnerTeamID determines friendly vs hostile)
 * - Capture volume (spherical trigger for agent tracking)
 * - Visual feedback (team colors, durability-based emission)
 * - Recovery mechanics (regenerates when no hostiles present)
 *
 * Design:
 * - Both teams have identical objectives (defend yours, capture theirs)
 * - No Defend/Capture type distinction - determined by OwnerTeamID comparison
 * - Volume-based rewards (continuous reward for staying in volume)
 * - Clear defeat condition (durability reaches 0)
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API AObjectiveActor : public AActor
{
	GENERATED_BODY()

public:
	AObjectiveActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	//========================================
	// Durability System
	//========================================

	/** Get current durability as percentage [0.0-1.0] */
	UFUNCTION(BlueprintPure, Category = "Objective")
	float GetDurabilityPercent() const { return FMath::Clamp(CurrentDurability / MaxDurability, 0.0f, 1.0f); }

	/** Check if objective is defeated (durability <= 0) */
	UFUNCTION(BlueprintPure, Category = "Objective")
	bool IsDefeated() const { return CurrentDurability <= 0.0f; }

	/** Reset durability to maximum (for episode reset) */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void ResetDurability();

	//========================================
	// Team Association
	//========================================

	/** Check if this objective belongs to the given team */
	UFUNCTION(BlueprintPure, Category = "Objective")
	bool IsFriendlyTo(int32 AgentTeamID) const { return OwnerTeamID == AgentTeamID; }

	/** Check if this objective is hostile to the given team */
	UFUNCTION(BlueprintPure, Category = "Objective")
	bool IsHostileTo(int32 AgentTeamID) const { return OwnerTeamID != AgentTeamID; }

	/** Get number of hostile agents currently in volume */
	UFUNCTION(BlueprintPure, Category = "Objective")
	int32 GetHostileAgentCount() const { return HostileAgentsInVolume.Num(); }

	/** Check if specific agent is inside capture volume */
	UFUNCTION(BlueprintPure, Category = "Objective")
	bool IsAgentInVolume(AActor* Agent) const;

protected:
	//========================================
	// Overlap Handlers
	//========================================

	UFUNCTION()
	void OnCaptureVolumeBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnCaptureVolumeEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	//========================================
	// Durability Logic
	//========================================

	/** Update durability based on hostile agent count (called by timer) */
	void UpdateDurability();

	/** Called when durability reaches 0 */
	void OnDefeat();

	/** Update material to reflect team color and durability state */
	void UpdateMaterial();

public:
	//========================================
	// Delegates
	//========================================

	/** Broadcast when this objective is defeated (durability reaches 0) */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnObjectiveDefeated, int32, DefeatedTeamID);
	UPROPERTY(BlueprintAssignable, Category = "Objective|Events")
	FOnObjectiveDefeated OnObjectiveDefeated;

public:
	//========================================
	// Components
	//========================================

	/** Central pillar mesh (visual representation) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> PillarMesh;

	/** Spherical capture volume (trigger for agent tracking) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CaptureVolume;

	//========================================
	// Configuration
	//========================================

	/** Team ID that owns this objective (0 or 1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective")
	int32 OwnerTeamID = 0;

	/** Maximum durability value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objective|Durability")
	float MaxDurability = 100.0f;

	/** Current durability (0.0 to MaxDurability) */
	UPROPERTY(BlueprintReadOnly, Category = "Objective|Durability")
	float CurrentDurability = 100.0f;

	/** Durability decline per hostile agent per second */
	UPROPERTY(EditAnywhere, Category = "Objective|Balance")
	float DamagePerAgentPerSecond = 2.0f;

	/** Durability recovery per second (when no hostiles present) */
	UPROPERTY(EditAnywhere, Category = "Objective|Balance")
	float RecoveryPerSecond = 1.0f;

	/** Capture volume radius (cm) */
	UPROPERTY(EditAnywhere, Category = "Objective|Volume")
	float CaptureRadius = 1000.0f;

	/** Show debug visualization (durability text, volume bounds) */
	UPROPERTY(EditAnywhere, Category = "Objective|Debug")
	bool bShowDebugVisualization = true;

protected:
	//========================================
	// Runtime State
	//========================================

	/** Set of hostile agents currently within capture volume */
	UPROPERTY()
	TSet<TObjectPtr<AActor>> HostileAgentsInVolume;

	/** Dynamic material instance for team color/durability feedback */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	/** Timer handle for durability updates (10Hz) */
	FTimerHandle DurabilityUpdateTimer;

	/** Was defeated on previous check (prevents spam) */
	bool bWasDefeated = false;
};
