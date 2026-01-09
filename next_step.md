# CORTEX v7.0 - Next Steps & Implementation Guide

**Version:** v7.0 (Durability-Based Objectives)
**Date:** 2026-01-09
**Status:** Objective System Implemented ✅

---

## Completed Tasks (v7.0)

### ✅ 1. Created AObjectiveActor Class with Durability System
**Files:** `Source/GameAI_Project/Public/Team/ObjectiveActor.h`, `Source/GameAI_Project/Private/Team/ObjectiveActor.cpp`

**Implementation:**
- Durability system (0-100%, declines with hostile agents)
- Capture volume (spherical trigger, 1000cm radius)
- Team ownership (OwnerTeamID determines friend vs foe)
- Visual feedback (team colors, durability-based emission)
- Recovery mechanics (1.0 durability/sec when empty)
- Debug visualization (durability %, hostile count)

**Key Features:**
```cpp
// Core mechanics
float DamagePerAgentPerSecond = 2.0f;    // Decline rate per hostile
float RecoveryPerSecond = 1.0f;          // Recovery when empty
int32 OwnerTeamID;                       // Team ownership (0 or 1)

// Components
UStaticMeshComponent* PillarMesh;        // Visual representation
USphereComponent* CaptureVolume;         // Trigger for tracking agents

// Helper methods
bool IsFriendlyTo(int32 TeamID);         // Check team ownership
bool IsHostileTo(int32 TeamID);          // Check hostile
bool IsAgentInVolume(AActor* Agent);     // Check presence in zone
```

---

### ✅ 2. Implemented Capture Volume Overlap Handlers
**Implementation:**
- Auto-tracks hostile agents via `OnComponentBeginOverlap` / `EndOverlap`
- Maintains `TSet<AActor*> HostileAgentsInVolume` for efficient counting
- Only hostile agents contribute to durability decline
- Friendly agents are ignored (no friendly fire on objectives)

**Mechanics:**
```cpp
// Decline: Multiple hostiles = faster decline
Reduction = HostileAgentsInVolume.Num() * 2.0f per second

// Example: 3 hostiles in volume = 6.0 durability/sec decline
// 100% durability → 0% in ~16.6 seconds with 3 hostiles
```

---

### ✅ 3. Updated RewardCalculator with Volume-Based Rewards
**File:** `Source/GameAI_Project/Private/RL/RewardCalculator.cpp:214-343`

**Removed:**
- Distance-based progress tracking
- Type-specific branching (Capture vs Defend)
- Ambiguous "moving closer" rewards

**Added:**
```cpp
// Defense Role (Friendly Objective)
if (AgentInFriendlyVolume)
{
    Reward += 0.05f;  // +0.05 per step (~0.5/sec at 10Hz)
}

if (KilledEnemy && AgentInFriendlyVolume)
{
    Reward += 5.0f;   // Defense kill bonus
}

// Assault Role (Hostile Objective)
if (AgentInEnemyVolume)
{
    Reward += 0.1f;   // +0.1 per step (~1.0/sec at 10Hz)
}

if (EnemyObjective.IsDefeated())
{
    Reward += 100.0f; // Base destruction reward
}
```

**Benefits:**
- Clear incentives: "Stay in zone" is unambiguous
- No distance ambiguity: Binary inside/outside check
- Encourages commitment: Must enter dangerous zones to progress
- Symmetric design: Both teams use identical reward structure

---

### ✅ 4. Removed EObjectiveType::Capture from Objective.h
**File:** `Source/GameAI_Project/Public/Team/Objective.h:10-25`

**Changed:**
```cpp
// OLD - v6.0
enum class EObjectiveType : uint8
{
    Capture,  // ❌ REMOVED in v7.0
    Defend,
    Support,
    Retreat
};

// NEW - v7.0
enum class EObjectiveType : uint8
{
    // Capture removed - replaced by ObjectiveActor.OwnerTeamID comparison
    Defend,   // Strategic intent: Hold position
    Assault,  // Strategic intent: Push forward
    Support,  // Strategic intent: Protect ally
    Retreat   // Strategic intent: Disengage
};
```

