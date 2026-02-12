// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/MocGameMode.h"
#include "Team/TeamManager.h"
#include "Actors/PickupBase.h"
#include "Actors/AmmoCrate.h"
#include "Characters/MocCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

AMocGameMode::AMocGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f; // Tick every frame for precise timing

	// Initialize default pickup locations
	// Health Packs (12 total) - Distributed across map
	HealthPackLocations = {
		FVector(-5000.0f, 2000.0f, 100.0f),   // Near Red base
		FVector(-5000.0f, -2000.0f, 100.0f),  // Near Red base
		FVector(-2500.0f, 3000.0f, 100.0f),   // North sector
		FVector(-2500.0f, -3000.0f, 100.0f),  // South sector
		FVector(0.0f, 3000.0f, 100.0f),       // North center
		FVector(0.0f, -3000.0f, 100.0f),      // South center
		FVector(2500.0f, 3000.0f, 100.0f),    // North sector
		FVector(2500.0f, -3000.0f, 100.0f),   // South sector
		FVector(5000.0f, 2000.0f, 100.0f),    // Near Blue base
		FVector(5000.0f, -2000.0f, 100.0f),   // Near Blue base
		FVector(-3500.0f, 0.0f, 100.0f),      // West corridor
		FVector(3500.0f, 0.0f, 100.0f)        // East corridor
	};

	// Ammo Crates (8 total) - Strategic positions
	AmmoCrateLocations = {
		FVector(-6000.0f, 0.0f, 100.0f),      // Red base
		FVector(-3500.0f, 4000.0f, 100.0f),   // Point B
		FVector(-1750.0f, 2000.0f, 100.0f),   // Between B and C
		FVector(0.0f, 0.0f, 100.0f),          // Point C (center)
		FVector(1750.0f, -2000.0f, 100.0f),   // Between C and D
		FVector(3500.0f, -4000.0f, 100.0f),   // Point D
		FVector(6000.0f, 0.0f, 100.0f),       // Blue base
		FVector(0.0f, 4500.0f, 100.0f)        // Far north
	};
}

void AMocGameMode::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] BeginPlay - Initializing MOC v10.1 match"));

	// Initialize all entities (find existing or spawn new)
	InitializeTeamManager();
	InitializeCapturePoints();
	InitializeHealthPacks();
	InitializeAmmoCrates();

	// Subscribe to events
	SubscribeToEvents();

	// Auto-start match if enabled
	if (bAutoStartMatch)
	{
		StartMatch();
	}

	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Initialization complete - Ready to start"));
}

void AMocGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Only tick during active match
	if (CurrentMatchState != EMocMatchState::InProgress)
	{
		return;
	}

	// Update match timer
	MatchTimer += DeltaTime;

	// Update passive income
	UpdatePassiveIncome(DeltaTime);

	// Check win conditions
	CheckWinConditions();

	// Debug visualization
	if (bShowDebugInfo)
	{
		FVector DebugLocation = FVector(0.0f, 0.0f, 500.0f);
		FString DebugString = FString::Printf(TEXT("Match Time: %.1f / %.1f\nRed Score: %d | Blue Score: %d"),
			MatchTimer, MaxMatchDuration, GetTeamScore(0), GetTeamScore(1));
		DrawDebugString(GetWorld(), DebugLocation, DebugString, nullptr, FColor::White, 0.0f, true, 1.5f);
	}
}

//========================================
// Match Management
//========================================

void AMocGameMode::StartMatch()
{
	if (CurrentMatchState != EMocMatchState::WaitingToStart)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MocGameMode] Cannot start match - already started"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Starting match"));

	// Reset timer
	MatchTimer = 0.0f;
	PassiveIncomeAccumulator = 0.0f;

	// Set state
	CurrentMatchState = EMocMatchState::InProgress;
	OnMatchStateChanged.Broadcast(CurrentMatchState);

	// Spawn teams
	if (TeamManager)
	{
		TeamManager->SpawnTeams();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MocGameMode] Cannot spawn teams - TeamManager is null"));
	}
}

