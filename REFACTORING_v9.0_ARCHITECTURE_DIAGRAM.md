# Team AI System v9.0 - Architecture Diagrams

## System Overview: Before vs After

### BEFORE (v8.20 - Tight Coupling)

```
┌─────────────────────────────────────────────────────────────────┐
│ TeamLeaderComponent (Monolithic)                                 │
│                                                                  │
│ ├─ RegisterFollower()                                           │
│ ├─ UnregisterFollower()                                         │
│ ├─ GetFollowers() ← WRAPPER                                     │
│ ├─ GetAliveFollowers() ← WRAPPER                                │
│ ├─ RegisterEnemy() ← WRAPPER                                    │
│ ├─ UnregisterEnemy() ← WRAPPER                                  │
│ ├─ GetKnownEnemies() ← WRAPPER                                  │
│ ├─ GetFriendlyObjective() ← WRAPPER                             │
│ ├─ GetHostileObjective() ← WRAPPER                              │
│ └─ BuildTeamObservation() ← WRAPPER                             │
│                                                                  │
│ Internal Data:                                                   │
│   - Followers: TArray<AActor*>                                  │
│   - KnownEnemies: TSet<AActor*>                                 │
│   - FriendlyObjective: AObjectiveActor*                         │
│   - HostileObjective: AObjectiveActor*                          │
└─────────────────────────────────────────────────────────────────┘
                              ↑
                              │ Direct calls (tight coupling)
                              │
┌─────────────────────────────────────────────────────────────────┐
│ ObservationBuilderComponent                                     │
│                                                                  │
│ FObservationElement BuildLocalObservation()                     │
│ {                                                                │
│     // BAD: Direct TeamLeader access                            │
│     if (TeamLeader)                                             │
│     {                                                            │
│         for (AActor* Ally : TeamLeader->GetAliveFollowers())    │
│         {                                                        │
│             // Find lowest health ally...                       │
│         }                                                        │
│     }                                                            │
│                                                                  │
│     AObjectiveActor* Friendly = TeamLeader->GetFriendlyObjective();│
│     AObjectiveActor* Hostile = TeamLeader->GetHostileObjective();│
│ }                                                                │
└─────────────────────────────────────────────────────────────────┘

Problems:
❌ ObservationBuilder knows about TeamLeader structure
❌ Pull-based data access (ObservationBuilder queries TeamLeader)
❌ Tight coupling - difficult to test in isolation
❌ SRP violation - ObservationBuilder manages dependencies
```

---

### AFTER (v9.0 Phase 3 - Manager Component Pattern)