**All References Updated:**
- `FollowerAgentComponent.cpp:240` - Capture → Assault
- `MCTS.cpp:391` - Capture → Assault
- `RewardCalculator.cpp:430` - Capture → Assault in OnObjectiveComplete

---

### ✅ 5. Deprecated STCondition_CheckObjectiveType Files
**Files:**
- `Source/GameAI_Project/Public/StateTree/Conditions/STCondition_CheckObjectiveType.h`
- `Source/GameAI_Project/Private/StateTree/Conditions/STCondition_CheckObjectiveType.cpp`
- `Source/GameAI_Project/Private/StateTree/FollowerStateTreeSchema.cpp`

**Status:** Deprecated (marked with warnings, commented out in schema)

**Replacement:**
```cpp
// OLD - Type-based checking
if (Objective->Type == EObjectiveType::Capture) { ... }

// NEW - Team ownership checking
if (ObjectiveActor->IsHostileTo(Agent->TeamID)) { ... }
```

---

### ✅ 6. Added ObjectiveManager Integration
**File:** `Source/GameAI_Project/Public/Team/ObjectiveManager.h:94-414`

**New Methods:**
```cpp
// Find all objective actors in the world
TArray<AObjectiveActor*> FindAllObjectiveActors() const;

// Find friendly objective (OwnerTeamID matches team)
AObjectiveActor* FindFriendlyObjective(int32 TeamID) const;

// Find hostile objective (OwnerTeamID doesn't match)
AObjectiveActor* FindHostileObjective(int32 TeamID) const;
```

**Usage Example:**
```cpp
// In TeamLeaderComponent::RunObjectiveAssignment()
UObjectiveManager* ObjMgr = GetObjectiveManager();

AObjectiveActor* FriendlyBase = ObjMgr->FindFriendlyObjective(TeamID);
AObjectiveActor* EnemyBase = ObjMgr->FindHostileObjective(TeamID);

// MCTS explores assignments:
// - All agents → EnemyBase (full assault)
// - 2 → EnemyBase, 2 → FriendlyBase (balanced)
// - 3 → EnemyBase, 1 → FriendlyBase (aggressive)
```

---

### ✅ 7. Updated All Type References
**Files Modified:**
- `RL/RewardCalculator.cpp` - Added `#include "Team/ObjectiveActor.h"`
- `RL/RewardCalculator.cpp:346-361` - Updated CalculateAlignmentBonus (Capture → Assault)
- `AI/MCTS/MCTS.cpp:391` - Updated health heuristic (Capture → Assault)
- `Team/FollowerAgentComponent.cpp:237-242` - Updated strategy mapping

---

## Objective System Overview (v7.0)

### Architecture Philosophy

**Before (Type-Based):**
- Explicit Defend/Capture type distinction
- Distance-based rewards (ambiguous progress)
- Type-checking branching logic everywhere
- 150+ lines of type-specific code

**After (Team-Based):**
- Unified ObjectiveActor with team ownership
- Volume-based rewards (clear incentives)
- Simple `IsFriendlyTo(TeamID)` checks
- Symmetric gameplay (defend yours, capture theirs)
- Clear win condition (durability = 0)

### Physical World Representation

```
Level Layout:
┌─────────────────────────────────────────────────────┐
│                                                     │
│   [Team 0 Base]                  [Team 1 Base]     │
│   OwnerTeamID=0                  OwnerTeamID=1     │
│   Durability: 100%               Durability: 100%  │
│   ┌─────────┐                    ┌─────────┐       │
│   │ Pillar  │                    │ Pillar  │       │
│   │  BLUE   │                    │   RED   │       │
│   └─────────┘                    └─────────┘       │
│      ○ ○ ○                          ○ ○ ○          │
│   (10m radius)                   (10m radius)      │
│   Capture Zone                   Capture Zone      │
│                                                     │
└─────────────────────────────────────────────────────┘

Team 0 Perspective:
- Left base = Friendly (Defend role)
- Right base = Hostile (Assault role)

Team 1 Perspective:
- Right base = Friendly (Defend role)
- Left base = Hostile (Assault role)
```

