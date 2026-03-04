// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Team/MocTeamInterface.h"
#include "CapturePoint.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UNiagaraComponent;

/**
 * Team ownership state for capture points
 */
UENUM(BlueprintType)
enum class ECapturePointOwnership : uint8
{
	Neutral		UMETA(DisplayName = "Neutral"),
	RedTeam		UMETA(DisplayName = "Red Team"),
	BlueTeam	UMETA(DisplayName = "Blue Team")
};

/**
 * Point ID for strategic value tracking
 */
UENUM(BlueprintType)
enum class ECapturePointID : uint8
{
	PointA		UMETA(DisplayName = "Point A - Red Base"),
	PointB		UMETA(DisplayName = "Point B - North Outpost"),
	PointC		UMETA(DisplayName = "Point C - Center Plaza"),
	PointD		UMETA(DisplayName = "Point D - South Outpost"),
	PointE		UMETA(DisplayName = "Point E - Blue Base")
};

/**
 * Delegate for capture events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPointCaptured, ECapturePointID, PointID, ECapturePointOwnership, PreviousOwner, ECapturePointOwnership, NewOwner);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCaptureProgressChanged, ECapturePointID, PointID, float, Progress);

/**
 * ACapturePoint - MOC v10.1 Capture Point System
 *
 * Implements MOBA-style capture mechanics per Section 0.2:
 * - Cylindrical capture zone (radius: 5m, height: 3m)
 * - Capture time: 20 seconds continuous majority presence
 * - Contestation: Pause progress when both teams present
 * - Progress decay: 50%/second when capturing team withdraws
 * - Scoring: +25 points on capture, +1 point/second passive income
 *
 * Usage:
 * 1. Place in level at strategic locations
 * 2. Set PointID and InitialOwner
 * 3. Game Mode subscribes to OnPointCaptured for scoring
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API ACapturePoint : public AActor, public IMocTeamInterface
{
	GENERATED_BODY()

public:
	ACapturePoint();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;


	//========================================
	// Team Interface Implementations
	//========================================
	virtual int32 GetTeamID_Implementation() const override;

	
	//========================================
	// Capture System
	//========================================

	/** Update capture progress based on current team presence */
	void UpdateCaptureProgress(float DeltaTime);

	/** Get current ownership state */
	UFUNCTION(BlueprintPure, Category = "CapturePoint")
	ECapturePointOwnership GetOwnership() const { return CurrentOwner; }

	/** Get capture progress [0.0 - 1.0] */
	UFUNCTION(BlueprintPure, Category = "CapturePoint")
	float GetCaptureProgress() const { return CaptureProgress; }

	/** Check if point is contested (both teams present) */
	UFUNCTION(BlueprintPure, Category = "CapturePoint")
	bool IsContested() const;

	/** Get team currently capturing (if any) */
	UFUNCTION(BlueprintPure, Category = "CapturePoint")
	int32 GetCapturingTeam() const;

	/** Check if capture was just completed this tick */
	bool CaptureCompleted() const { return bJustCaptured; }

	/** Get owning team ID (0 = Red, 1 = Blue, -1 = Neutral) */
	UFUNCTION(BlueprintPure, Category = "CapturePoint")
	int32 GetOwningTeamID() const;

	/** Reset point state (for episode reset) */
	UFUNCTION(BlueprintCallable, Category = "CapturePoint")
	void ResetPoint();

	/** Force set ownership (for initial setup) */
	UFUNCTION(BlueprintCallable, Category = "CapturePoint")
	void SetOwnership(ECapturePointOwnership NewOwner);

	//========================================
	// Agent Tracking
	//========================================

	/** Get number of Red team agents in zone */
	UFUNCTION(BlueprintPure, Category = "CapturePoint")
	int32 GetRedTeamCount() const { return RedTeamAgents.Num(); }

	/** Get number of Blue team agents in zone */
	UFUNCTION(BlueprintPure, Category = "CapturePoint")
	int32 GetBlueTeamCount() const { return BlueTeamAgents.Num(); }

	/** Get all agents currently in capture zone */
	UFUNCTION(BlueprintPure, Category = "CapturePoint")
	TArray<AActor*> GetAgentsInZone() const;

