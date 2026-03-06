// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FogOfWarManager.generated.h"

class UTextureRenderTarget2D;

/**
 * Explored resource structure for permanent memory
 */
USTRUCT(BlueprintType)
struct FExploredResource
{
	GENERATED_BODY()

	/** Resource actor reference */
	UPROPERTY(BlueprintReadWrite)
	AActor* ActorRef = nullptr;

	/** Resource location (cached) */
	UPROPERTY(BlueprintReadWrite)
	FVector Location = FVector::ZeroVector;

	/** Is resource currently available? (updated when in sight) */
	UPROPERTY(BlueprintReadWrite)
	bool bIsAvailable = true;

	/** Last time this resource was seen (for staleness checks) */
	UPROPERTY(BlueprintReadWrite)
	float LastSeenTime = 0.0f;

	FExploredResource() = default;

	FExploredResource(AActor* InActor, FVector InLocation, bool bInAvailable, float InTime)
		: ActorRef(InActor)
		, Location(InLocation)
		, bIsAvailable(bInAvailable)
		, LastSeenTime(InTime)
	{}
};

/**
 * AFogOfWarManager - MOC v10.1 Fog of War System
 *
 * Responsibilities:
 * - Dynamic Objects (Enemies): 5-second memory decay after losing sight
 * - Static Objects (Resources/Bases): Permanent discovery with state updates
 * - Visual Rendering: Render Target preparation for fog-of-war display
 *
 * Memory Logic:
 * 1. **Enemies:** Positions are remembered for 5 seconds after last sighting, then forgotten
 * 2. **Resources:** Once discovered, permanently remembered. Availability status updated only when visible
 *
 * Visual System (Prepared):
 * - Render Target textures for each team's fog-of-war visualization
 * - UpdateVision() called by agents to clear fog (ready for GPU-based rendering)
 *
 * Usage:
 * 1. Place in level (one per game, or spawn from GameMode)
 * 2. MatchManager references this manager
 * 3. MocCharacter calls UpdateVision() every Tick
 * 4. AI queries enemy/resource positions through MatchManager → FogOfWarManager
 */
UCLASS()
class GAMEAI_PROJECT_API AFogOfWarManager : public AActor
{
	GENERATED_BODY()

public:
	AFogOfWarManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	//========================================
	// Dynamic Objects (Enemies) - 5-Second Memory
	//========================================

	/**
	 * Report enemy sighting (updates timestamp)
	 * @param TeamID - Team that spotted the enemy (0 = Red, 1 = Blue)
	 * @param Enemy - Enemy actor reference
	 * @param Location - Last known position
	 */
	UFUNCTION(BlueprintCallable, Category = "FogOfWar")
	void ReportEnemy(int32 TeamID, AActor* Enemy, FVector Location);

	/**
	 * Get last known enemy position
	 * @param TeamID - Querying team
	 * @param Enemy - Enemy to query
	 * @return Last known position (ZeroVector if not known or expired)
	 */
	UFUNCTION(BlueprintPure, Category = "FogOfWar")
	FVector GetLastKnownEnemyPosition(int32 TeamID, AActor* Enemy) const;

	/**
	 * Check if enemy position is still valid (< 5 seconds old)
	 * @param TeamID - Querying team
	 * @param Enemy - Enemy to check
	 * @return True if position is valid and not expired
	 */
	UFUNCTION(BlueprintPure, Category = "FogOfWar")
	bool IsEnemyPositionValid(int32 TeamID, AActor* Enemy) const;

	/**
	 * Get all currently remembered enemies for a team
	 * @param TeamID - Querying team
	 * @return Array of enemy actors with valid positions
	 */
	UFUNCTION(BlueprintPure, Category = "FogOfWar")
	TArray<AActor*> GetRememberedEnemies(int32 TeamID) const;

	//========================================
	// Static Objects (Resources) - Permanent Memory
	//========================================

	/**
	 * Report resource discovery (adds to permanent memory or updates state)
	 * @param TeamID - Team that discovered the resource
	 * @param Resource - Resource actor reference
	 * @param bAvailable - Is resource currently available for pickup?
	 */
	UFUNCTION(BlueprintCallable, Category = "FogOfWar")
	void ReportResource(int32 TeamID, AActor* Resource, bool bAvailable);

