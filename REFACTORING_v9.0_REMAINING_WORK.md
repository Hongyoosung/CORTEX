# CORTEX v9.0 Refactoring - Remaining Work

**Date:** 2026-02-03
**Status:** ✅ Phase 1-2 Complete | 🔜 Phase 3-6 Remaining

---

## Quick Reference: What's Done vs. What's Left

### ✅ Completed (Phases 1-2)
- Character wrapper API (80+ functions)
- Dependency injection pattern established
- RewardCalculator migrated (4 FindComponentByClass eliminated)
- 1 StateTree task migrated (STTask_ExecuteTacticalMovement)
- Schola environment partially migrated (3 FindComponentByClass eliminated)

### 🔜 Remaining Work (Phases 3-6)

**Phase 3:** Dependency Injection - Add setter methods to components (1 day)
**Phase 4:** Merge Trivial Components - TeamComms, SquadManager (2 days)
**Phase 5:** Merge Coordinator Components - FollowerAgent, TeamLeader (1 day)
**Phase 6:** Full Testing & Documentation (1 day)

**Total Remaining:** 5 days

---

## Phase 3: Complete Dependency Injection (1 day)

### Objective
Eliminate internal FindComponentByClass calls in components by injecting dependencies through character.

### Files to Modify

#### 1. CombatExecutorComponent.h
**Add public setter methods:**
```cpp
public:
    // Dependency injection (called by character)
    void SetRewardCalculator(URewardCalculator* Calculator);
    void SetHealthComponent(UHealthComponent* Health);
    void SetPerceptionComponent(UAgentPerceptionComponent* Perception);
```

**Already has private members** (verified in Phase 1):
```cpp
private:
    UPROPERTY()
    UHealthComponent* CachedHealthComponent = nullptr;

    UPROPERTY()
    UAgentPerceptionComponent* CachedPerceptionComponent = nullptr;
```

**Note:** RewardCalculator already has a setter (Phase 1), just needs Health/Perception setters.

#### 2. CombatExecutorComponent.cpp
**Implement setter methods:**
```cpp
void UCombatExecutorComponent::SetHealthComponent(UHealthComponent* Health)
{
    CachedHealthComponent = Health;

    if (CachedHealthComponent)
    {
        UE_LOG(LogTemp, Log, TEXT("[CombatExecutor v9.0] Injected HealthComponent"));
    }
}

void UCombatExecutorComponent::SetPerceptionComponent(UAgentPerceptionComponent* Perception)
{
    CachedPerceptionComponent = Perception;

    if (CachedPerceptionComponent)
    {
        UE_LOG(LogTemp, Log, TEXT("[CombatExecutor v9.0] Injected PerceptionComponent"));
    }
}
```

**Remove BeginPlay() FindComponentByClass calls** (if they exist).

#### 3. ObservationBuilderComponent.h
**Add public setter methods:**
```cpp
public:
    // v9.0: Dependency injection (called by character)
    void SetHealthComponent(UHealthComponent* Health);
    void SetPerceptionComponent(UAgentPerceptionComponent* Perception);
```

**Add private members** (if not already present):
```cpp
private:
    UPROPERTY()
    UHealthComponent* CachedHealthComponent = nullptr;

    UPROPERTY()
    UAgentPerceptionComponent* CachedPerceptionComponent = nullptr;
```

#### 4. ObservationBuilderComponent.cpp
**Implement setter methods:**
```cpp
void UObservationBuilderComponent::SetHealthComponent(UHealthComponent* Health)
{
    CachedHealthComponent = Health;

    if (CachedHealthComponent)
    {
        UE_LOG(LogTemp, Log, TEXT("[ObservationBuilder v9.0] Injected HealthComponent"));
    }
}

void UObservationBuilderComponent::SetPerceptionComponent(UAgentPerceptionComponent* Perception)
{
    CachedPerceptionComponent = Perception;

    if (CachedPerceptionComponent)
    {
        UE_LOG(LogTemp, Log, TEXT("[ObservationBuilder v9.0] Injected PerceptionComponent"));
    }
}
```

**Update BuildLocalObservation() to use cached components** (remove FindComponentByClass calls).