```
┌─────────────────────────────────────────────────────────────────┐
│ TeamLeaderComponent (Coordinator)                                │
│                                                                  │
│ Responsibilities:                                                │
│   ├─ ProcessStrategicEvent()                                    │
│   ├─ ApplyStrategyAssignment()                                  │
│   ├─ PushContextToFollower() ← Propagates data                 │
│   └─ Coordinate manager components                              │
│                                                                  │
│ Internal Cache:                                                  │
│   ├─ CurrentTeamObservation: FTeamObservation                   │
│   └─ CurrentAssignments: TMap<AActor*, FStrategyAssignment>    │
│                                                                  │
│ Manager References:                                              │
│   ├─ SquadManager ───────────┐                                 │
│   ├─ IntelManager ───────────┤                                 │
│   └─ StrategicPlanner ───────┤                                 │
└──────────────────────────────┼──────────────────────────────────┘
                               │
                ┌──────────────┼──────────────┐
                │              │              │
                ▼              ▼              ▼
    ┌───────────────┐ ┌───────────────┐ ┌───────────────┐
    │ SquadManager  │ │ IntelManager  │ │ Strategic-    │
    │               │ │               │ │ Planner       │
    │ Data:         │ │ Data:         │ │               │
    │ - Followers   │ │ - KnownEnemies│ │ - MCTS        │
    │               │ │ - Objectives  │ │ - BatchCache  │
    │ Methods:      │ │ - TeamObs     │ │               │
    │ - Register    │ │               │ │ Methods:      │
    │ - GetAlive    │ │ Methods:      │ │ - RunMCTS     │
    │ - GetCount    │ │ - RegisterEnemy│ │ - PollAsync  │
    └───────────────┘ │ - BuildTeamObs│ └───────────────┘
                      └───────────────┘

            ↓ Push-based data flow (Dependency Injection)

┌─────────────────────────────────────────────────────────────────┐
│ FollowerAgentComponent (Coordinator - No Caching)                │
│                                                                  │
│ void UpdateTacticalContext(                                     │
│     AObjectiveActor* Friendly,                                  │
│     AObjectiveActor* Hostile,                                   │
│     const FTeamObservation& TeamObs)                            │
│ {                                                                │
│     // GOOD: Pure propagation (no caching)                      │
│     if (ObservationBuilder)                                     │
│     {                                                            │
│         ObservationBuilder->SetObjectives(Friendly, Hostile);   │
│         ObservationBuilder->UpdateTeamIntel(TeamObs);           │
│     }                                                            │
│ }                                                                │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│ ObservationBuilderComponent (Pure Sensor)                       │
│                                                                  │
│ Cached Data (Injected):                                         │
│   ├─ CachedFriendlyObjective: AObjectiveActor*                 │
│   ├─ CachedHostileObjective: AObjectiveActor*                  │
│   └─ CachedTeamObservation: FTeamObservation                   │
│                                                                  │
│ FObservationElement BuildLocalObservation()                     │
│ {                                                                │
│     // GOOD: Use only cached/injected data                      │
│     if (CachedTeamObservation.FollowerObservations.Num() > 0)  │
│     {                                                            │
│         for (const FObservationElement& AllyObs :               │
│              CachedTeamObservation.FollowerObservations)        │
│         {                                                        │
│             // Find lowest health ally...                       │
│             if (AllyObs.AgentHealth < WorstAllyHealth)          │
│             {                                                    │
│                 WorstAllyHealth = AllyObs.AgentHealth;          │
│                 AllyLocation = AllyObs.Position;                │
│             }                                                    │
│         }                                                        │
│     }                                                            │
│                                                                  │
│     // Use cached objectives (injected via SetObjectives)       │
│     if (CachedFriendlyObjective)                                │
│     {                                                            │
│         Observation.FriendlyObjectiveDistance = ...;            │
│     }                                                            │
│ }                                                                │
│                                                                  │
│ NO external dependencies! Pure function.                        │
└─────────────────────────────────────────────────────────────────┘

Benefits:
✅ ObservationBuilder has zero external dependencies
✅ Push-based data injection (coordinator pushes data down)
✅ Loose coupling - easy to test in isolation
✅ SRP compliance - each component has single responsibility
✅ Clear data ownership and flow
```

---

## Data Flow Diagram: Complete System

### 1. Objective Discovery Flow

```
Step 1: World Scan
─────────────────────────────────────────────────────────────
IntelManager::DiscoverWorldObjectives()
    │
    ├─ Find ObjectiveActors in world
    ├─ Classify by OwnerTeamID
    │   ├─ FriendlyObjective (TeamID == my team)
    │   └─ HostileObjective (SimManager says enemy)
    │
    └─ Broadcast OnObjectivesDiscovered event
         │
         └──────────────────────────┐
                                    ▼
Step 2: Event Handling            TeamLeader::HandleObjectivesDiscovered()
─────────────────────────────────────────────────────────────
    │
    ├─ Get Friendly = IntelManager->GetFriendlyObjective()
    ├─ Get Hostile = IntelManager->GetHostileObjective()
    │
    └─ For each follower in SquadManager->GetAliveFollowers()
         │
         └─ PushContextToFollower(follower, Friendly, Hostile)
              │
              └──────────────────────────┐
                                         ▼
Step 3: Propagation                   FollowerAgent::UpdateTacticalContext()
─────────────────────────────────────────────────────────────
    │
    └─ ObservationBuilder->SetObjectives(Friendly, Hostile)
         │
         ├─ CachedFriendlyObjective = Friendly  ← CACHED HERE
         └─ CachedHostileObjective = Hostile    ← CACHED HERE


Step 4: Usage
─────────────────────────────────────────────────────────────
ObservationBuilder::BuildLocalObservation()
    │
    └─ PopulateObjectiveContext(Observation)
         │
         ├─ Use CachedFriendlyObjective
         │   └─ Calculate distance, direction
         └─ Use CachedHostileObjective
             └─ Calculate distance, direction

         Returns: Observation with 6 objective features
```

