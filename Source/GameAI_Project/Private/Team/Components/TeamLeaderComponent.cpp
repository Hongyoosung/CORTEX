#include "Team/Components/TeamLeaderComponent.h"
#include "Team/Components/FollowerAgentComponent.h"
#include "Team/ObjectiveActor.h"  // v8.0: For durability-based objectives
#include "AI/MCTS/MCTS.h"
#include "AI/MCTS/MCTSAsyncTask.h"
#include "Observation/ObservationElement.h"
#include "RL/RLTypes.h"  // v8.0: EStrategyType, FStrategyAssignment
#include "RL/RLPolicyNetwork.h"  // v8.0: For value estimates
#include "Core/SimulationManagerGameMode.h"
#include "Actor/LeaderCharacter.h"
#include "DrawDebugHelpers.h"
#include "Async/Async.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "RL/Components/RewardCalculator.h"
#include "Kismet/GameplayStatics.h"
#include "Combat/Components/HealthComponent.h"
#include "TimerManager.h"

//==============================================================================
// v8.0: TEAM LEADER COMPONENT
// MCTS assigns Strategies directly (not Missions) → RL outputs tactical params → Rules execute
//==============================================================================

UTeamLeaderComponent::UTeamLeaderComponent()
	: AsyncMCTSTask(nullptr)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.5f;  // Update every 0.5s
}

void UTeamLeaderComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeMCTS();

	if (bAutoRegisterWithSimManager)
	{
		ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
		if (SimManager)
		{
			// 리더 등록 시도
			if (SimManager->RegisterTeam(TeamID, this, TeamName, TeamColor))
			{
				bIsRegisteredToManager = true; // 등록 상태 확인
				UE_LOG(LogTemp, Warning, TEXT("✅ TeamLeader '%s': Registered with SimulationManager"), *TeamName);

				// 환경 등록
				int32 EnvironmentID = TeamID / 2;
				SimManager->RegisterTeamEnvironment(TeamID, EnvironmentID);

				// 대기 중이던 팔로워들을 이제 등록 처리
				ProcessPendingRegistrations();
			}
			SimManager->OnEpisodeStarted.AddDynamic(this, &UTeamLeaderComponent::OnEpisodeStart);
			SimManager->OnEpisodeEnded.AddDynamic(this, &UTeamLeaderComponent::OnEpisodeComplete);
		}
	}

	// RACE CONDITION FIX: Delay objective discovery to allow GameMode to setup enemy relationships
	// GameMode Blueprint has a 0.2s delay before calling "Set Mutual Enemies"
	// We delay discovery by 0.3s to ensure enemy setup is complete
	FTimerHandle DelayedDiscoveryTimer;
	GetWorld()->GetTimerManager().SetTimer(
		DelayedDiscoveryTimer,
		[this]()
		{
			DiscoverWorldObjectives();
		},
		0.3f,  // 0.3s delay (after GameMode's 0.2s delay)
		false  // No loop
	);

	UE_LOG(LogTemp, Log, TEXT("TeamLeaderComponent: Initialized team '%s' (objective discovery scheduled for 0.3s delay)"), *TeamName);
}

void UTeamLeaderComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Check if simulation is running
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
	if (SimManager && !SimManager->IsSimulationRunning())
	{
		return;
	}

	// Update team observation (for next decision)
	if (Followers.Num() > 0)
	{
		CurrentTeamObservation = BuildTeamObservation();
	}

	//--------------------------------------------------------------------------
	// CONTINUOUS PLANNING (v3.0 Sprint 6)
	//--------------------------------------------------------------------------
	if (bContinuousPlanning)
	{
		TimeSinceLastPlanning += DeltaTime;

		// Check if we should run proactive planning
		if (TimeSinceLastPlanning >= ContinuousPlanningInterval &&
			!bMCTSRunning &&
			GetAliveFollowers().Num() > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("⏰ [CONTINUOUS PLANNING] '%s': Planning interval reached (%.2fs), triggering MCTS"),
				*TeamName, TimeSinceLastPlanning);

			TimeSinceLastPlanning = 0.0f;

			if (bAsyncMCTS)
			{
				RunStrategyAssignmentAsync();
			}
			else
			{
				RunStrategyAssignment();
			}
		}
	}

	// Check if async MCTS task completed (v8.0)
	if (AsyncMCTSTask != nullptr && AsyncMCTSTask->IsDone())
	{
		// Get results from completed task (v8.0 API)
		TMap<AActor*, FStrategyAssignment> AssignmentMap = AsyncMCTSTask->GetTask().GetResults();
		float ExecutionTime = AsyncMCTSTask->GetTask().GetExecutionTime();

		// Convert map to array for ApplyStrategyAssignment
		TArray<FStrategyAssignment> Assignments;
		for (const auto& Pair : AssignmentMap)
		{
			Assignments.Add(Pair.Value);
		}

		UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v8.0] '%s': Async task completed in %.2fms - %d assignments"),
			*TeamName, ExecutionTime, Assignments.Num());

		// Update performance stats
		MCTSExecutionCount++;
		AverageMCTSExecutionTime = ((AverageMCTSExecutionTime * (MCTSExecutionCount - 1)) + ExecutionTime) / MCTSExecutionCount;

		// Performance warning if exceeding target
		const float TargetTime = 50.0f;
		if (ExecutionTime > TargetTime)
		{
			UE_LOG(LogTemp, Warning, TEXT("⚠️ [PERFORMANCE] '%s': MCTS took %.2fms (exceeds target of %.0fms) - Avg: %.2fms over %d runs"),
				*TeamName, ExecutionTime, TargetTime, AverageMCTSExecutionTime, MCTSExecutionCount);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("✓ [PERFORMANCE] '%s': MCTS took %.2fms (within target) - Avg: %.2fms over %d runs"),
				*TeamName, ExecutionTime, AverageMCTSExecutionTime, MCTSExecutionCount);
		}

		// Delete completed task (FAsyncTask requires manual cleanup)
		delete AsyncMCTSTask;
		AsyncMCTSTask = nullptr;

		// Process results on game thread (v8.0)
		ApplyStrategyAssignment(Assignments);
	}

	// Process pending events (can interrupt if critical and bAllowEventInterrupts=true)
	ProcessPendingEvents();

	// ============================================================================
	// PROXIMITY DIAGNOSIS: Log inter-agent distances every 2 seconds
	// ============================================================================
	TimeSinceLastFormationLog += DeltaTime;
	if (TimeSinceLastFormationLog >= 2.0f)
	{
		TimeSinceLastFormationLog = 0.0f;

		TArray<AActor*> AliveFollowers = GetAliveFollowers();
		if (AliveFollowers.Num() >= 2)
		{
			//UE_LOG(LogTemp, Warning, TEXT("[FORMATION] '%s': Inter-agent distances (%d agents):"), *TeamName, AliveFollowers.Num());

			// Calculate all pairwise distances
			float MinDistance = FLT_MAX;
			float MaxDistance = 0.0f;
			float TotalDistance = 0.0f;
			int32 PairCount = 0;

			for (int32 i = 0; i < AliveFollowers.Num(); ++i)
			{
				AActor* Agent1 = AliveFollowers[i];
				if (!Agent1) continue;

				for (int32 j = i + 1; j < AliveFollowers.Num(); ++j)
				{
					AActor* Agent2 = AliveFollowers[j];
					if (!Agent2) continue;

					float Distance = FVector::Dist(Agent1->GetActorLocation(), Agent2->GetActorLocation());

					/*UE_LOG(LogTemp, Warning, TEXT("[FORMATION]   '%s' <-> '%s': %.1f cm"),
						*Agent1->GetName(),
						*Agent2->GetName(),
						Distance);*/

					MinDistance = FMath::Min(MinDistance, Distance);
					MaxDistance = FMath::Max(MaxDistance, Distance);
					TotalDistance += Distance;
					PairCount++;
				}
			}

			if (PairCount > 0)
			{
				float AvgDistance = TotalDistance / PairCount;
				/*UE_LOG(LogTemp, Warning, TEXT("[FORMATION] '%s': Distance stats - Min: %.1f cm, Max: %.1f cm, Avg: %.1f cm"),
					*TeamName, MinDistance, MaxDistance, AvgDistance);*/
			}
		}
	}

	// Draw debug info if enabled
	if (bEnableDebugDrawing)
	{
		DrawDebugInfo();
	}
}

void UTeamLeaderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up any running async tasks
	if (AsyncMCTSTask != nullptr)
	{
		// FAsyncTask requires manual cleanup
		AsyncMCTSTask->EnsureCompletion();  // Wait for task to finish
		delete AsyncMCTSTask;
		AsyncMCTSTask = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

//------------------------------------------------------------------------------
// INITIALIZATION
//------------------------------------------------------------------------------

void UTeamLeaderComponent::InitializeMCTS()
{
	StrategicMCTS = NewObject<UMCTS>(this);
	if (StrategicMCTS)
	{
		// Initialize MCTS for team-level decisions
		StrategicMCTS->InitializeTeamMCTS(MCTSSimulations, 1.41f);

		// Also set properties directly for compatibility
		StrategicMCTS->MaxSimulations = MCTSSimulations;
		StrategicMCTS->ExplorationParameter = 1.41f;
		StrategicMCTS->DiscountFactor = 0.95f;

		UE_LOG(LogTemp, Log, TEXT("TeamLeaderComponent: MCTS initialized with %d simulations"), MCTSSimulations);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("TeamLeaderComponent: Failed to create MCTS"));
	}
}