### Durability Mechanics Flow

```
Initial State:
┌──────────────────────────────────┐
│ Durability: 100%                 │
│ Hostiles: 0                      │
│ Status: Healthy                  │
└──────────────────────────────────┘
                ↓
Enemy Enters Volume:
┌──────────────────────────────────┐
│ Durability: 100% → Declining     │
│ Hostiles: 1 (2.0/sec decline)    │
│ Status: Under Attack             │
└──────────────────────────────────┘
                ↓
Multiple Hostiles:
┌──────────────────────────────────┐
│ Durability: 75% → Declining Fast │
│ Hostiles: 3 (6.0/sec decline)    │
│ Status: Critical Defense         │
└──────────────────────────────────┘
                ↓
Hostiles Cleared:
┌──────────────────────────────────┐
│ Durability: 50% → Recovering     │
│ Hostiles: 0 (1.0/sec recovery)   │
│ Status: Regenerating             │
└──────────────────────────────────┘
                ↓
Failed Defense:
┌──────────────────────────────────┐
│ Durability: 0%                   │
│ Hostiles: N/A                    │
│ Status: DEFEATED                 │
│ → Episode Reset                  │
└──────────────────────────────────┘
```

### Reward Signal Comparison

**OLD - Distance-Based Rewards:**
```cpp
// Ambiguous progress
if (MovingCloser) {
    Reward += DistanceDelta * 0.5f;  // What distance? How fast?
}

// Type-specific branching
if (Type == Capture) {
    // Offensive logic
} else if (Type == Defend) {
    // Defensive logic
}
```

**NEW - Volume-Based Rewards:**
```cpp
// Clear binary signal
if (InEnemyVolume) {
    Reward += 0.1f;  // Simple: Inside = rewarded
}

// Unified logic
if (IsHostileTo(TeamID)) {
    // Assault: Presence in enemy zone
} else {
    // Defense: Presence in friendly zone
}
```

### Team Symmetry

Both teams use **identical mechanics**:

```cpp
// Team 0 Agent
Friendly = ObjectiveActor with OwnerTeamID = 0
Hostile  = ObjectiveActor with OwnerTeamID = 1

// Team 1 Agent
Friendly = ObjectiveActor with OwnerTeamID = 1
Hostile  = ObjectiveActor with OwnerTeamID = 0

// Reward logic is symmetric
DefendReward = IsInFriendlyVolume ? 0.05f : 0.0f;
AssaultReward = IsInHostileVolume ? 0.1f : 0.0f;
```

No team-specific hardcoding - completely symmetric gameplay.

---

## Next Steps for Integration

### 1. Level Setup (Blueprint/C++)

**Place ObjectiveActors in your map:**

```cpp
// Blueprint or World Outliner
1. Add Actor → AObjectiveActor → Name: "Team0_Base"
   - OwnerTeamID = 0
   - Location = FVector(-2000, 0, 100)
   - CaptureRadius = 1000.0f

2. Add Actor → AObjectiveActor → Name: "Team1_Base"
   - OwnerTeamID = 1
   - Location = FVector(2000, 0, 100)
   - CaptureRadius = 1000.0f
```

**Verify Components:**
- PillarMesh has static mesh assigned
- CaptureVolume is visible (green sphere in editor)
- Debug visualization is enabled initially

---

### 2. Update MCTS Assignment Logic

**File:** `Team/TeamLeaderComponent.cpp`