---

### 2. Ally Intelligence Flow (Support Strategy)

```
Step 1: Team Observation Building
─────────────────────────────────────────────────────────────
TeamLeader::TickComponent()
    │
    └─ CurrentTeamObservation = BuildTeamObservation()
         │
         └─ IntelManager->BuildTeamObservation(
                 SquadManager->GetFollowers()
            )
              │
              ├─ For each follower:
              │    ├─ Get FollowerAgentComponent
              │    ├─ Build FObservationElement
              │    └─ Append to FollowerObservations[]
              │
              ├─ Calculate team metrics
              │    ├─ AverageTeamHealth
              │    ├─ TeamCentroid
              │    └─ FormationSpread
              │
              └─ Return FTeamObservation
                   │
                   └─ TeamLeader caches as CurrentTeamObservation


Step 2: Broadcast to Followers
─────────────────────────────────────────────────────────────
TeamLeader::BroadcastTacticalContext() [called periodically]
    │
    └─ For each follower in SquadManager->GetAliveFollowers()
         │
         └─ PushContextToFollower(
                 follower,
                 IntelManager->GetFriendlyObjective(),
                 IntelManager->GetHostileObjective(),
                 CurrentTeamObservation  ← INCLUDES ALLY DATA
            )
              │
              └──────────────────────────┐
                                         ▼
Step 3: Propagation                   FollowerAgent::UpdateTacticalContext()
─────────────────────────────────────────────────────────────
    │
    └─ ObservationBuilder->UpdateTeamIntel(TeamObs)
         │
         └─ CachedTeamObservation = TeamObs  ← CACHED HERE
              │
              └─ Contains FollowerObservations[] with:
                   - Position
                   - AgentHealth
                   - (all 56 features per follower)


Step 4: Support Strategy Usage
─────────────────────────────────────────────────────────────
ObservationBuilder::BuildLocalObservation()
    │
    └─ if (CachedTeamObservation.FollowerObservations.Num() > 0)
         │
         ├─ float WorstAllyHealth = 1.0f
         │
         ├─ For each AllyObs in CachedTeamObservation.FollowerObservations
         │    │
         │    ├─ Skip self (compare positions)
         │    │
         │    └─ if (AllyObs.AgentHealth < WorstAllyHealth)
         │         │
         │         ├─ WorstAllyHealth = AllyObs.AgentHealth
         │         └─ AllyLocation = AllyObs.Position
         │
         └─ if (WorstAllyHealth < 0.5) // Ally needs help
              │
              ├─ Observation.bAllyNeedsHelp = true
              ├─ Observation.AllyHealth = WorstAllyHealth
              ├─ Observation.AllyDistance = distance to AllyLocation
              └─ Observation.AllyDirection = direction to AllyLocation

         Returns: Observation with ally context for Support strategy
```

---

## Sequence Diagram: Episode Start Flow