	/**
	 * Get all known resources for a team
	 * @param TeamID - Querying team
	 * @return Array of explored resources (permanent memory)
	 */
	UFUNCTION(BlueprintPure, Category = "FogOfWar")
	TArray<FExploredResource> GetKnownResources(int32 TeamID) const;

	/**
	 * Get known resources of specific type by location
	 * @param TeamID - Querying team
	 * @param ResourceType - Filter by pickup type (optional)
	 * @return Array of resource locations
	 */
	UFUNCTION(BlueprintPure, Category = "FogOfWar")
	TArray<FVector> GetKnownResourceLocations(int32 TeamID) const;

	//========================================
	// Visual System (Render Target)
	//========================================

	/**
	 * Update vision for agent (clears fog in render target)
	 * Called every tick by MocCharacter
	 * @param TeamID - Agent's team
	 * @param ViewerLocation - Agent's current position
	 * @param Radius - Vision range
	 */
	UFUNCTION(BlueprintCallable, Category = "FogOfWar")
	void UpdateVision(int32 TeamID, FVector ViewerLocation, float Radius);

	/**
	 * Get render target for team's fog-of-war visualization
	 * @param TeamID - Team to query (0 = Red, 1 = Blue)
	 * @return Render target texture (may be null if not initialized)
	 */
	UFUNCTION(BlueprintPure, Category = "FogOfWar")
	UTextureRenderTarget2D* GetFogTexture(int32 TeamID) const;

	//========================================
	// Episode Management
	//========================================

	/** Reset all fog of war state for new episode */
	UFUNCTION(BlueprintCallable, Category = "FogOfWar|Episode")
	void Reset();

	//========================================
	// Debug Visualization
	//========================================

	/** Draw debug information (enemy positions, resources, vision ranges) */
	void DrawDebugInfo();

protected:
	//========================================
	// Internal Logic
	//========================================

	/** Decay enemy positions older than EnemyMemoryDuration (called in Tick) */
	void DecayEnemyPositions(float DeltaTime);

	/** Get team-specific data structure */
	TMap<AActor*, float>& GetEnemyTimestamps(int32 TeamID);
	const TMap<AActor*, float>& GetEnemyTimestamps(int32 TeamID) const;

	TMap<AActor*, FVector>& GetEnemyPositions(int32 TeamID);
	const TMap<AActor*, FVector>& GetEnemyPositions(int32 TeamID) const;

	TArray<FExploredResource>& GetResourceList(int32 TeamID);
	const TArray<FExploredResource>& GetResourceList(int32 TeamID) const;

public:
	//========================================
	// Configuration
	//========================================

	/** Enemy position memory duration (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|Config")
	float EnemyMemoryDuration = 5.0f;

	/** How often to check for expired enemy positions (seconds) */
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Config")
	float DecayCheckInterval = 0.5f;

	/** Map dimensions (for render target mapping) */
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Visual")
	FVector2D MapSize = FVector2D(15000.0f, 15000.0f); // 150m x 150m

	/** Render target resolution (pixels) */
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Visual")
	int32 RenderTargetResolution = 512;

	/** Show debug visualization */
	UPROPERTY(EditAnywhere, Category = "FogOfWar|Debug")
	bool bShowDebugInfo = false;

	//========================================
	// Visual Components
	//========================================

	/** Red team fog-of-war texture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|Visual")
	UTextureRenderTarget2D* RedTeamFogTexture = nullptr;

	/** Blue team fog-of-war texture */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FogOfWar|Visual")
	UTextureRenderTarget2D* BlueTeamFogTexture = nullptr;

protected:
	//========================================
	// Memory Storage
	//========================================

	/** Enemy last seen timestamps - TeamID -> (Enemy -> Timestamp) */
	UPROPERTY()
	TMap<AActor*, float> RedTeamEnemyTimestamps;

	UPROPERTY()
	TMap<AActor*, float> BlueTeamEnemyTimestamps;

	/** Enemy last known positions - TeamID -> (Enemy -> Position) */
	UPROPERTY()
	TMap<AActor*, FVector> RedTeamEnemyPositions;

	UPROPERTY()
	TMap<AActor*, FVector> BlueTeamEnemyPositions;

	/** Known resources (permanent memory) - TeamID -> ResourceList */
	UPROPERTY()
	TArray<FExploredResource> RedTeamKnownResources;

	UPROPERTY()
	TArray<FExploredResource> BlueTeamKnownResources;

	/** Time since last decay check */
	float TimeSinceDecayCheck = 0.0f;
};