```cpp
void UTeamLeaderComponent::RunObjectiveAssignment()
{
    if (!ObjectiveManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("TeamLeader: No ObjectiveManager!"));
        return;
    }

    // Find team objectives using new helper methods
    AObjectiveActor* FriendlyBase = ObjectiveManager->FindFriendlyObjective(TeamID);
    AObjectiveActor* EnemyBase = ObjectiveManager->FindHostileObjective(TeamID);

    if (!FriendlyBase || !EnemyBase)
    {
        UE_LOG(LogTemp, Error, TEXT("TeamLeader: Missing objective actors in level!"));
        return;
    }

    // MCTS explores different assignments
    // Option 1: All assault (all → EnemyBase)
    // Option 2: Balanced (2 → EnemyBase, 2 → FriendlyBase)
    // Option 3: Aggressive (3 → EnemyBase, 1 → FriendlyBase)

    // Assign based on MCTS result
    for (auto* Follower : Followers)
    {
        // MCTS selects: FriendlyBase or EnemyBase per agent
        AActor* AssignedObjective = MCTSSelectedObjectiveForAgent(Follower);
        Follower->SetCurrentObjective(AssignedObjective);
    }
}
```

---

### 3. Create Material with Team Color Parameters

**Material Setup (Unreal Editor):**

1. Create Material: `M_ObjectivePillar`
2. Add Parameters:
   - **TeamColor** (Vector Parameter) - Base team color (Blue/Red)
   - **EmissionStrength** (Scalar Parameter) - Pulses when low durability
   - **BaseColor** (Vector Parameter) - Lerps from team color to red as damaged

**Material Graph:**
```
TeamColor → Lerp (Alpha: DurabilityPercent) → BaseColor
                 ↓
            (DurabilityPercent < 0.5 ? Red : TeamColor)

EmissionStrength → Multiply → Emissive Color
                    ↓
                (Higher when damaged)
```

**Apply to ObjectiveActor:**
- Assign `M_ObjectivePillar` to PillarMesh in Blueprint/C++
- ObjectiveActor::UpdateMaterial() will set parameters at runtime

---

### 4. Add GameMode Defeat Handler

**File:** Your GameMode class

```cpp
// In YourGameMode.h
UFUNCTION()
void OnObjectiveDefeated(int32 LosingTeamID);

// In YourGameMode.cpp
void AYourGameMode::OnObjectiveDefeated(int32 LosingTeamID)
{
    UE_LOG(LogTemp, Warning, TEXT("GAME OVER: Team %d lost!"), LosingTeamID);

    // Reset all objectives
    for (TActorIterator<AObjectiveActor> It(GetWorld()); It; ++It)
    {
        AObjectiveActor* Obj = *It;
        if (Obj && IsValid(Obj))
        {
            Obj->ResetDurability();
        }
    }

    // Reset agents
    ResetAllAgents();

    // Respawn characters
    RespawnTeams();

    // Broadcast episode complete to Python training
    BroadcastEpisodeComplete(LosingTeamID == 0 ? -1.0f : 1.0f);
}
```

**Connect to ObjectiveActor:**
```cpp
// In ObjectiveActor::OnDefeat()
AGameModeBase* GameMode = UGameplayStatics::GetGameMode(this);
if (AYourGameMode* CustomGM = Cast<AYourGameMode>(GameMode))
{
    CustomGM->OnObjectiveDefeated(OwnerTeamID);
}
```

---

### 5. Update Python Training Environment

**Remove distance-based features (no longer used):**

```python
# OLD - v6.0 observation space
obs_space = spaces.Box(
    low=-1.0, high=1.0,
    shape=(68,),  # Includes objective distance
    dtype=np.float32
)

# Distance feature calculation (REMOVE THIS):
objective_distance = np.linalg.norm(
    agent_pos - objective_pos
) / MAX_DISTANCE

# NEW - v7.0 observation space
# Keep same 68 features, but distance is now:
# 0.0 if outside volume, 1.0 if inside volume
in_objective_volume = float(
    agent_in_capture_volume(agent, objective)
)
```

**Update reward calculation (server-side only, Python just receives):**
- No changes needed in Python
- RewardCalculator.cpp handles volume-based rewards
- Python training loop receives final reward values

---

### 6. Testing Checklist