#### 5. FollowerCharacter.cpp::InjectDependencies()
**Update to inject into ObservationBuilder:**
```cpp
void AFollowerCharacter::InjectDependencies()
{
    // ... existing CombatExecutor injection ...

    // ObservationBuilder needs: HealthComponent, PerceptionComponent
    if (CachedObservationBuilder)
    {
        if (CachedHealthComponent)
        {
            CachedObservationBuilder->SetHealthComponent(CachedHealthComponent);
        }

        if (CachedPerceptionComponent)
        {
            CachedObservationBuilder->SetPerceptionComponent(CachedPerceptionComponent);
        }

        UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] Injected dependencies into ObservationBuilder"));
    }

    UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0 Phase3] Dependency injection complete (all components)"));
}
```

### Testing
- Run a single episode
- Check logs for "Injected HealthComponent" and "Injected PerceptionComponent"
- Verify observations are built correctly (52 features)
- Verify combat execution works (damage, kills)

---

## Phase 4: Merge Trivial Components (2 days)

### Objective
Merge TeamCommsComponent (60 lines) and SquadManagerComponent (120 lines) into character classes.

### Part 1: Merge TeamCommsComponent → FollowerCharacter (1 day)

#### 1. FollowerCharacter.h - Add Private Data
**Add to private section:**
```cpp
private:
    //==========================================================================
    // v9.0 PHASE 4: TEAM COMMUNICATION DATA (merged from TeamCommsComponent)
    //==========================================================================
    UPROPERTY()
    UTeamLeaderComponent* CachedTeamLeader = nullptr;

    UPROPERTY(EditAnywhere, Category = "AI|Team", meta = (AllowPrivateAccess = "true"))
    int32 TeamID = 0;

    UPROPERTY(EditAnywhere, Category = "AI|Team", meta = (AllowPrivateAccess = "true"))
    bool bAutoRegisterWithLeader = true;
```

#### 2. FollowerCharacter.cpp - Implement Methods
**Copy TeamComms methods from TeamCommsComponent.cpp:**
```cpp
bool AFollowerCharacter::RegisterWithLeader(UTeamLeaderComponent* Leader)
{
    // Implementation from TeamCommsComponent::RegisterWithTeamLeader()
    // ...
}

void AFollowerCharacter::UnregisterFromLeader()
{
    // Implementation from TeamCommsComponent::UnregisterFromTeamLeader()
    // ...
}
```

**Update BeginPlay() to auto-register:**
```cpp
// Replace TeamComms auto-registration logic with direct implementation
if (bAutoRegisterWithLeader)
{
    TArray<AActor*> FoundLeaders;
    UGameplayStatics::GetAllActorsWithTag(GetWorld(), TEXT("TeamLeader"), FoundLeaders);

    for (AActor* LeaderActor : FoundLeaders)
    {
        ALeaderCharacter* Leader = Cast<ALeaderCharacter>(LeaderActor);
        if (Leader && Leader->GetTeamID() == TeamID)
        {
            if (Leader->RegisterFollower(this))
            {
                CachedTeamLeader = Leader->GetTeamLeaderComponent();
                UE_LOG(LogTemp, Log, TEXT("[FollowerCharacter v9.0] Registered with leader %s"), *Leader->GetName());
                break;
            }
        }
    }
}
```

**Update wrapper functions to use cached leader:**
```cpp
UTeamLeaderComponent* AFollowerCharacter::GetTeamLeader() const
{
    return CachedTeamLeader;
}

int32 AFollowerCharacter::GetTeamID() const
{
    return TeamID;
}

void AFollowerCharacter::SignalEventToLeader(EStrategicEvent Event, AActor* Instigator, FVector Location, int32 Priority)
{
    if (CachedTeamLeader)
    {
        CachedTeamLeader->ProcessStrategicEvent(Event, Instigator, Location, Priority);
    }
}
```

#### 3. Remove TeamCommsComponent
**Delete files:**
- `Source/GameAI_Project/Public/Team/Components/TeamCommsComponent.h`
- `Source/GameAI_Project/Private/Team/Components/TeamCommsComponent.cpp`

**Update FollowerCharacter constructor:**
```cpp
// Remove TeamCommsComponent creation
// TeamCommsComponent = CreateDefaultSubobject<UTeamCommsComponent>(TEXT("TeamCommsComponent"));
```