void UTeamLeaderComponent::DiscoverWorldObjectives()
{
	if (!GetWorld()) return;

	//==========================================================================
	// v8.0: ObjectiveActor Discovery for Strategy Assignment
	// MCTS now assigns strategies directly to objectives
	//
	// v8.5 MULTI-ENVIRONMENT FIX:
	// In vectorized training (4 environments, 8 teams total), we must filter
	// objectives by environment to prevent cross-environment assignments.
	//
	// Environment mapping: EnvironmentID = TeamID / 2
	//   - Team 0, 1 → Environment 0
	//   - Team 2, 3 → Environment 1
	//   - Team 4, 5 → Environment 2
	//   - Team 6, 7 → Environment 3
	//==========================================================================

	// Calculate which environment this team belongs to
	int32 MyEnvironmentID = TeamID / 2;

	// Get SimulationManager to query enemy relationships
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(
		UGameplayStatics::GetGameMode(GetWorld())
	);

	// Find all ObjectiveActors in the world
	TArray<AActor*> FoundObjectives;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AObjectiveActor::StaticClass(), FoundObjectives);

	UE_LOG(LogTemp, Display, TEXT("[v8.5 DISCOVERY] TeamLeader '%s' (TeamID=%d, Env %d): Scanning %d total objectives..."),
		*TeamName, TeamID, MyEnvironmentID, FoundObjectives.Num());

	// Find friendly and hostile objectives (environment-filtered)
	for (AActor* Actor : FoundObjectives)
	{
		AObjectiveActor* Objective = Cast<AObjectiveActor>(Actor);
		if (!Objective) continue;

		// Calculate objective's environment
		int32 ObjectiveEnvironmentID = Objective->OwnerTeamID / 2;

		// CRITICAL: Skip objectives from different environments
		if (ObjectiveEnvironmentID != MyEnvironmentID)
		{
			UE_LOG(LogTemp, Verbose, TEXT("  ⏭️ Skipping objective '%s' (Team %d, Env %d) - different environment"),
				*Objective->GetName(), Objective->OwnerTeamID, ObjectiveEnvironmentID);
			continue;
		}

		// Check if friendly (same team)
		if (Objective->OwnerTeamID == TeamID)
		{
			FriendlyObjective = Objective;
			UE_LOG(LogTemp, Display, TEXT("✅ [v8.5 DISCOVERY] TeamLeader '%s' (TeamID=%d, Env %d): Found FRIENDLY ObjectiveActor '%s' (Team %d)"),
				*TeamName, TeamID, MyEnvironmentID,
				*FriendlyObjective->GetName(),
				Objective->OwnerTeamID);
		}
		// Check if hostile using SimulationManager's enemy relationship system
		else if (SimManager && SimManager->AreTeamsEnemies(TeamID, Objective->OwnerTeamID))
		{
			HostileObjective = Objective;
			UE_LOG(LogTemp, Display, TEXT("✅ [v8.5 DISCOVERY] TeamLeader '%s' (TeamID=%d, Env %d): Found HOSTILE ObjectiveActor '%s' (Team %d)"),
				*TeamName, TeamID, MyEnvironmentID,
				*HostileObjective->GetName(),
				Objective->OwnerTeamID);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("  ⚠️ Objective '%s' (Team %d, Env %d) is in same environment but not friendly/hostile - neutral?"),
				*Objective->GetName(), Objective->OwnerTeamID, ObjectiveEnvironmentID);
		}
	}

	// Validate discovery
	if (!FriendlyObjective)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ [v8.0 DISCOVERY] TeamLeader '%s' (TeamID=%d): No friendly ObjectiveActor found!"),
			*TeamName, TeamID);
	}

	if (!HostileObjective)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ [v8.0 DISCOVERY] TeamLeader '%s' (TeamID=%d): No hostile ObjectiveActor found!"),
			*TeamName, TeamID);
	}

	if (FriendlyObjective && HostileObjective)
	{
		UE_LOG(LogTemp, Display, TEXT("✅ [v8.0 DISCOVERY] TeamLeader '%s': Ready for strategy assignment (Defend: %s, Assault: %s)"),
			*TeamName,
			*FriendlyObjective->GetName(),
			*HostileObjective->GetName());
	}
}

void UTeamLeaderComponent::ProcessPendingRegistrations()
{
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
	if (!SimManager || PendingFollowerRegistration.Num() == 0) return;

	UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': Processing %d pending followers..."), *TeamName, PendingFollowerRegistration.Num());

	for (AActor* Follower : PendingFollowerRegistration)
	{
		if (Follower)
		{
			SimManager->RegisterTeamMember(TeamID, Follower);
		}
	}

	PendingFollowerRegistration.Empty();
}

//------------------------------------------------------------------------------
// FOLLOWER MANAGEMENT
//------------------------------------------------------------------------------

bool UTeamLeaderComponent::RegisterFollower(AActor* Follower)
{
	if (!Follower) return false;

	if (Followers.Num() >= MaxFollowers) {
		UE_LOG(LogTemp, Error, TEXT("❌ TeamLeader '%s': Max followers reached!"), *TeamName);
		return false;
	}

	if (Followers.Contains(Follower)) return false;

	// 리스트에는 먼저 추가
	Followers.Add(Follower);

	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());

	// 핵심 수정: 리더가 아직 매니저에 등록되지 않았다면 대기열로 보냄
	if (SimManager && bIsRegisteredToManager)
	{
		SimManager->RegisterTeamMember(TeamID, Follower);
		UE_LOG(LogTemp, Log, TEXT("TeamLeader '%s': Registered %s (Immediate)"), *TeamName, *Follower->GetName());
	}
	else
	{
		PendingFollowerRegistration.Add(Follower);
		UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': %s added to PENDING queue (Leader not registered yet)"), *TeamName, *Follower->GetName());
	}

	OnFollowerRegistered.Broadcast(Follower, Followers.Num());
	return true;
}

void UTeamLeaderComponent::UnregisterFollower(AActor* Follower)
{
	if (!Follower) return;

	if (!Followers.Contains(Follower))
	{
		UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': Follower %s not registered"),
			*TeamName, *Follower->GetName());
		return;
	}

	Followers.Remove(Follower);
	CurrentAssignments.Remove(Follower);

	// Unregister from SimulationManager (fix for team ID detection)
	ASimulationManagerGameMode* SimManager = Cast<ASimulationManagerGameMode>(GetWorld()->GetAuthGameMode());
	if (SimManager)
	{
		SimManager->UnregisterTeamMember(TeamID, Follower);
		UE_LOG(LogTemp, Log, TEXT("TeamLeader '%s': Unregistered follower %s from SimulationManager (TeamID: %d, %d remaining)"),
			*TeamName, *Follower->GetName(), TeamID, Followers.Num());
	}

	TotalFollowersLost++;

	// Broadcast event
	OnFollowerUnregistered.Broadcast(Follower, Followers.Num());

	// If all followers dead, trigger critical event
	if (GetAliveFollowers().Num() == 0 && Followers.Num() > 0)
	{
		ProcessStrategicEvent(EStrategicEvent::Custom, nullptr, FVector::ZeroVector, 10);
		UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': All followers eliminated!"), *TeamName);
	}
}