```
Episode Start Event
         │
         ▼
┌────────────────────────────────────────────────────────────────┐
│ SimulationManager::StartEpisode()                              │
│   Broadcasts OnEpisodeStarted(EnvironmentID, EpisodeNumber)   │
└────────────────────────────────────────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────────────┐
│ TeamLeader::OnEpisodeStart(EnvironmentID, EpisodeNumber)      │
│   1. Filter: Check if our environment                          │
│   2. Clear previous episode data                               │
│      - CurrentBatchKey.Empty()                                 │
│      - CurrentAssignments.Empty()                              │
│   3. Ready for new episode                                     │
└────────────────────────────────────────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────────────┐
│ IntelManager::DiscoverWorldObjectives() [delayed 0.3s]        │
│   1. Find all ObjectiveActors                                  │
│   2. Classify as Friendly/Hostile                              │
│   3. Broadcast OnObjectivesDiscovered                          │
└────────────────────────────────────────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────────────┐
│ TeamLeader::HandleObjectivesDiscovered()                       │
│   For each follower:                                           │
│     PushContextToFollower(Friendly, Hostile, TeamObs)          │
└────────────────────────────────────────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────────────┐
│ FollowerAgent::UpdateTacticalContext(Friendly, Hostile, ...)  │
│   1. ObservationBuilder->SetObjectives(Friendly, Hostile)     │
│   2. ObservationBuilder->UpdateTeamIntel(TeamObs)              │
│   → Follower now ready to build observations                  │
└────────────────────────────────────────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────────────┐
│ StrategicPlanner::RunMCTS() [triggered after objectives ready]│
│   1. IntelManager->BuildTeamObservation()                     │
│   2. MCTS generates strategy assignments                       │
│   3. OnPlanReady.Broadcast(Assignments)                        │
└────────────────────────────────────────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────────────┐
│ TeamLeader::OnPlanReady(Assignments)                           │
│   ApplyStrategyAssignment(Assignments)                         │
│     For each assignment:                                       │
│       FollowerAgent->SetStrategyAssignment(Assignment)         │
└────────────────────────────────────────────────────────────────┘
         │
         ▼
    Episode Running
    (Agents execute strategies)
```

---

## Component Dependency Graph

### BEFORE (Circular Dependencies)

```
                    ┌─────────────────┐
                    │  TeamLeader     │
                    │                 │
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
              ▼              ▼              ▼
    ┌─────────────┐ ┌─────────────┐ ┌─────────────┐
    │  Follower   │ │  Observation│ │  Combat     │
    │  Agent      │ │  Builder    │ │  Executor   │
    └──────┬──────┘ └──────┬──────┘ └─────────────┘
           │               │
           └───────┬───────┘
                   │
                   ▼
           ❌ Queries TeamLeader
           (Circular dependency!)
```

### AFTER (Unidirectional Flow)

```
                    ┌─────────────────────────────────┐
                    │  TeamLeader (Coordinator)       │
                    │  ├─ SquadManager                │
                    │  ├─ IntelManager                │
                    │  └─ StrategicPlanner            │
                    └────────┬────────────────────────┘
                             │ Push data down
                             ▼
                    ┌─────────────────┐
                    │  FollowerAgent  │
                    │  (Coordinator)  │
                    └────────┬────────┘
                             │ Propagate
              ┌──────────────┼──────────────┐
              │              │              │
              ▼              ▼              ▼
    ┌─────────────┐ ┌─────────────┐ ┌─────────────┐
    │ TacticalState│ │ Observation │ │ Combat      │
    │              │ │ Builder     │ │ Executor    │
    └─────────────┘ └─────────────┘ └─────────────┘
                             ▲
                             │
                   ✅ Uses only injected data
                   (No external dependencies!)
```

---

## Memory Layout Comparison

### Data Ownership

| Data | BEFORE | AFTER |
|------|--------|-------|
| **Followers Roster** | TeamLeader directly | SquadManager (injected to IntelManager) |
| **Known Enemies** | TeamLeader directly | IntelManager |
| **Objectives** | TeamLeader directly | IntelManager (injected to ObservationBuilder) |
| **Team Observation** | Computed on-demand | Cached in TeamLeader, injected to followers |
| **Ally Context** | Queried from TeamLeader | Cached in ObservationBuilder |