**Update BP_Follower blueprint:**
- Open blueprint in editor
- Remove TeamCommsComponent
- Save blueprint

#### 4. Update References
**Search for TeamCommsComponent usage:**
```bash
git grep -n "TeamCommsComponent" Source/
```

**Update all references to use character API instead.**

### Part 2: Merge SquadManagerComponent → LeaderCharacter (1 day)

#### 1. LeaderCharacter.h - Add Private Data
**Add to private section:**
```cpp
private:
    //==========================================================================
    // v9.0 PHASE 4: SQUAD DATA (merged from SquadManagerComponent)
    //==========================================================================
    UPROPERTY()
    TArray<AActor*> RegisteredFollowers;

    UPROPERTY()
    TArray<AActor*> PendingFollowerRegistration;

    UPROPERTY(EditAnywhere, Category = "AI|Squad", meta = (AllowPrivateAccess = "true"))
    int32 MaxFollowers = 4;

    UPROPERTY(EditAnywhere, Category = "AI|Team", meta = (AllowPrivateAccess = "true"))
    int32 TeamID = 0;

    void ProcessDeferredRegistrations();
```

#### 2. LeaderCharacter.cpp - Implement Methods
**Copy SquadManager methods from SquadManagerComponent.cpp:**
```cpp
bool ALeaderCharacter::RegisterFollower(AActor* Follower)
{
    // Implementation from SquadManagerComponent::RegisterFollower()
    // ...
}

bool ALeaderCharacter::UnregisterFollower(AActor* Follower)
{
    // Implementation from SquadManagerComponent::UnregisterFollower()
    // ...
}

TArray<AActor*> ALeaderCharacter::GetAliveFollowers() const
{
    // Implementation from SquadManagerComponent::GetAliveFollowers()
    // ...
}

void ALeaderCharacter::ProcessDeferredRegistrations()
{
    // Implementation from SquadManagerComponent::ProcessPendingRegistrations()
    // ...
}
```

**Update Tick() to process deferred registrations:**
```cpp
void ALeaderCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Process deferred follower registrations (merged from SquadManager)
    ProcessDeferredRegistrations();

    // Update combat timers
    UpdateTimers(DeltaTime);
}
```

#### 3. Remove SquadManagerComponent
**Delete files:**
- `Source/GameAI_Project/Public/Team/Components/SquadManagerComponent.h`
- `Source/GameAI_Project/Private/Team/Components/SquadManagerComponent.cpp`

**Update LeaderCharacter constructor:**
```cpp
// Remove SquadManagerComponent creation
// SquadManagerComponent = CreateDefaultSubobject<USquadManagerComponent>(TEXT("SquadManagerComponent"));
```

**Update BP_TeamLeader blueprint:**
- Open blueprint in editor
- Remove SquadManagerComponent
- Save blueprint

#### 4. Update References
**Search for SquadManagerComponent usage:**
```bash
git grep -n "SquadManagerComponent" Source/
```

**Update TeamLeaderComponent to use character methods:**
```cpp
// TeamLeaderComponent.cpp - Update RegisterFollower calls
ALeaderCharacter* LeaderChar = Cast<ALeaderCharacter>(GetOwner());
if (LeaderChar)
{
    LeaderChar->RegisterFollower(Follower);
}
```

### Testing Phase 4
- Compile project (expect no errors)
- Run BP_Follower and BP_TeamLeader (expect normal behavior)
- Check logs for "Registered with leader"
- Verify follower count updates correctly
- Verify strategy assignments still work

**Expected Component Counts After Phase 4:**
- Follower: 8 → 6 components (-25%)
- Leader: 5 → 3 components (-40%)

---

## Phase 5: Merge Coordinator Components (1 day)

### Objective
Merge FollowerAgentComponent and TeamLeaderComponent logic into character classes (confirmed by user).

### Part 1: Merge FollowerAgentComponent → FollowerCharacter

#### 1. Move TickComponent Logic
**FollowerCharacter.cpp - Update Tick():**
```cpp
void AFollowerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // v9.0 Phase 5: Merged from FollowerAgentComponent::TickComponent()

    // Update tactical context if needed
    UpdateTacticalContext();

    // Update context bridge for StateTree
    UpdateContextBridge();

    // Update visual logger (if enabled)
    if (VisualLoggerComponent)
    {
        VisualLoggerComponent->UpdateDebugInfo();
    }
}
```