```
Setup Tests:
[ ] Project compiles successfully
[ ] Both ObjectiveActors placed in level (Team 0 & Team 1)
[ ] PillarMesh has valid static mesh assigned
[ ] CaptureVolume visible in editor (green sphere)
[ ] Material has TeamColor/EmissionStrength parameters

Functionality Tests:
[ ] Agent enters volume → HostileAgentsInVolume increments
[ ] Agent exits volume → HostileAgentsInVolume decrements
[ ] Durability declines with hostiles (2.0/sec per agent)
[ ] Durability recovers when empty (1.0/sec)
[ ] Material color changes based on durability
[ ] Debug text displays correct values

Reward Tests:
[ ] Agent in friendly volume → +0.05 reward
[ ] Agent in enemy volume → +0.1 reward
[ ] Kill in friendly volume → +5.0 bonus
[ ] Enemy base destroyed → +100.0 reward

Integration Tests:
[ ] MCTS finds objectives via ObjectiveManager
[ ] Agents assigned to correct objectives
[ ] Defeat triggers episode reset
[ ] No compiler errors for removed Capture type

Performance Tests:
[ ] 10Hz durability timer doesn't cause lag
[ ] Overlap events efficient with 8 agents
[ ] Debug visualization can be toggled off
```

---

## File Change Summary

### Files Created (2 files)
```
Source/GameAI_Project/Public/Team/ObjectiveActor.h       (217 lines)
Source/GameAI_Project/Private/Team/ObjectiveActor.cpp    (218 lines)
```

### Files Modified (7 files)
```
Source/GameAI_Project/Public/Team/Objective.h                           (Line 10-25: Removed Capture)
Source/GameAI_Project/Public/Team/ObjectiveManager.h                    (Line 94-109: Added v7.0 methods)
Source/GameAI_Project/Private/Team/ObjectiveManager.cpp                 (Line 1-7, 360-414: Integration)
Source/GameAI_Project/Private/RL/RewardCalculator.cpp                   (Line 1-10, 214-343, 346-361: Volume rewards)
Source/GameAI_Project/Private/Team/FollowerAgentComponent.cpp           (Line 237-242: Strategy mapping)
Source/GameAI_Project/Private/AI/MCTS/MCTS.cpp                          (Line 390-393: Heuristic)
Source/GameAI_Project/Private/StateTree/FollowerStateTreeSchema.cpp    (Line 13-14, 191-192: Deprecated)
```

### Files Deprecated (2 files)
```
Source/GameAI_Project/Public/StateTree/Conditions/STCondition_CheckObjectiveType.h
Source/GameAI_Project/Private/StateTree/Conditions/STCondition_CheckObjectiveType.cpp
```
*Note: Left in place for backward compatibility, marked deprecated*

---

## Architecture Impact

### Component Relationships (v7.0)

```
ObjectiveManager
     ├─ FindAllObjectiveActors() → TArray<AObjectiveActor*>
     ├─ FindFriendlyObjective(TeamID) → AObjectiveActor*
     └─ FindHostileObjective(TeamID) → AObjectiveActor*
                    ↓
TeamLeaderComponent
     ├─ RunObjectiveAssignment()
     │    ├─ Query ObjectiveManager
     │    ├─ Run MCTS (explore assignments)
     │    └─ Assign followers to objectives
     └─ Followers receive objective assignments
                    ↓
FollowerAgentComponent
     ├─ SetCurrentObjective(AObjectiveActor*)
     ├─ GetStrategy() based on objective
     └─ RewardCalculator checks volume presence
                    ↓
RewardCalculator
     ├─ Check ObjectiveActor->IsAgentInVolume()
     ├─ Calculate volume retention rewards
     └─ Provide reward to FollowerComponent
                    ↓
ObjectiveActor (Physical World)
     ├─ CaptureVolume detects overlaps
     ├─ Update HostileAgentsInVolume
     ├─ Decline/recover durability (timer)
     └─ OnDefeat() → GameMode reset
```

### Data Flow (v7.0)