TArray<AActor*> UTeamLeaderComponent::GetAliveFollowers() const
{
	TArray<AActor*> Alive;

	for (AActor* Follower : Followers)
	{
		if (!Follower) continue;

		// Simple alive check - can be extended with health component check
		if (!Follower->IsPendingKillPending())
		{
			Alive.Add(Follower);
		}
	}

	return Alive;
}

//------------------------------------------------------------------------------
// EVENT PROCESSING
//------------------------------------------------------------------------------

void UTeamLeaderComponent::ProcessStrategicEvent(
	EStrategicEvent Event,
	AActor* Instigator,
	FVector Location,
	int32 Priority)
{
	FStrategicEventContext Context;
	Context.EventType = Event;
	Context.Instigator = Instigator;
	Context.Location = Location;
	Context.Priority = Priority;

	ProcessStrategicEventWithContext(Context);
}

void UTeamLeaderComponent::ProcessStrategicEventWithContext(
	const FStrategicEventContext& Context)
{
	FString EventName = UEnum::GetValueAsString(Context.EventType);
	FString InstigatorName = Context.Instigator ? Context.Instigator->GetName() : TEXT("None");

	UE_LOG(LogTemp, Warning, TEXT("[TEAM LEADER] '%s': Received event %s from %s (Priority: %d, Location: %s)"),
		*TeamName,
		*EventName,
		*InstigatorName,
		Context.Priority,
		*Context.Location.ToString());

	// Add to pending queue
	PendingEvents.Add(Context);
	UE_LOG(LogTemp, Display, TEXT("[TEAM LEADER] '%s': Event queued (%d pending events)"),
		*TeamName,
		PendingEvents.Num());

	// Check if we should trigger MCTS immediately
	bool bShouldTrigger = ShouldTriggerMCTS(Context);

	UE_LOG(LogTemp, Warning, TEXT("[TEAM LEADER] '%s': MCTS trigger check: %s (bMCTSRunning=%s, IsCooldown=%s, Priority=%d >= Threshold=%d)"),
		*TeamName,
		bShouldTrigger ? TEXT("YES") : TEXT("NO"),
		bMCTSRunning ? TEXT("true") : TEXT("false"),
		IsMCTSOnCooldown() ? TEXT("true") : TEXT("false"),
		Context.Priority,
		EventPriorityThreshold);

	if (bShouldTrigger)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TEAM LEADER] '%s': TRIGGERING MCTS (Mode: %s)"),
			*TeamName,
			bAsyncMCTS ? TEXT("ASYNC") : TEXT("SYNC"));

		if (bAsyncMCTS)
		{
			RunStrategyAssignmentAsync();
		}
		else
		{
			RunStrategyAssignment();
		}
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[TEAM LEADER] '%s': MCTS not triggered, event will be processed later"),
			*TeamName);
	}

	OnEventProcessed.Broadcast(Context.EventType, bShouldTrigger);
}

bool UTeamLeaderComponent::ShouldTriggerMCTS(const FStrategicEventContext& Context) const
{
	// Don't trigger if MCTS already running
	if (bMCTSRunning)
	{
		UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': MCTS already running, event queued"), *TeamName);
		return false;
	}

	// v3.0 Sprint 6: In continuous planning mode, only interrupt for critical events
	if (bContinuousPlanning)
	{
		if (!bAllowEventInterrupts)
		{
			// Event interrupts disabled, let continuous planning handle it
			UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': Continuous planning mode, event interrupts disabled"), *TeamName);
			return false;
		}

		// Only trigger on CRITICAL events (priority >= 9)
		bool bIsCritical = Context.Priority >= 9;

		// Critical event types
		switch (Context.EventType)
		{
			case EStrategicEvent::AllyKilled:
			case EStrategicEvent::EnemySpotted:
			case EStrategicEvent::EnemyKilled:
			case EStrategicEvent::UnderFire:

				bIsCritical = true;
				break;
			default:
				break;
		}

		if (!bIsCritical)
		{
			UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': Continuous planning mode, non-critical event queued for next cycle"), *TeamName);
			return false;
		}

		UE_LOG(LogTemp, Warning, TEXT("TeamLeader '%s': CRITICAL EVENT interrupting continuous planning"), *TeamName);
		return true;
	}

	// Legacy event-driven mode
	// Don't trigger if on cooldown
	if (IsMCTSOnCooldown())
	{
		UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': MCTS on cooldown, event queued"), *TeamName);
		return false;
	}

	// Trigger if event priority exceeds threshold
	if (Context.Priority >= EventPriorityThreshold)
	{
		UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': Event priority %d >= threshold %d, triggering MCTS"),
			*TeamName, Context.Priority, EventPriorityThreshold);
		return true;
	}

	// High-priority events always trigger
	switch (Context.EventType)
	{
		case EStrategicEvent::AllyKilled:
		case EStrategicEvent::EnemySpotted:

			UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': Critical event type, triggering MCTS"), *TeamName);
			return true;
		default:
			break;
	}

	return false;
}