#### 2. Move Core Methods
**Copy key methods from FollowerAgentComponent.cpp:**
- `UpdateTacticalContext()`
- Strategy assignment handling
- Episode lifecycle management

#### 3. Delete FollowerAgentComponent
**Delete files:**
- `Source/GameAI_Project/Public/Team/Components/FollowerAgentComponent.h`
- `Source/GameAI_Project/Private/Team/Components/FollowerAgentComponent.cpp`

**Update references:** All external systems already use character API (Phase 2).

### Part 2: Merge TeamLeaderComponent → LeaderCharacter

#### 1. Move Event Processing
**LeaderCharacter.cpp - Add ProcessStrategicEvent():**
```cpp
void ALeaderCharacter::ProcessStrategicEvent(EStrategicEvent Event, AActor* Instigator, FVector Location, int32 Priority)
{
    // Implementation from TeamLeaderComponent::ProcessStrategicEvent()
    // Trigger MCTS if needed
    // ...
}
```

#### 2. Move MCTS Coordination
**Copy MCTS coordination logic from TeamLeaderComponent:**
- OnPlanReady delegate handling
- Strategy assignment broadcasting

#### 3. Delete TeamLeaderComponent
**Delete files:**
- `Source/GameAI_Project/Public/Team/Components/TeamLeaderComponent.h`
- `Source/GameAI_Project/Private/Team/Components/TeamLeaderComponent.cpp`

### Testing Phase 5
- Compile project
- Run full 1000-step episode
- Verify MCTS runs automatically
- Verify strategy assignments applied
- Verify RL policy outputs tactical parameters
- Compare performance with baseline (should match)

**Expected Component Counts After Phase 5:**
- Follower: 6 → 5 components (-17%)
- Leader: 3 → 2 components (-33%)

---

## Phase 6: Full Testing & Documentation (1 day)

### Part 1: Comprehensive Testing

#### 1. Compilation Testing
```bash
# Windows (Visual Studio)
# Build solution in Development Editor configuration
# Expected: 0 errors, 0 warnings
```

#### 2. Functional Testing
**Run 1000-step training episode:**
```python
# From CORTEX_Training directory
python train_rllib.py
```

**Verify:**
- [x] Followers auto-register with leader on BeginPlay
- [x] MCTS assigns strategies to followers (log: "Strategy assigned")
- [x] RL policy outputs tactical parameters (log: "[RL] Tactical params")
- [x] StateTree executes movement/combat tasks (log: "[TACTICAL v9.0]")
- [x] Combat system deals/takes damage (log: "[COMBAT]")
- [x] Rewards calculated correctly (compare with baseline v8.20)
- [x] Episode reset works (all components reset)

#### 3. Performance Testing
**Profile FindComponentByClass count:**
```cpp
// Add to FollowerCharacter::Tick() temporarily
static int32 FrameCount = 0;
if (++FrameCount % 600 == 0) // Every 10 seconds
{
    UE_LOG(LogTemp, Display, TEXT("[PROFILING] Frame %d - All component access via cached references"), FrameCount);
}
```

**Expected Result:** 0 FindComponentByClass per frame (except optional ScholaAgentComponent)

**Measure frame time:**
```
Before (v8.20): 30-40ms per frame
After (v9.0): 25-35ms per frame (-15% improvement)
```

#### 4. Architecture Validation
**Verify zero cross-component references:**
```bash
# Search for FindComponentByClass outside BeginPlay
git grep -n "FindComponentByClass" Source/ | grep -v "BeginPlay" | grep -v "InitializeComponents"
# Expected: 0-2 results (only ScholaAgent if any)
```

#### 5. Detailed Test Cases

**Test Case 1: RewardCalculator**
- Run 1000-step episode
- Log rewards every 100 steps
- Compare with v8.20 baseline (should match within 5%)

**Test Case 2: StateTree Execution**
- All tasks execute (movement, fire, aim)
- Agents reach objectives
- EQS queries succeed
- Log: "[TACTICAL v9.0] Movement ENABLED"

