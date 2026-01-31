# CORTEX v9.0: MCTS Strategy Lock Fix

**Date:** 2026-01-30
**Issue:** Strategy distribution locked at 75% Assault, 24% Support (Batch 0: TightAssault)

---

## Problems Identified

### Problem 1: Within-Episode MCTS Selection Lock ⚠️ CRITICAL

**Root Cause:**
- All batches start with `Trials=0` → `UCB=∞` for all batches
- In `SelectBatchByUCB1()`, when comparing UCB values:
  ```cpp
  if (UCB > BestUCB)  // Note: NOT >=
  {
      BestUCB = UCB;
      BestBatchIdx = i;
  }
  ```
- When all batches tie (UCB=∞), the loop always selects **the first batch (index 0)**
- MCTS runs every 20 seconds but **never updates cache during episode**
- Result: Same batch (TightAssault: 3A+1S) selected repeatedly

**Evidence:**
```
[MCTS v8.20] UCB1 Batch Selection:
  Batch 0: WR=50.00 (0/0), Exploration=0.00, UCB=∞ ← BEST
  Batch 1: WR=50.00 (0/0), Exploration=0.00, UCB=∞
  Batch 2: WR=50.00 (0/0), Exploration=0.00, UCB=∞
  ...
Selected batch 0 with UCB=∞  ← ALWAYS BATCH 0
```

### Problem 2: Episode Completion Verification ⚠️

**Potential Issue:**
- `OnEpisodeComplete()` handler exists and calls `UpdateBatchCache()`
- But lacks detailed logging to verify it's being called correctly
- If `CurrentBatchKey` is empty, cache update is silently skipped

---

## Solutions Implemented

### Fix 1: Epsilon-Greedy Exploration (20% Random Selection)

**File:** `AI/MCTS/MCTS.cpp::SelectBatchByUCB1()`

**Change:**
```cpp
// ✅ v9.0 FIX: Epsilon-greedy exploration (20% random selection)
const float EpsilonExploration = 0.20f;
if (FMath::FRand() < EpsilonExploration)
{
    int32 RandomIdx = FMath::RandRange(0, AllBatches.Num() - 1);
    UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0] 🎲 EPSILON-GREEDY: Randomly selected batch %d (exploration)"), RandomIdx);
    return AllBatches[RandomIdx];
}
```

**Effect:**
- **80% of the time**: Use UCB1 (exploit best known batch)
- **20% of the time**: Random selection (explore untried batches)
- Within a single episode (e.g., 3 MCTS calls at t=0s, 20s, 40s):
  - 1st call: 20% chance random, 80% chance batch 0 (all tied)
  - 2nd call: 20% chance random, 80% chance batch 0 (still tied)
  - 3rd call: 20% chance random, 80% chance batch 0 (still tied)
- **Expected diversity**: ~60% batch variety per episode

### Fix 2: Randomized Tie-Breaking

**File:** `AI/MCTS/MCTS.cpp::SelectBatchByUCB1()`

**Change:**
```cpp
// v9.0: Track all batches with best UCB (for tie-breaking)
TArray<int32> BestBatchIndices;

for (int32 i = 0; i < AllBatches.Num(); ++i)
{
    if (UCB > BestUCB)
    {
        BestUCB = UCB;
        BestBatchIndices.Empty();
        BestBatchIndices.Add(i);
    }
    else if (FMath::IsNearlyEqual(UCB, BestUCB, 0.0001f))
    {
        BestBatchIndices.Add(i);  // Track ties
    }
}

// ✅ v9.0 FIX: Randomized tie-breaking
if (BestBatchIndices.Num() > 1)
{
    BestBatchIdx = BestBatchIndices[FMath::RandRange(0, BestBatchIndices.Num() - 1)];
    UE_LOG(LogTemp, Warning, TEXT("[MCTS v9.0] 🎲 TIE-BREAKING: %d batches tied, randomly selected batch %d"),
        BestBatchIndices.Num(), BestBatchIdx);
}
```

**Effect:**
- When multiple batches have identical UCB values (common with UCB=∞)
- **Randomly select one** instead of always picking the first
- Provides additional exploration even when epsilon-greedy doesn't trigger

### Fix 3: Enhanced Episode Completion Logging

**File:** `Team/Components/TeamLeaderComponent.cpp::OnEpisodeComplete()`

**Changes:**
1. **Entry logging:**
   ```cpp
   UE_LOG(LogTemp, Warning, TEXT("[EPISODE END v9.0] 🏁 Team %d (%s): Episode complete received for Env %d"),
       TeamID, *TeamName, EnvironmentID);
   ```

2. **Filter logging:**
   ```cpp
   UE_LOG(LogTemp, Verbose, TEXT("[EPISODE END v9.0] Team %d: Ignoring Env %d (MyEnv=%d)"),
       TeamID, EnvironmentID, MyEnvironmentID);
   ```

3. **Cache update logging:**
   ```cpp
   UE_LOG(LogTemp, Warning, TEXT("[MCTS CACHE UPDATE v9.0] Team %d: Updating batch '%s' with result..."),
       TeamID, *CurrentBatchKey);

   UE_LOG(LogTemp, Warning, TEXT("[MCTS CACHE UPDATE v9.0] ✅ Team %d | Batch '%s' → %s"),
       TeamID, *CurrentBatchKey, *ResultStr);
   ```

4. **Failure detection:**
   ```cpp
   UE_LOG(LogTemp, Error, TEXT("❌ [MCTS CACHE UPDATE v9.0 FAILED] Team %d: CurrentBatchKey is EMPTY! MCTS did not run this episode."), TeamID);
   ```