void UTeamLeaderComponent::OnEpisodeStart(int32 EnvironmentID, int32 EpisodeNumber)
{
	// 1. 내 환경인지 확인 (EnvironmentID = TeamID / 2)
	int32 MyEnvironmentID = TeamID / 2;
	if (EnvironmentID != MyEnvironmentID)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] Env %d | Team %d | Episode %d Started - Triggering Initial MCTS"),
		EnvironmentID, TeamID, EpisodeNumber);

	// 2. 이전 에피소드 데이터 완전 초기화
	CurrentBatchKey.Empty();
	CurrentAssignments.Empty();

	// 3. 목표물 재탐색 (에피소드 리셋 후 객체 상태가 변경되었을 수 있으므로 안전장치)
	if (!FriendlyObjective || !HostileObjective)
	{
		DiscoverWorldObjectives();
	}

	// 4. MCTS 즉시 실행하여 초기 배치 할당 (이것이 없으면 CurrentBatchKey가 비어있게 됨)
	if (bAsyncMCTS)
	{
		RunStrategyAssignmentAsync();
	}
	else
	{
		RunStrategyAssignment();
	}
}

void UTeamLeaderComponent::OnEpisodeComplete(int32 EnvironmentID, const FEpisodeResult& Result)
{
	UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] Episode complete received for Env %d | Team %d"), EnvironmentID, TeamID);
	// 1. [Multi-Env Filter] 이 이벤트가 우리 환경에서 발생한 것인지 확인
	// 가정: EnvironmentID = TeamID / 2 (0,1팀 -> Env0 | 2,3팀 -> Env1 ...)
	int32 MyEnvironmentID = TeamID / 2;

	if (EnvironmentID != MyEnvironmentID)
	{
		// 다른 환경(병렬 훈련 중인 다른 팀들)의 결과이므로 무시
		return;
	}


	// 2. [Result Mapping] 전역 결과를 팀 관점의 결과로 변환
	ETeamEpisodeResult LocalResult = ETeamEpisodeResult::Draw;

	if (Result.WinningTeamID == TeamID)
	{
		LocalResult = ETeamEpisodeResult::Win;
	}
	else if (Result.LosingTeamID == TeamID)
	{
		LocalResult = ETeamEpisodeResult::Loss;
	}
	else
	{
		// 승자도 패자도 아닌 경우 (타임아웃) 혹은 무승부
		// Timeout일 경우 보통 WinningTeamID = -1, LosingTeamID = -1
		LocalResult = ETeamEpisodeResult::Draw;
	}

	// 3. [MCTS Update] 배치 캐시 업데이트
	if (StrategicMCTS)
	{
		if (!CurrentBatchKey.IsEmpty())
		{
			StrategicMCTS->UpdateBatchCache(CurrentAssignments, LocalResult);

			// 로그 출력 (디버깅용)
			FString ResultStr;
			switch (LocalResult)
			{
			case ETeamEpisodeResult::Win: ResultStr = TEXT("WIN 🏆"); break;
			case ETeamEpisodeResult::Loss: ResultStr = TEXT("LOSS ❌"); break;
			case ETeamEpisodeResult::Draw: ResultStr = TEXT("DRAW ➖"); break;
			}

			UE_LOG(LogTemp, Warning, TEXT("[TeamLeader] Env %d | Team %d | Batch '%s' Result: %s"),
				EnvironmentID, TeamID, *CurrentBatchKey, *ResultStr);

			// 4. [Persistence] 캐시 저장 (옵션: 에피소드 10회마다 저장)
			static int32 EpisodeCounter = 0;
			if (++EpisodeCounter % 10 == 0)
			{
				FString CachePath = FPaths::ProjectSavedDir() + TEXT("MCTS/BatchCache.json");
				StrategicMCTS->SaveBatchCache(CachePath);
			}

			UE_LOG(LogTemp, Warning, TEXT("✅ [MCTS Update] Cache updated for batch '%s'"), *CurrentBatchKey);
		}
		else
		{
			// 이 로그가 뜨면 MCTS가 에피소드 중에 한 번도 안 돈 것입니다.
			UE_LOG(LogTemp, Error, TEXT("❌ [MCTS Update Failed] CurrentBatchKey is EMPTY! MCTS did not run this episode."));
		}
	}

	// 5. [Reset] 다음 에피소드를 위해 상태 초기화
	CurrentBatchKey.Empty();
	CurrentAssignments.Empty();
}

void UTeamLeaderComponent::ProcessPendingEvents()
{
	if (PendingEvents.Num() == 0) return;
	if (bMCTSRunning) return;
	if (IsMCTSOnCooldown()) return;

	UE_LOG(LogTemp, Warning, TEXT("TeamLeaderComponent: Tick - Processing strategic decisions for team '%s'"), *TeamName);


	// Sort by priority (highest first)
	PendingEvents.Sort([](const FStrategicEventContext& A, const FStrategicEventContext& B) {
		return A.Priority > B.Priority;
	});

	// Process highest priority event
	FStrategicEventContext TopEvent = PendingEvents[0];
	PendingEvents.RemoveAt(0);

	UE_LOG(LogTemp, Verbose, TEXT("TeamLeader '%s': Processing pending event %d (Priority: %d)"),
		*TeamName, static_cast<int32>(TopEvent.EventType), TopEvent.Priority);

	if (ShouldTriggerMCTS(TopEvent))
	{
		if (bAsyncMCTS)
		{
			RunStrategyAssignmentAsync();
		}
		else
		{
			RunStrategyAssignment();
		}
	}
}