void AMocGameMode::EndMatch(EMocMatchState WinnerState)
{
	if (bMatchEnded)
	{
		return;
	}

	bMatchEnded = true;
	CurrentMatchState = WinnerState;

	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Match ended - State: %d"), static_cast<int32>(WinnerState));

	// Broadcast match end
	OnMatchStateChanged.Broadcast(CurrentMatchState);

	// Determine winner message
	FString WinnerMessage;
	switch (WinnerState)
	{
	case EMocMatchState::RedTeamWon:
		WinnerMessage = TEXT("Red Team Wins!");
		break;
	case EMocMatchState::BlueTeamWon:
		WinnerMessage = TEXT("Blue Team Wins!");
		break;
	case EMocMatchState::TimeExpired:
		{
			int32 RedScore = GetTeamScore(0);
			int32 BlueScore = GetTeamScore(1);
			if (RedScore > BlueScore)
			{
				WinnerMessage = FString::Printf(TEXT("Time Expired - Red Team Wins! (%d vs %d)"), RedScore, BlueScore);
			}
			else if (BlueScore > RedScore)
			{
				WinnerMessage = FString::Printf(TEXT("Time Expired - Blue Team Wins! (%d vs %d)"), BlueScore, RedScore);
			}
			else
			{
				WinnerMessage = FString::Printf(TEXT("Time Expired - Draw! (%d vs %d)"), RedScore, BlueScore);
			}
		}
		break;
	default:
		WinnerMessage = TEXT("Match Ended");
		break;
	}

	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] %s"), *WinnerMessage);
}

void AMocGameMode::ResetMatch()
{
	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Resetting match for new episode"));

	// 1. Reset match state (GameMode's responsibility)
	MatchTimer = 0.0f;
	PassiveIncomeAccumulator = 0.0f;
	bMatchEnded = false;
	CurrentMatchState = EMocMatchState::InProgress;

	// 2. Delegate to TeamManager (team-level reset)
	if (TeamManager)
	{
		TeamManager->ResetTeams();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MocGameMode] Cannot reset - TeamManager is null"));
	}

	// 3. Reset capture points
	for (auto& Pair : CapturePointsMap)
	{
		if (ACapturePoint* Point = Pair.Value)
		{
			Point->ResetPoint();
		}
	}

	// 4. Reset pickups
	for (APickupBase* Pickup : HealthPacks)
	{
		if (Pickup)
		{
			Pickup->Reset();
		}
	}
	for (APickupBase* Pickup : AmmoCrates)
	{
		if (Pickup)
		{
			Pickup->Reset();
		}
	}


	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Match reset complete"));
}

//========================================
// Scoring System
//========================================

void AMocGameMode::AddTeamScore(int32 TeamID, int32 Amount, const FString& Reason)
{
	if (!TeamManager)
	{
		return;
	}

	// Validate team ID
	if (TeamID != 0 && TeamID != 1)
	{
		return;
	}

	// Add score via TeamManager
	TeamManager->AddTeamScore(TeamID, Amount);

	// Broadcast score update
	int32 NewScore = GetTeamScore(TeamID);
	OnScoreUpdated.Broadcast(TeamID, NewScore, Reason);

	// Log score change
	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Team %d scored %d points (%s) - New total: %d"),
		TeamID, Amount, *Reason, NewScore);
}

int32 AMocGameMode::GetTeamScore(int32 TeamID) const
{
	if (!TeamManager)
	{
		return 0;
	}

	return TeamManager->GetTeamScore(TeamID);
}

//========================================
// Entity Access
//========================================

const ACapturePoint* AMocGameMode::GetCapturePoint(ECapturePointID PointID) const
{
	const ACapturePoint* const* FoundPoint = CapturePointsMap.Find(PointID);
	return FoundPoint ? *FoundPoint : nullptr;
}

TArray<ACapturePoint*> AMocGameMode::GetAllCapturePoints() const
{
	TArray<ACapturePoint*> Points;
	CapturePointsMap.GenerateValueArray(Points);
	return Points;
}

//========================================
// Initialization
//========================================