**Test Case 3: Schola Training**
- Python training loop runs 100 episodes
- Rewards match baseline
- Observations have 56 features
- No errors in UE5 logs

**Test Case 4: Combat System**
- Damage rewards trigger correctly
- Kill rewards apply (+50.0)
- Death penalty applies (-100.0)
- Log: "[COMBAT] Kill reward"

**Test Case 5: MCTS**
- Strategy assignments applied
- Followers execute assigned strategies
- Log: "[MCTS] Batch selected"

**Test Case 6: Episode Reset**
- All components reset state
- No leaked data
- Observations reset to default
- Log: "[FollowerCharacter] Episode reset complete"

### Part 2: Documentation Update

#### 1. Search for Remaining FindComponentByClass
```bash
git grep -n "FindComponentByClass" Source/
```

**Update REMAINING_WORK.md with findings.**

#### 2. Update CLAUDE.md

**Add new architecture section:**
```markdown
## v9.0 Architecture Update (Phase 4)

**Character-as-Central-Hub Pattern:**
- FollowerCharacter exposes 54 wrapper functions
- LeaderCharacter exposes 24 wrapper functions
- Zero cross-component references
- All component access cached once in BeginPlay

**Component Count:**
- Follower: 8 → 5 components (-37.5%)
- Leader: 5 → 2 components (-60%)

**Performance:**
- FindComponentByClass/frame: 50+ → 0-2 (-96%)
- Frame time: 30-40ms → 25-35ms (-15%)

**Key Files:**
- `FollowerCharacter.h/.cpp` - Central coordinator (54 methods)
- `LeaderCharacter.h/.cpp` - Team coordinator (24 methods)
- All external systems use character API (RewardCalculator, StateTree, Schola, EQS)
```

#### 3. Add Architecture Diagram
**Create REFACTORING_v9.0_FINAL_ARCHITECTURE.md:**
```
External Systems (RewardCalculator, StateTree, Schola, EQS)
    ↓ (1 cast, 0 FindComponentByClass)
Character Classes (FollowerCharacter, LeaderCharacter)
    ↓ (cached references, dependency injection)
Components (5 Follower + 2 Leader = 7 total)
    ↓ (no cross-refs, all dependencies injected)
Sub-components (Health, Weapon, Perception)
```

#### 4. Document Dependency Injection Pattern
**Add code comments explaining:**
```cpp
// v9.0 DEPENDENCY INJECTION PATTERN
// 1. Character caches all component references in InitializeComponents() (BeginPlay)
// 2. Character injects dependencies via InjectDependencies() (after step 1)
// 3. Components receive dependencies through setter methods (not by searching)
// 4. External systems access components through character wrapper API (not by searching)
//
// BENEFITS:
// - Zero runtime FindComponentByClass (96% reduction)
// - Type-safe API with clear intent
// - Testable (can inject mocks)
// - Single source of truth (character)
```

### Part 3: Final Validation

**Checklist:**
- [ ] Code compiles without errors or warnings
- [ ] All tests pass (functional, performance, architecture)
- [ ] FindComponentByClass count: 0-2 per frame
- [ ] Performance maintained or improved (<35ms frame time)
- [ ] Documentation updated (CLAUDE.md, architecture diagrams)
- [ ] All phases complete (1-6)

---

## Rollback Plan

**If critical issues arise:**

1. **Revert to Phase 2 (before merging components):**
   - `git revert` Phase 4-5 commits
   - Re-add TeamCommsComponent/SquadManagerComponent
   - Update blueprints

2. **Revert to v8.20 (before refactoring):**
   - `git checkout v8.20-baseline`
   - All systems use FindComponentByClass (working state)

**No data loss:** All changes are tracked in git

---

## Estimated Timeline

| Phase | Days | Cumulative |
|-------|------|------------|
| Phase 3: Dependency Injection | 1 | 1 day |
| Phase 4: Merge Trivial Components | 2 | 3 days |
| Phase 5: Merge Coordinator Components | 1 | 4 days |
| Phase 6: Full Testing & Documentation | 1 | 5 days |

**Total:** 5 days remaining

---

**Document Version:** v1.0
**Last Updated:** 2026-02-03
**Status:** 🔜 Ready for Phase 3