bool UTeamLeaderComponent::IsMCTSOnCooldown() const
{
	float CurrentTime = FPlatformTime::Seconds();
	return (CurrentTime - LastMCTSTime) < MCTSCooldown;
}

//------------------------------------------------------------------------------
// MCTS EXECUTION
//------------------------------------------------------------------------------

FTeamObservation UTeamLeaderComponent::BuildTeamObservation()
{
	// Gather all follower observations
	TArray<AActor*> AliveFollowers = GetAliveFollowers();
	TArray<AActor*> Enemies = GetKnownEnemies();

	// v8.0: Use discovered friendly objective
	AActor* FriendlyObj = FriendlyObjective;

	FTeamObservation TeamObs = FTeamObservation::BuildFromTeam(
		AliveFollowers,
		FriendlyObj,
		Enemies
	);

	return TeamObs;
}


//==============================================================================
// v8.0: STRATEGY ASSIGNMENT (SYNC)
//==============================================================================

void UTeamLeaderComponent::RunStrategyAssignment()
{
	if (bMCTSRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("MCTS already running"));
		return;
	}

	TArray<AActor*> AliveAgents = GetAliveFollowers();
	if (AliveAgents.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("No alive agents"));
		return;
	}

	// [Fix] CachedObservations 수집 로직 추가
	TMap<AActor*, FObservationElement> CachedObservations;
	for (AActor* Agent : AliveAgents)
	{
		if (UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>())
		{
			// 각 팔로워의 현재 관측 정보를 빌드하여 맵에 저장
			CachedObservations.Add(Agent, FollowerComp->BuildLocalObservation());
		}
	}

	// v8.20: Call new batch-level implementation
	// (CachedObservations 변수 사용 가능해짐)
	TMap<AActor*, FStrategyAssignment> Assignments =
		StrategicMCTS->RunStrategyAssignment_v820(
			AliveAgents,
			{ FriendlyObjective, HostileObjective }, // Objective 멤버 변수명은 확인 필요
			MCTSSimulations,
			CachedObservations);

	if (Assignments.Num() != AliveAgents.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("[TeamLeader] Batch assignment incomplete! Got %d, Expected %d"),
			Assignments.Num(), AliveAgents.Num());
		return;
	}

	// Store for end-of-episode cache update
	CurrentBatchKey = StrategicMCTS->GetBatchKey(Assignments);
	CurrentAssignments = Assignments;

	// [Fix] TMap의 값들을 TArray로 변환하여 전달
	TArray<FStrategyAssignment> AssignmentList;
	Assignments.GenerateValueArray(AssignmentList); // Map의 Value들만 추출

	ApplyStrategyAssignment(AssignmentList);
}


//==============================================================================
// v8.0: STRATEGY ASSIGNMENT (ASYNC)
//==============================================================================