void AMocGameMode::InitializeTeamManager()
{
	// First, try to find existing TeamManager in the level
	if (bUsePlacedActors)
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATeamManager::StaticClass(), FoundActors);

		if (FoundActors.Num() > 0)
		{
			TeamManager = Cast<ATeamManager>(FoundActors[0]);
			if (TeamManager)
			{
				UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Found existing TeamManager in level"));
				return;
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("[MocGameMode] No TeamManager found in level, spawning new one"));
	}

	// Spawn new TeamManager if not found or bUsePlacedActors is false
	if (!TeamManagerClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[MocGameMode] TeamManagerClass is not set"));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(TEXT("TeamManager"));
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	TeamManager = GetWorld()->SpawnActor<ATeamManager>(TeamManagerClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	if (TeamManager)
	{
		UE_LOG(LogTemp, Log, TEXT("[MocGameMode] TeamManager spawned successfully"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[MocGameMode] Failed to spawn TeamManager"));
	}
}

void AMocGameMode::InitializeCapturePoints()
{
	// First, try to find existing capture points in the level
	if (bUsePlacedActors)
	{
		FindPlacedCapturePoints();

		// If we found all 5 points, we're done
		if (CapturePointsMap.Num() == 5)
		{
			UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Using %d pre-placed capture points from level"), CapturePointsMap.Num());
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("[MocGameMode] Only found %d capture points in level (need 5), spawning missing points"), CapturePointsMap.Num());
	}

	// Spawn missing capture points or all if bUsePlacedActors is false
	if (!CapturePointClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[MocGameMode] CapturePointClass is not set"));
		return;
	}

	// Spawn Point A (Red Base) if not found
	if (!CapturePointsMap.Contains(ECapturePointID::PointA))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = FName(TEXT("CapturePoint_A"));
		ACapturePoint* Point = GetWorld()->SpawnActor<ACapturePoint>(CapturePointClass, PointA_Location, FRotator::ZeroRotator, SpawnParams);
		if (Point)
		{
			Point->PointID = ECapturePointID::PointA;
			Point->InitialOwner = ECapturePointOwnership::RedTeam;
			Point->SetOwnership(ECapturePointOwnership::RedTeam);
			CapturePointsMap.Add(ECapturePointID::PointA, Point);
		}
	}

	// Spawn Point B (North Outpost) if not found
	if (!CapturePointsMap.Contains(ECapturePointID::PointB))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = FName(TEXT("CapturePoint_B"));
		ACapturePoint* Point = GetWorld()->SpawnActor<ACapturePoint>(CapturePointClass, PointB_Location, FRotator::ZeroRotator, SpawnParams);
		if (Point)
		{
			Point->PointID = ECapturePointID::PointB;
			Point->InitialOwner = ECapturePointOwnership::Neutral;
			CapturePointsMap.Add(ECapturePointID::PointB, Point);
		}
	}

	// Spawn Point C (Center Plaza) if not found
	if (!CapturePointsMap.Contains(ECapturePointID::PointC))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = FName(TEXT("CapturePoint_C"));
		ACapturePoint* Point = GetWorld()->SpawnActor<ACapturePoint>(CapturePointClass, PointC_Location, FRotator::ZeroRotator, SpawnParams);
		if (Point)
		{
			Point->PointID = ECapturePointID::PointC;
			Point->InitialOwner = ECapturePointOwnership::Neutral;
			CapturePointsMap.Add(ECapturePointID::PointC, Point);
		}
	}

	// Spawn Point D (South Outpost) if not found
	if (!CapturePointsMap.Contains(ECapturePointID::PointD))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = FName(TEXT("CapturePoint_D"));
		ACapturePoint* Point = GetWorld()->SpawnActor<ACapturePoint>(CapturePointClass, PointD_Location, FRotator::ZeroRotator, SpawnParams);
		if (Point)
		{
			Point->PointID = ECapturePointID::PointD;
			Point->InitialOwner = ECapturePointOwnership::Neutral;
			CapturePointsMap.Add(ECapturePointID::PointD, Point);
		}
	}

	// Spawn Point E (Blue Base) if not found
	if (!CapturePointsMap.Contains(ECapturePointID::PointE))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = FName(TEXT("CapturePoint_E"));
		ACapturePoint* Point = GetWorld()->SpawnActor<ACapturePoint>(CapturePointClass, PointE_Location, FRotator::ZeroRotator, SpawnParams);
		if (Point)
		{
			Point->PointID = ECapturePointID::PointE;
			Point->InitialOwner = ECapturePointOwnership::BlueTeam;
			Point->SetOwnership(ECapturePointOwnership::BlueTeam);
			CapturePointsMap.Add(ECapturePointID::PointE, Point);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Initialized %d capture points (placed + spawned)"), CapturePointsMap.Num());
}