5. **Batch storage verification:**
   ```cpp
   UE_LOG(LogTemp, Warning, TEXT("[MCTS BATCH STORED v9.0] Team %d: BatchKey='%s' (%d assignments)"),
       TeamID, *CurrentBatchKey, CurrentAssignments.Num());
   ```

**Effect:**
- Full visibility into episode lifecycle
- Easy diagnosis of cache update failures
- Verification that batches are being tracked correctly

---

## Expected Behavior After Fix

### Within-Episode MCTS (20-second intervals)

**Before Fix:**
```
t=0s:   UCB=∞ for all → Select batch 0 (TightAssault)
t=20s:  UCB=∞ for all → Select batch 0 (TightAssault)  ← STUCK
t=40s:  UCB=∞ for all → Select batch 0 (TightAssault)  ← STUCK
```

**After Fix:**
```
t=0s:   80% → UCB tie-break (random among all 8)
        20% → Epsilon-greedy random
        Result: Varied selection (e.g., Batch 2: Balanced)

t=20s:  80% → UCB tie-break (random among all 8)
        20% → Epsilon-greedy random
        Result: Varied selection (e.g., Batch 5: OffensiveSwarm)

t=40s:  80% → UCB tie-break (random among all 8)
        20% → Epsilon-greedy random
        Result: Varied selection (e.g., Batch 1: WideDefense)
```

### Expected Strategy Distribution (Python Logs)

**Before Fix:**
```
[STRATEGY DIST] Assault= 75% | Defend= 0% | Support= 24% | Retreat= 0%
```

**After Fix (First Episode):**
```
[STRATEGY DIST] Assault= 45% | Defend= 20% | Support= 25% | Retreat= 10%
```
- Much more balanced distribution
- All strategies should appear

**After Fix (After 100+ Episodes):**
```
[STRATEGY DIST] Assault= 40% | Defend= 25% | Support= 20% | Retreat= 15%
```
- UCB1 learning converges to best-performing batches
- But maintains exploration via epsilon-greedy
- Distribution reflects actual win rates

### Expected Logs

**MCTS Selection (Diverse):**
```
[MCTS v9.0] 🎲 EPSILON-GREEDY: Randomly selected batch 3 (exploration)
[MCTS v9.0] UCB1 Batch Selection:
  Batch 0: WR=50.00 (0/0), Exploration=0.00, UCB=∞ ← TIE
  Batch 1: WR=50.00 (0/0), Exploration=0.00, UCB=∞ ← TIE
  ...
  Batch 7: WR=50.00 (0/0), Exploration=0.00, UCB=∞ ← TIE
[MCTS v9.0] 🎲 TIE-BREAKING: 8 batches tied, randomly selected batch 5
[MCTS v9.0] ✅ Selected batch 5 with UCB=∞
```

**Episode End (Cache Update):**
```
[EPISODE END v9.0] 🏁 Team 0 (Bravo): Episode complete received for Env 0
[MCTS CACHE UPDATE v9.0] Team 0: Updating batch 'BP_FollowerAgent_C_28→Assault,BP_FollowerAgent_C_29→Assault,BP_FollowerAgent_C_30→Defend,BP_FollowerAgent_C_31→Support' with result...
[MCTS CACHE UPDATE v9.0] ✅ Team 0 | Batch '...' → WIN 🏆
```

**UCB Convergence (After 50+ Episodes):**
```
[MCTS v9.0] UCB1 Batch Selection:
  Batch 0: WR=58.33 (7/12), Exploration=0.15, UCB=0.8912 ← NEW BEST
  Batch 1: WR=45.45 (5/11), Exploration=0.16, UCB=0.7321
  Batch 2: WR=50.00 (4/8), Exploration=0.21, UCB=0.8142
  ...
[MCTS v9.0] ✅ Selected batch 0 with UCB=0.8912
```

---

## Testing Checklist

- [ ] **Single Episode Test**
  - Run one episode, check logs for MCTS calls at 0s, 20s, 40s
  - Verify different batches are selected (not always Batch 0)
  - Check Python logs: `[STRATEGY EXTRACT]` should show varied one-hot vectors

- [ ] **Episode Completion Test**
  - Check for `[EPISODE END v9.0] 🏁` log at episode end
  - Verify `[MCTS CACHE UPDATE v9.0] ✅` appears
  - Confirm batch key is not empty

- [ ] **Multi-Episode Test (10+ episodes)**
  - Verify `[STRATEGY DIST]` log shows balanced distribution (not 75%/24%/0%/0%)
  - Check cache file created: `Saved/MCTS/BatchCache.json`
  - Verify UCB values start to diverge (not all ∞)

- [ ] **Convergence Test (100+ episodes)**
  - UCB values should stabilize (e.g., 0.6-0.9 range)
  - Win rate distribution should reflect learning
  - Best batches selected more often (but exploration still occurs)

---

## Rollback Instructions

If issues occur, revert these files:
1. `Source/GameAI_Project/Private/AI/MCTS/MCTS.cpp`
2. `Source/GameAI_Project/Private/Team/Components/TeamLeaderComponent.cpp`

```bash
git checkout HEAD~1 -- Source/GameAI_Project/Private/AI/MCTS/MCTS.cpp
git checkout HEAD~1 -- Source/GameAI_Project/Private/Team/Components/TeamLeaderComponent.cpp
```

---

## Future Improvements

1. **Adaptive Epsilon:** Start with 30% exploration, decay to 10% over time
2. **In-Episode Cache Updates:** Track batch usage within episode, update soft counts
3. **Batch Priority:** Weight batches by recent performance (sliding window)
4. **Multi-Armed Bandit:** Replace UCB1 with Thompson Sampling or UCB-V

---

**Status:** ✅ Implementation Complete
**Next Step:** Test in training environment, monitor strategy distribution