void UTeamLeaderComponent::RunStrategyAssignmentAsync()
{
	if (bMCTSRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': MCTS already running"), *TeamName);
		return;
	}

	if (!StrategicMCTS)
	{
		UE_LOG(LogTemp, Error, TEXT("🎯 TeamLeader '%s': Missing MCTS"), *TeamName);
		return;
	}

	TArray<AActor*> AliveAgents = GetAliveFollowers();

	if (AliveAgents.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': No alive agents, skipping MCTS"), *TeamName);
		return;
	}

	if (!FriendlyObjective || !HostileObjective)
	{
		UE_LOG(LogTemp, Warning, TEXT("🎯 TeamLeader '%s': Missing objectives, retrying discovery..."), *TeamName);

		// RACE CONDITION FIX: Enemy team may not have registered yet during BeginPlay
		// Retry objective discovery now that all teams should be registered
		DiscoverWorldObjectives();

		// Check again after retry
		if (!FriendlyObjective || !HostileObjective)
		{
			UE_LOG(LogTemp, Error, TEXT("🎯 TeamLeader '%s': Still missing objectives after retry! Friendly=%s, Hostile=%s"),
				*TeamName,
				FriendlyObjective ? TEXT("OK") : TEXT("NULL"),
				HostileObjective ? TEXT("OK") : TEXT("NULL"));
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("✅ TeamLeader '%s': Objectives found after retry!"), *TeamName);
	}

	bMCTSRunning = true;
	LastMCTSTime = FPlatformTime::Seconds();

	UE_LOG(LogTemp, Warning, TEXT("🎯 [MCTS v8.0] '%s': STARTED (ASYNC) - %d agents"),
		*TeamName,
		AliveAgents.Num());

	// v8.0: Pre-cache observations on game thread for thread-safe async execution
	TMap<AActor*, FObservationElement> CachedObservations;
	for (AActor* Agent : AliveAgents)
	{
		if (!Agent) continue;

		UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>();
		if (FollowerComp)
		{
			// Build observation NOW on the game thread (safe to call GetWorld() here)
			FObservationElement Obs = FollowerComp->BuildLocalObservation();
			CachedObservations.Add(Agent, Obs);
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("🎯 [MCTS v8.0] '%s': Cached %d observations for async execution"),
		*TeamName,
		CachedObservations.Num());

	// Build objectives array (v8.0)
	TArray<AObjectiveActor*> Objectives;
	if (FriendlyObjective) Objectives.Add(FriendlyObjective);
	if (HostileObjective) Objectives.Add(HostileObjective);

	// Create async task (v8.0 API)
	AsyncMCTSTask = new FAsyncTask<FMCTSAsyncTask>(
		StrategicMCTS,
		AliveAgents,
		Objectives,
		MCTSSimulations,
		CachedObservations // Pass cached observations for thread safety
	);

	// Start background execution
	AsyncMCTSTask->StartBackgroundTask();

	UE_LOG(LogTemp, Verbose, TEXT("🎯 [MCTS v8.0] '%s': Async task started, will poll for completion in Tick"),
		*TeamName);
}


//==============================================================================
// v8.0: APPLY STRATEGY ASSIGNMENT
//==============================================================================

void UTeamLeaderComponent::ApplyStrategyAssignment(const TArray<FStrategyAssignment>& Assignments)
{
	UE_LOG(LogTemp, Warning, TEXT("🎯 [ASSIGNMENT v8.0] '%s': Applying %d strategy assignments"),
		*TeamName,
		Assignments.Num());

	// Log strategy summary
	TMap<EStrategyType, int32> StrategyCounts;
	for (const FStrategyAssignment& Assignment : Assignments)
	{
		StrategyCounts.FindOrAdd(Assignment.Strategy, 0)++;
	}

	UE_LOG(LogTemp, Display, TEXT("🎯 [ASSIGNMENT v8.0] '%s': Strategy breakdown:"),
		*TeamName);
	for (const auto& CountPair : StrategyCounts)
	{
		UE_LOG(LogTemp, Display, TEXT("   - %s: %d agents"),
			*UEnum::GetValueAsString(CountPair.Key),
			CountPair.Value);
	}

	// Apply assignments to followers
	for (const FStrategyAssignment& Assignment : Assignments)
	{
		AActor* Agent = Assignment.Agent;

		if (!Agent)
		{
			UE_LOG(LogTemp, Error, TEXT("🎯 [ASSIGNMENT v8.0] Skipping invalid assignment: Agent=NULL"));
			continue;
		}

		// Update current assignments
		CurrentAssignments.Add(Agent, Assignment);

		// Notify follower
		UFollowerAgentComponent* FollowerComp = Agent->FindComponentByClass<UFollowerAgentComponent>();
		if (FollowerComp)
		{
			FollowerComp->SetStrategyAssignment(Assignment);

			// v8.0: Enhanced logging
			FString ObjectiveName = Assignment.TargetObjective ? Assignment.TargetObjective->GetName() : TEXT("None");

			UE_LOG(LogTemp, Warning, TEXT("🎯 [ASSIGNMENT v8.0] Agent '%s' → Strategy '%s' (Objective=%s, Priority=%d, Value=%.2f, Visits=%d)"),
				*Agent->GetName(),
				*UEnum::GetValueAsString(Assignment.Strategy),
				*ObjectiveName,
				Assignment.Priority,
				Assignment.ExpectedValue,
				Assignment.VisitCount);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("🎯 [ASSIGNMENT v8.0] Agent '%s' has no FollowerAgentComponent!"),
				*Agent->GetName());
		}
	}

	// Broadcast event (v8.0)
	FStrategyAssignmentMap AssignmentMap;
	AssignmentMap.Assignments = Assignments;
	OnStrategicDecisionMade.Broadcast(AssignmentMap);

	bMCTSRunning = false;

	UE_LOG(LogTemp, Warning, TEXT("🎯 [ASSIGNMENT v8.0] '%s': All assignments complete"),
		*TeamName);
}


//------------------------------------------------------------------------------
// ENEMY TRACKING
//------------------------------------------------------------------------------

void UTeamLeaderComponent::RegisterEnemy(AActor* Enemy)
{
	if (!Enemy) return;

	// Filter out Leader characters - they should not be registered as enemies
	if (Enemy->IsA<ALeaderCharacter>())
	{
		return;
	}

	if (!KnownEnemies.Contains(Enemy))
	{
		KnownEnemies.Add(Enemy);
		UE_LOG(LogTemp, Warning, TEXT("[TEAM LEADER] '%s': Registered NEW enemy: %s (Total enemies: %d)"),
			*TeamName, *Enemy->GetName(), KnownEnemies.Num());
	}
}

void UTeamLeaderComponent::UnregisterEnemy(AActor* Enemy)
{
	if (!Enemy) return;

	if (KnownEnemies.Contains(Enemy))
	{
		KnownEnemies.Remove(Enemy);
		TotalEnemiesEliminated++;

		UE_LOG(LogTemp, Log, TEXT("TeamLeader '%s': Enemy %s eliminated (Remaining: %d)"),
			*TeamName, *Enemy->GetName(), KnownEnemies.Num());

		// Trigger event
		ProcessStrategicEvent(EStrategicEvent::EnemyEliminated, Enemy, Enemy->GetActorLocation(), 6);
	}
}

TArray<AActor*> UTeamLeaderComponent::GetKnownEnemies() const
{
	return KnownEnemies.Array();
}

void UTeamLeaderComponent::ClearKnownEnemies()
{
	int32 ClearedCount = KnownEnemies.Num();
	KnownEnemies.Empty();

	UE_LOG(LogTemp, Warning, TEXT("[TEAM LEADER] '%s': Cleared %d known enemies (episode reset)"),
		*TeamName, ClearedCount);
}