void AMocGameMode::InitializeHealthPacks()
{
	// First, try to find existing health packs in the level
	if (bUsePlacedActors)
	{
		FindPlacedHealthPacks();

		// If we found health packs, use them
		if (HealthPacks.Num() > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Using %d pre-placed health packs from level"), HealthPacks.Num());
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("[MocGameMode] No health packs found in level, spawning default set"));
	}

	// Spawn health packs if not found or bUsePlacedActors is false
	if (!HealthPackClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[MocGameMode] HealthPackClass is not set"));
		return;
	}

	for (int32 i = 0; i < HealthPackLocations.Num(); ++i)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = FName(*FString::Printf(TEXT("HealthPack_%d"), i));
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		APickupBase* HealthPack = GetWorld()->SpawnActor<APickupBase>(HealthPackClass, HealthPackLocations[i], FRotator::ZeroRotator, SpawnParams);
		if (HealthPack)
		{
			HealthPacks.Add(HealthPack);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Spawned %d health packs"), HealthPacks.Num());
}

void AMocGameMode::InitializeAmmoCrates()
{
	// First, try to find existing ammo crates in the level
	if (bUsePlacedActors)
	{
		FindPlacedAmmoCrates();

		// If we found ammo crates, use them
		if (AmmoCrates.Num() > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Using %d pre-placed ammo crates from level"), AmmoCrates.Num());
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("[MocGameMode] No ammo crates found in level, spawning default set"));
	}

	// Spawn ammo crates if not found or bUsePlacedActors is false
	if (!AmmoCrateClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[MocGameMode] AmmoCrateClass is not set"));
		return;
	}

	for (int32 i = 0; i < AmmoCrateLocations.Num(); ++i)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = FName(*FString::Printf(TEXT("AmmoCrate_%d"), i));
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		APickupBase* AmmoCrate = GetWorld()->SpawnActor<APickupBase>(AmmoCrateClass, AmmoCrateLocations[i], FRotator::ZeroRotator, SpawnParams);
		if (AmmoCrate)
		{
			AmmoCrates.Add(AmmoCrate);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Spawned %d ammo crates"), AmmoCrates.Num());
}

void AMocGameMode::SubscribeToEvents()
{
	// Subscribe to CapturePoint events
	for (const auto& Pair : CapturePointsMap)
	{
		ACapturePoint* Point = Pair.Value;
		if (Point)
		{
			Point->OnPointCaptured.AddDynamic(this, &AMocGameMode::OnPointCaptured);
		}
	}

	// Subscribe to TeamManager events
	if (TeamManager)
	{
		TeamManager->OnAgentKilled.AddDynamic(this, &AMocGameMode::OnAgentKilled);
	}

	// Subscribe to Pickup events
	for (APickupBase* HealthPack : HealthPacks)
	{
		if (HealthPack)
		{
			HealthPack->OnPickupCollected.AddDynamic(this, &AMocGameMode::OnPickupCollected);
		}
	}

	for (APickupBase* AmmoCrate : AmmoCrates)
	{
		if (AmmoCrate)
		{
			AmmoCrate->OnPickupCollected.AddDynamic(this, &AMocGameMode::OnPickupCollected);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Subscribed to all game events"));
}

//========================================
// Event Handlers
//========================================

void AMocGameMode::OnPointCaptured(ECapturePointID PointID, ECapturePointOwnership PreviousOwner, ECapturePointOwnership NewOwner)
{
	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Point %d captured - Previous: %d, New: %d"),
		static_cast<int32>(PointID), static_cast<int32>(PreviousOwner), static_cast<int32>(NewOwner));

	// Award points to new owner
	if (NewOwner == ECapturePointOwnership::RedTeam)
	{
		AddTeamScore(0, CaptureReward, TEXT("Point Capture"));
	}
	else if (NewOwner == ECapturePointOwnership::BlueTeam)
	{
		AddTeamScore(1, CaptureReward, TEXT("Point Capture"));
	}
}

void AMocGameMode::OnAgentKilled(int32 VictimTeamID, int32 KillerTeamID, AMocCharacter* Victim)
{
	UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Agent killed - Victim Team: %d, Killer Team: %d"), VictimTeamID, KillerTeamID);

	// Award points to killer team (if different from victim)
	if (KillerTeamID != -1 && KillerTeamID != VictimTeamID)
	{
		AddTeamScore(KillerTeamID, KillPoints, TEXT("Kill"));
	}
}

void AMocGameMode::OnPickupCollected(AActor* Collector, EPickupType PickupType)
{
	// Log pickup for analytics (no scoring for pickups)
	FString PickupName = (PickupType == EPickupType::Health) ? TEXT("Health Pack") : TEXT("Ammo Crate");
	UE_LOG(LogTemp, Verbose, TEXT("[MocGameMode] Pickup collected - Type: %s"), *PickupName);
}

//========================================
// Game Logic
//========================================

void AMocGameMode::UpdatePassiveIncome(float DeltaTime)
{
	// Count owned points per team
	int32 RedOwned = 0;
	int32 BlueOwned = 0;
	CountOwnedPoints(RedOwned, BlueOwned);

	// Calculate passive income
	PassiveIncomeAccumulator += PassiveIncomeRate * DeltaTime;

	// Award whole points when accumulated >= 1.0
	if (PassiveIncomeAccumulator >= 1.0f)
	{
		int32 Points = FMath::FloorToInt(PassiveIncomeAccumulator);
		PassiveIncomeAccumulator -= Points;

		// Award points based on owned capture points
		if (RedOwned > 0)
		{
			AddTeamScore(0, RedOwned * Points, TEXT("Passive Income"));
		}

		if (BlueOwned > 0)
		{
			AddTeamScore(1, BlueOwned * Points, TEXT("Passive Income"));
		}
	}
}

void AMocGameMode::CheckWinConditions()
{
	// Check score victory
	int32 RedScore = GetTeamScore(0);
	int32 BlueScore = GetTeamScore(1);

	if (RedScore >= WinningScore)
	{
		EndMatch(EMocMatchState::RedTeamWon);
		return;
	}

	if (BlueScore >= WinningScore)
	{
		EndMatch(EMocMatchState::BlueTeamWon);
		return;
	}

	// Check time limit
	if (MatchTimer >= MaxMatchDuration)
	{
		EndMatch(EMocMatchState::TimeExpired);
	}
}

void AMocGameMode::CountOwnedPoints(int32& OutRedOwned, int32& OutBlueOwned) const
{
	OutRedOwned = 0;
	OutBlueOwned = 0;

	for (const auto& Pair : CapturePointsMap)
	{
		ACapturePoint* Point = Pair.Value;
		if (Point)
		{
			ECapturePointOwnership Ownership = Point->GetOwnership();
			if (Ownership == ECapturePointOwnership::RedTeam)
			{
				OutRedOwned++;
			}
			else if (Ownership == ECapturePointOwnership::BlueTeam)
			{
				OutBlueOwned++;
			}
		}
	}
}

//========================================
// Helper Functions
//========================================

void AMocGameMode::FindPlacedCapturePoints()
{
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ACapturePoint::StaticClass(), FoundActors);

	for (AActor* Actor : FoundActors)
	{
		ACapturePoint* Point = Cast<ACapturePoint>(Actor);
		if (Point && !CapturePointsMap.Contains(Point->PointID))
		{
			// Register the capture point
			CapturePointsMap.Add(Point->PointID, Point);

			// Set initial ownership if specified in the level
			if (Point->InitialOwner != ECapturePointOwnership::Neutral)
			{
				Point->SetOwnership(Point->InitialOwner);
			}

			UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Found placed capture point: %s (ID: %d)"),
				*Point->GetName(), static_cast<int32>(Point->PointID));
		}
	}
}

void AMocGameMode::FindPlacedHealthPacks()
{
	// Try to find actors based on HealthPackClass if set
	if (HealthPackClass)
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), HealthPackClass, FoundActors);

		for (AActor* Actor : FoundActors)
		{
			APickupBase* HealthPack = Cast<APickupBase>(Actor);
			if (HealthPack && HealthPack->GetPickupType() == EPickupType::Health)
			{
				HealthPacks.Add(HealthPack);
				UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Found placed health pack: %s"), *HealthPack->GetName());
			}
		}
	}

	// If HealthPackClass is not set or no instances found, try searching by base class
	if (HealthPacks.Num() == 0)
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), APickupBase::StaticClass(), FoundActors);

		for (AActor* Actor : FoundActors)
		{
			APickupBase* Pickup = Cast<APickupBase>(Actor);
			if (Pickup && Pickup->GetPickupType() == EPickupType::Health)
			{
				HealthPacks.Add(Pickup);
				UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Found placed health pack: %s"), *Pickup->GetName());
			}
		}
	}
}

void AMocGameMode::FindPlacedAmmoCrates()
{
	// Try to find actors based on AmmoCrateClass if set
	if (AmmoCrateClass)
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AmmoCrateClass, FoundActors);

		for (AActor* Actor : FoundActors)
		{
			APickupBase* AmmoCrate = Cast<APickupBase>(Actor);
			if (AmmoCrate && AmmoCrate->GetPickupType() == EPickupType::Ammo)
			{
				AmmoCrates.Add(AmmoCrate);
				UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Found placed ammo crate: %s"), *AmmoCrate->GetName());
			}
		}
	}

	// If AmmoCrateClass is not set or no instances found, try searching by base class
	if (AmmoCrates.Num() == 0)
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), APickupBase::StaticClass(), FoundActors);

		for (AActor* Actor : FoundActors)
		{
			APickupBase* Pickup = Cast<APickupBase>(Actor);
			if (Pickup && Pickup->GetPickupType() == EPickupType::Ammo)
			{
				AmmoCrates.Add(Pickup);
				UE_LOG(LogTemp, Log, TEXT("[MocGameMode] Found placed ammo crate: %s"), *Pickup->GetName());
			}
		}
	}
}
