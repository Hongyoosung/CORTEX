// VisualLoggerComponent.h
// Phase 3: Debug visualization (extracted from TeamLeaderComponent and FollowerAgentComponent)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/MocTypes.h"
#include "VisualLoggerComponent.generated.h"

/**
 * Visual Logger Component
 *
 * Centralizes debug visualization for team components.
 * Extracts all DrawDebug* calls from TeamLeader and FollowerAgent.
 *
 * Benefits:
 * - Clean separation of debug logic from core logic
 * - Can be fully disabled in shipping builds
 * - Centralized visualization settings
 * - Easier to maintain and extend
 *
 * Usage:
 * 1. Attach to actor needing debug visualization
 * 2. Call drawing methods (DrawAgentState, DrawTeamInfo, etc.)
 * 3. Enable/disable via bEnableDebugDrawing
 */
UCLASS(ClassGroup=(Debug), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UVisualLoggerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVisualLoggerComponent();

	// ========== Configuration ==========

	/** Enable all debug drawing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Logger")
	bool bEnableDebugDrawing = false;

	/** Draw agent state info (strategy, health, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Logger")
	bool bDrawAgentInfo = true;

	/** Draw team leader info (follower count, MCTS status, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Logger")
	bool bDrawTeamInfo = true;

	/** Draw tactical parameters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Logger")
	bool bDrawTacticalParams = true;

	/** Draw combat information */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Logger")
	bool bDrawCombatInfo = true;

	/** Draw objective lines */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Logger")
	bool bDrawObjectives = true;

	/** Draw formation info */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Logger")
	bool bDrawFormation = true;

	/** Debug text height offset (above actor) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Logger")
	float TextHeightOffset = 100.0f;

	/** Debug line thickness */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Logger")
	float LineThickness = 2.0f;

	/** Debug sphere radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Logger")
	float SphereRadius = 30.0f;

	// ========== Follower Visualization ==========

	/**
	 * Draw follower agent state.
	 * Shows strategy, health, objective, tactical parameters.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visual Logger")
	void DrawFollowerState(
		const FVector& Location,
		EStrategyType Strategy,
		float Health,
		const FTacticalParameters& TacticalParams,
		AActor* TargetObjective = nullptr
	);

	/**
	 * Draw combat information.
	 * Shows target, distance, weapon status.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visual Logger")
	void DrawCombatState(
		const FVector& Location,
		AActor* Target,
		float DistanceToTarget,
		bool bIsAiming,
		bool bIsFiring
	);

	/**
	 * Draw line to target (enemy, objective, etc.).
	 */
	UFUNCTION(BlueprintCallable, Category = "Visual Logger")
	void DrawLineToTarget(
		const FVector& Start,
		const FVector& End,
		FLinearColor Color = FLinearColor::Red,
		bool bDrawArrow = true
	);

	// ========== Leader Visualization ==========

	/**
	 * Draw team leader state.
	 * Shows follower count, MCTS status, team metrics.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visual Logger")
	void DrawTeamLeaderState(
		const FVector& Location,
		int32 FollowerCount,
		int32 AliveFollowerCount,
		bool bIsMCTSRunning,
		float LastMCTSTime
	);

	/**
	 * Draw formation info.
	 * Shows follower positions and distances.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visual Logger")
	void DrawFormationInfo(
		const FVector& LeaderLocation,
		const TArray<AActor*>& Followers
	);

	/**
	 * Draw objective markers.
	 * Shows friendly and hostile objectives with labels.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visual Logger")
	void DrawObjectiveMarkers(
		AActor* FriendlyObjective,
		AActor* HostileObjective
	);

	// ========== Generic Primitives ==========

	/**
	 * Draw debug text at world location.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visual Logger")
	void DrawDebugText(
		const FVector& Location,
		const FString& Text,
		FLinearColor Color = FLinearColor::White,
		float Duration = 0.0f
	);

	/**
	 * Draw debug sphere at world location.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visual Logger")
	void DrawSphere(
		const FVector& Location,
		float Radius,
		FLinearColor Color = FLinearColor::Green,
		float Duration = 0.0f
	);

	/**
	 * Draw debug line between two points.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visual Logger")
	void DrawLine(
		const FVector& Start,
		const FVector& End,
		FLinearColor Color = FLinearColor::White,
		float Duration = 0.0f,
		float Thickness = 1.0f
	);

	/**
	 * Draw debug arrow between two points.
	 */
	UFUNCTION(BlueprintCallable, Category = "Visual Logger")
	void DrawArrow(
		const FVector& Start,
		const FVector& End,
		FLinearColor Color = FLinearColor::White,
		float ArrowSize = 50.0f,
		float Duration = 0.0f,
		float Thickness = 1.0f
	);

private:
	/**
	 * Get strategy color for visualization.
	 */
	FLinearColor GetStrategyColor(EStrategyType Strategy) const;

	/**
	 * Format tactical parameters as string.
	 */
	FString FormatTacticalParams(const FTacticalParameters& Params) const;

	/**
	 * Check if debug drawing is enabled (master switch + shipping build check).
	 */
	bool IsDebugDrawingEnabled() const;
};