//------------------------------------------------------------------------------
// METRICS
//------------------------------------------------------------------------------

FTeamMetrics UTeamLeaderComponent::GetTeamMetrics() const
{
	FTeamMetrics Metrics;

	Metrics.TotalFollowers = Followers.Num();
	Metrics.AliveFollowers = GetAliveFollowers().Num();
	Metrics.AverageHealth = CurrentTeamObservation.AverageTeamHealth;
	Metrics.EnemiesEliminated = TotalEnemiesEliminated;
	Metrics.FollowersLost = TotalFollowersLost;
	Metrics.CommandsIssued = TotalCommandsIssued;

	// Calculate K/D ratio
	if (TotalFollowersLost > 0)
	{
		Metrics.KillDeathRatio = static_cast<float>(TotalEnemiesEliminated) / static_cast<float>(TotalFollowersLost);
	}
	else
	{
		Metrics.KillDeathRatio = static_cast<float>(TotalEnemiesEliminated);
	}

	return Metrics;
}

//------------------------------------------------------------------------------
// DEBUG VISUALIZATION (v6.0 Phase 13)
//------------------------------------------------------------------------------

void UTeamLeaderComponent::DrawDebugInfo()
{
	if (!GetOwner()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	FVector LeaderPos = GetOwner()->GetActorLocation();

	// Draw team centroid
	if (CurrentTeamObservation.AliveFollowers > 0)
	{
		DrawDebugSphere(World, CurrentTeamObservation.TeamCentroid, 100.0f, 12,
			TeamColor.ToFColor(true), false, 0.5f, 0, 3.0f);
	}

	// ============================================
	// v8.0: MCTS Strategy Assignment Visualization
	// ============================================
	if (bEnableDebugDrawing)
	{
		for (const auto& Pair : CurrentAssignments)
		{
			AActor* Agent = Pair.Key;
			const FStrategyAssignment& Assignment = Pair.Value;

			if (!Agent) continue;

			FVector AgentPos = Agent->GetActorLocation();

			// Get objective position
			FVector ObjectivePos = FVector::ZeroVector;
			if (Assignment.TargetObjective && IsValid(Assignment.TargetObjective))
			{
				ObjectivePos = Assignment.TargetObjective->GetActorLocation();
			}

			// Draw MCTS assignment arrow (yellow arrow: Agent → Objective)
			if (!ObjectivePos.IsZero())
			{
				DrawDebugDirectionalArrow(
					World,
					AgentPos,
					ObjectivePos,
					100.0f,  // Arrow size
					FColor::Yellow,
					false, -1.0f, 0, 3.0f  // Thickness
				);
			}

			// Draw strategy and value estimate (green text above agent)
			FString InfoText = FString::Printf(TEXT("%s | V=%.2f"),
				*UEnum::GetValueAsString(Assignment.Strategy),
				Assignment.ExpectedValue);

			DrawDebugString(
				World,
				AgentPos + FVector(0, 0, 150),
				InfoText,
				nullptr,
				FColor::Green,
				-1.0f,
				true  // Draw shadow
			);

			// Draw objective type (cyan text above objective)
			if (Assignment.TargetObjective && !ObjectivePos.IsZero())
			{
				FString ObjectiveText = Assignment.TargetObjective->GetName();
				DrawDebugString(
					World,
					ObjectivePos + FVector(0, 0, 100),
					ObjectiveText,
					nullptr,
					FColor::Cyan,
					-1.0f,
					true  // Draw shadow
				);
			}
		}
	}
	// ============================================
	// End v8.0 MCTS Visualization
	// ============================================

	// Draw lines to each follower (legacy)
	for (AActor* Follower : GetAliveFollowers())
	{
		if (!Follower) continue;

		FVector FollowerPos = Follower->GetActorLocation();
		DrawDebugLine(World, LeaderPos, FollowerPos, TeamColor.ToFColor(true), false, -1.0f, 0, 2.0f);

		// Draw strategy type above follower (v8.0 legacy - redundant with v8.0 visualization)
		if (!bEnableDebugDrawing)
		{
			if (const FStrategyAssignment* AssignmentPtr = CurrentAssignments.Find(Follower))
			{
				FString StrategyText = UEnum::GetValueAsString(AssignmentPtr->Strategy);
				// Use 0.0f duration to prevent overlapping text from multiple frames
				DrawDebugString(World, FollowerPos + FVector(0, 0, 180), StrategyText, nullptr, FColor::White, -1.0f, true);
			}
		}
	}

	// Draw enemy indicators
	for (AActor* Enemy : KnownEnemies)
	{
		if (!Enemy) continue;

		FVector EnemyPos = Enemy->GetActorLocation();
		DrawDebugSphere(World, EnemyPos, 50.0f, 8, FColor::Red, false, 0.5f);
	}

	// Draw team info
	FString TeamInfo = FString::Printf(TEXT("%s\nFollowers: %d/%d\nHealth: %.1f%%\nEnemies: %d"),
		*TeamName,
		GetAliveFollowers().Num(),
		Followers.Num(),
		CurrentTeamObservation.AverageTeamHealth,
		KnownEnemies.Num());

	DrawDebugString(World, LeaderPos + FVector(0, 0, 200), TeamInfo, nullptr, TeamColor.ToFColor(true), -1.0f, true);
}