### Cache Hierarchy

```
TeamLeader Level (Strategic):
    ├─ CurrentTeamObservation (updated every tick)
    └─ CurrentAssignments (updated every MCTS cycle)

Follower Level (Tactical):
    └─ [NO CACHE - Pure propagation]

ObservationBuilder Level (Sensor):
    ├─ CachedFriendlyObjective (updated on objective discovery)
    ├─ CachedHostileObjective (updated on objective discovery)
    ├─ CachedTeamObservation (updated every tick via UpdateTeamIntel)
    ├─ CachedCoverLocation (updated every 0.5s)
    └─ LocalObservation (updated every RL inference cycle)
```

---

## Testing Strategy

### Unit Tests (Component Isolation)

```cpp
// ObservationBuilder - Pure Function Testing
TEST(ObservationBuilder, BuildWithInjectedData)
{
    UObservationBuilderComponent* Builder = NewObject<UObservationBuilderComponent>();

    // Setup injected data (no external dependencies!)
    FTeamObservation TeamObs;
    TeamObs.FollowerObservations.Add(CreateMockObservation(0.5f, FVector(100, 0, 0)));
    TeamObs.FollowerObservations.Add(CreateMockObservation(0.3f, FVector(200, 0, 0)));

    Builder->UpdateTeamIntel(TeamObs);
    Builder->SetObjectives(MockFriendlyObj, MockHostileObj);

    // Build observation - purely functional
    FObservationElement Obs = Builder->BuildLocalObservation();

    // Verify ally context uses injected data
    ASSERT_TRUE(Obs.bAllyNeedsHelp);
    ASSERT_FLOAT_EQ(Obs.AllyHealth, 0.3f); // Lowest health ally
}
```

### Integration Tests (Data Flow)

```cpp
TEST(TeamAI, ObjectiveContextPropagation)
{
    // 1. IntelManager discovers objectives
    IntelManager->DiscoverWorldObjectives();
    ASSERT_TRUE(IntelManager->AreObjectivesDiscovered());

    // 2. TeamLeader broadcasts to followers
    TeamLeader->HandleObjectivesDiscovered();

    // 3. Verify follower received data
    FObservationElement Obs = Follower->BuildLocalObservation();
    ASSERT_GT(Obs.FriendlyObjectiveDistance, 0.0f);
    ASSERT_GT(Obs.HostileObjectiveDistance, 0.0f);
}

TEST(TeamAI, AllyIntelPropagation)
{
    // 1. TeamLeader builds team observation
    FTeamObservation TeamObs = TeamLeader->BuildTeamObservation();
    ASSERT_GT(TeamObs.FollowerObservations.Num(), 0);

    // 2. Broadcast to followers
    TeamLeader->BroadcastTacticalContext();

    // 3. Support strategy follower finds lowest health ally
    Follower->SetStrategyAssignment(CreateSupportAssignment());
    FObservationElement Obs = Follower->BuildLocalObservation();

    if (Obs.bAllyNeedsHelp) // Ally below 50% health exists
    {
        ASSERT_GT(Obs.AllyDistance, 0.0f);
        ASSERT_LT(Obs.AllyHealth, 0.5f);
    }
}
```

---

## Conclusion

The v9.0 Phase 3 refactoring achieves a **clean, maintainable architecture** with:

- ✅ **Single Responsibility:** Each component has one clear purpose
- ✅ **Loose Coupling:** Push-based data injection eliminates circular dependencies
- ✅ **Testability:** Pure functions and isolated components
- ✅ **Explicit Data Flow:** Clear ownership and propagation paths
- ✅ **Performance:** Same memory footprint, no additional overhead

**Data flows in ONE direction:** TeamLeader → FollowerAgent → Sub-Components

**No circular dependencies:** Sub-components never query upwards

This design is **scalable, maintainable, and follows industry best practices** for component-based architecture in Unreal Engine.