```
1. Level Start:
   → ObjectiveActors spawn in world
   → TeamLeaderComponent queries ObjectiveManager
   → MCTS receives available objectives

2. Assignment Phase (every 1.5s):
   → MCTS explores assignments
   → RL value function evaluates each option
   → Best assignment selected
   → Followers receive objective targets

3. Execution Phase (every tick):
   → Agents move toward objectives
   → Enter/exit capture volumes
   → ObjectiveActor tracks hostiles
   → RewardCalculator checks volume presence
   → Rewards provided based on presence

4. Durability Update (10Hz):
   → Count HostileAgentsInVolume
   → Decline: Hostiles × 2.0/sec
   → Recover: Empty volume → +1.0/sec
   → Check defeat condition (≤ 0.0)

5. Defeat Condition:
   → Durability reaches 0
   → OnDefeat() broadcasts to GameMode
   → Episode reset triggered
   → All ObjectiveActors reset durability
```

---

## Benefits Summary

### Code Simplification
- **Removed:** 150+ lines of type-checking logic
- **Removed:** Distance calculation overhead
- **Removed:** Ambiguous reward signals
- **Added:** Simple `IsFriendlyTo()` / `IsHostileTo()` checks
- **Added:** Clear binary volume presence checks

### Gameplay Improvements
- **Symmetric mechanics:** Both teams identical
- **Clear win condition:** Durability = 0
- **Visual feedback:** See durability % and hostile count
- **Physical objectives:** Actual world actors, not abstract
- **Better incentives:** "Stay in zone" is unambiguous

### Training Improvements
- **Clearer rewards:** Volume presence vs distance
- **Better alignment:** Rewards match desired behavior
- **Symmetric learning:** No team-specific biases
- **Explicit goals:** Destroy enemy base, defend friendly

### Performance
- **10Hz updates:** Efficient timer-based durability
- **Efficient tracking:** TSet for hostile agents
- **Binary checks:** Faster than distance calculations
- **Minimal overhead:** No distance normalization per tick

---

## Troubleshooting Guide

### Common Issues

**Issue: Durability not declining**
```
Check:
1. Are hostile agents actually entering volume?
   → Enable debug visualization (bShowDebugVisualization = true)
   → Look for "Hostile agent entered volume" logs

2. Is CaptureVolume collision enabled?
   → Check: SetCollisionEnabled(QueryOnly)
   → Check: SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap)

3. Is timer running?
   → Look for "Durability declining" verbose logs
   → Check GetTimerManager().IsTimerActive(DurabilityUpdateTimer)
```

**Issue: Rewards not working**
```
Check:
1. Is ObjectiveActor cast succeeding?
   → Look for "Team/ObjectiveActor.h" include in RewardCalculator.cpp
   → Add log: UE_LOG(LogTemp, Warning, TEXT("ObjectiveActor: %s"), ObjectiveActor ? TEXT("Valid") : TEXT("NULL"));

2. Is agent actually in volume?
   → Check CaptureVolume->IsOverlappingActor(Agent)
   → Verify CaptureRadius is large enough (default 1000cm)

3. Are rewards being forwarded?
   → Look for "[REWARD]" logs in RewardCalculator::CalculateObjectiveProgressReward
   → Verify FollowerComponent->ProvideReward() is called
```

**Issue: Teams not finding objectives**
```
Check:
1. Are ObjectiveActors placed in level?
   → World Outliner → Search "ObjectiveActor"
   → Should see 2 actors (Team0_Base, Team1_Base)

2. Are OwnerTeamIDs set correctly?
   → Team0_Base: OwnerTeamID = 0
   → Team1_Base: OwnerTeamID = 1

3. Is ObjectiveManager valid?
   → Check TeamLeaderComponent has reference
   → Verify FindAllObjectiveActors() returns 2 actors
```

---

## References

### Design Document
See: `OBJECTIVE_SYSTEM.md` (original specification)

### Implementation Files
- Objective System: `Team/ObjectiveActor.{h,cpp}`
- Rewards: `RL/RewardCalculator.cpp:214-343`
- Integration: `Team/ObjectiveManager.{h,cpp}`
- Architecture: `CLAUDE.md` (updated v7.0 section)

### Key Commits
- v7.0 Implementation: 2026-01-09
- Objective System Refactoring
- Volume-Based Rewards
- Legacy Code Cleanup

---

**Document Version:** v7.0
**Last Updated:** 2026-01-09
**Next Review:** After MCTS integration testing
