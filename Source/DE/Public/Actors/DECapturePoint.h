// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Team/DETeamInterface.h"
#include "DECapturePoint.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UNiagaraComponent;
class ADEMatchManager;

/**
 * Point ID for strategic value tracking
 */
UENUM(BlueprintType)
enum class ECapturePointID : uint8
{
	PointA		UMETA(DisplayName = "Point A - Base 1"),
	PointB		UMETA(DisplayName = "Point B - North Outpost"),
	PointC		UMETA(DisplayName = "Point C - Center Plaza"),
	PointD		UMETA(DisplayName = "Point D - South Outpost"),
	PointE		UMETA(DisplayName = "Point E - Base 2")
};


USTRUCT()
struct FTeamAgentSet
{
	GENERATED_BODY()

	UPROPERTY()
	TSet<AActor*> Agents;
};

/**
 * Delegates for capture events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPointCaptured, int32, PreviousTeam, int32, NewTeam);


UCLASS(Blueprintable)
class DE_API ADECapturePoint : public AActor, public IDETeamInterface
{
	GENERATED_BODY()

public:
	ADECapturePoint();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	//========================================
	// Team Interface Implementations
	//========================================
	virtual int32 GetTeamID_Implementation() const override;
	virtual int32 GetEnvID_Implementation() const override;
	virtual void SetTeamID_Implementation(int32 NewTeamID) override;
	virtual void SetEnvID_Implementation(int32 NewEnvID) override;

	//========================================
	// Capture System
	//========================================

	void UpdateCaptureProgress(float DeltaTime);

	UFUNCTION(BlueprintPure, Category = "DECapturePoint")
	int32 GetOwnership() const { return CurrentOwner; }

	UFUNCTION(BlueprintPure, Category = "DECapturePoint")
	float GetCaptureProgress() const { return CaptureProgress; }

	UFUNCTION(BlueprintPure, Category = "DECapturePoint")
	bool IsContested() const;

	UFUNCTION(BlueprintPure, Category = "DECapturePoint")
	int32 GetCapturingTeam() const { return CapturingTeam; }

	bool CaptureCompleted() const { return bJustCaptured; }

	UFUNCTION(BlueprintCallable, Category = "DECapturePoint")
	void ResetPoint();

	UFUNCTION(BlueprintCallable, Category = "DECapturePoint")
	void SetOwnership(int32 NewOwnerTeamID);

	void SetMatchManager(ADEMatchManager* InMatchManager) { OwnerMatchManager = InMatchManager; }

	//========================================
	// Agent Tracking
	//========================================

	UFUNCTION(BlueprintPure, Category = "DECapturePoint")
	int32 GetTeamCountInZone(int32 TeamID) const;

	UFUNCTION(BlueprintPure, Category = "DECapturePoint")
	TArray<AActor*> GetAgentsInZone() const;

protected:
	//========================================
	// Overlap Handlers
	//========================================
	UFUNCTION()
	void OnCaptureZoneBeginOverlap(
		UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnCaptureZoneEndOverlap(
		UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	//========================================
	// Internal Logic
	//========================================
	void CompleteCaptureSequence();
	void UpdateVisuals();
	void UpdateNiagaraColor();
	int32 GetAgentTeamID(AActor* Agent) const;
	FLinearColor GetTeamColor(int32 TeamID) const;

public:
	//========================================
	// Components
	//========================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* RootComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* PointMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USphereComponent* CaptureZone;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UTextRenderComponent* DebugText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UNiagaraComponent* TeamColorVFX;

	//========================================
	// Configuration
	//========================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DECapturePoint")
	ECapturePointID PointID = ECapturePointID::PointC;

	/** Initial owner at match start (-1 for Neutral) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DECapturePoint")
	int32 InitialOwner = -1;

	UPROPERTY(EditAnywhere, Category = "DECapturePoint|Balance")
	float CaptureTime = 20.0f;

	UPROPERTY(EditAnywhere, Category = "DECapturePoint|Balance")
	float DecayRate = 0.005f;

	UPROPERTY(EditAnywhere, Category = "DECapturePoint|Zone")
	float CaptureRadius = 500.0f;

	UPROPERTY(EditAnywhere, Category = "DECapturePoint|Zone")
	float CaptureHeight = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DECapturePoint|Class")
	FString StrategicBonus = TEXT("Standard Position");

	UPROPERTY(EditAnywhere, Category = "DECapturePoint|Debug")
	bool bShowDebugInfo = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DECapturePoint|VFX")
	class UNiagaraSystem* TeamColorVFXAsset;

	UPROPERTY(EditAnywhere, Category = "DECapturePoint|VFX")
	FName VFXColorParameterName = FName("TeamColor");

	//========================================
	// Events
	//========================================
	UPROPERTY(BlueprintAssignable, Category = "DECapturePoint|Events")
	FOnPointCaptured OnPointCaptured_Delegate;



protected:
	//========================================
	// Runtime State
	//========================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DECapturePoint")
	int32 EnvID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "DECapturePoint|State")
	int32 CurrentOwner = -1;

	UPROPERTY(BlueprintReadOnly, Category = "DECapturePoint|State")
	float CaptureProgress = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "DECapturePoint|State")
	int32 CapturingTeam = -1;

	/** Dynamically maps TeamID to agents inside the zone */
	UPROPERTY()
	TMap<int32, FTeamAgentSet> AgentsInZoneByTeam;

	bool bJustCaptured = false;
	float PreviousProgress = 0.0f;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	UPROPERTY()
	TObjectPtr<ADEMatchManager> OwnerMatchManager = nullptr;
};