protected:
	//========================================
	// Overlap Handlers
	//========================================

	UFUNCTION()
	void OnCaptureZoneBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnCaptureZoneEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	//========================================
	// Internal Logic
	//========================================

	/** Determine which team is capturing based on presence */
	void DetermineMajorityTeam();

	/** Complete capture and transfer ownership */
	void CompleteCaptureSequence();

	/** Update visual representation (colors, progress indicator) */
	void UpdateVisuals();

	/** Update Niagara VFX color based on ownership */
	void UpdateNiagaraColor();

	/** Get team ID from agent actor */
	int32 GetAgentTeamID(AActor* Agent) const;

public:
	//========================================
	// Components
	//========================================

	/** Root component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* RootComp;

	/** Visual mesh for the capture point */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* PointMesh;

	/** Cylindrical capture zone (radius: 500cm = 5m, height: 300cm = 3m) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USphereComponent* CaptureZone;

	/** Debug text renderer showing capture state */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UTextRenderComponent* DebugText;

	/** Niagara VFX for team color identification */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UNiagaraComponent* TeamColorVFX;

	//========================================
	// Configuration
	//========================================

	/** Unique identifier for this capture point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CapturePoint")
	ECapturePointID PointID = ECapturePointID::PointC;

	/** Initial owner at match start */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CapturePoint")
	ECapturePointOwnership InitialOwner = ECapturePointOwnership::Neutral;

	/** Time required to capture (seconds) */
	UPROPERTY(EditAnywhere, Category = "CapturePoint|Balance")
	float CaptureTime = 20.0f;

	/** Progress decay rate when capturing team withdraws (percentage per second) */
	UPROPERTY(EditAnywhere, Category = "CapturePoint|Balance")
	float DecayRate = 0.005f;

	/** Capture zone radius (cm) */
	UPROPERTY(EditAnywhere, Category = "CapturePoint|Zone")
	float CaptureRadius = 500.0f;

	/** Capture zone height (cm) */
	UPROPERTY(EditAnywhere, Category = "CapturePoint|Zone")
	float CaptureHeight = 300.0f;

	/** Strategic bonuses (e.g., "+10% accuracy" for high ground) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CapturePoint|Strategy")
	FString StrategicBonus = TEXT("Standard Position");

	/** Environment ID for parallel environment isolation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CapturePoint")
	int32 EnvID = 0;

	/** Show debug visualization */
	UPROPERTY(EditAnywhere, Category = "CapturePoint|Debug")
	bool bShowDebugInfo = true;

	/** Niagara system asset for team identification VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CapturePoint|VFX")
	class UNiagaraSystem* TeamColorVFXAsset;

	/** VFX color parameter name */
	UPROPERTY(EditAnywhere, Category = "CapturePoint|VFX")
	FName VFXColorParameterName = FName("TeamColor");

	//========================================
	// Events
	//========================================

	/** Broadcast when ownership changes */
	UPROPERTY(BlueprintAssignable, Category = "CapturePoint|Events")
	FOnPointCaptured OnPointCaptured;

	/** Broadcast when capture progress changes (for UI updates) */
	UPROPERTY(BlueprintAssignable, Category = "CapturePoint|Events")
	FOnCaptureProgressChanged OnCaptureProgressChanged;

protected:
	//========================================
	// Runtime State
	//========================================

	/** Current ownership state */
	UPROPERTY(BlueprintReadOnly, Category = "CapturePoint|State")
	ECapturePointOwnership CurrentOwner;

	/** Current capture progress [0.0 - 1.0] */
	UPROPERTY(BlueprintReadOnly, Category = "CapturePoint|State")
	float CaptureProgress = 0.0f;

	/** Team currently attempting capture (-1 = none, 0 = Red, 1 = Blue) */
	UPROPERTY(BlueprintReadOnly, Category = "CapturePoint|State")
	int32 CapturingTeam = -1;

	/** Red team agents in zone */
	UPROPERTY()
	TSet<AActor*> RedTeamAgents;

	/** Blue team agents in zone */
	UPROPERTY()
	TSet<AActor*> BlueTeamAgents;

	/** Flag set when capture completes (cleared next tick) */
	bool bJustCaptured = false;

	/** Previous progress value (for event broadcasting) */
	float PreviousProgress = 0.0f;

	/** Dynamic material instance for visual feedback */
	UPROPERTY()
	UMaterialInstanceDynamic* DynamicMaterial;
};
