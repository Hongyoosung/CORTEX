# Training Mode Movement Fix - v10.2

**Date:** 2026-02-14
**Critical Fix:** Replaced teleportation with proper AI movement in training mode

---

## Issues Resolved

### 1. EQS Corner-Stuck Bug ✅
**Problem:** Forward-facing cone queries failed when agents backed into corners
**Solution:** Updated guide to recommend 360° circular generator (Points: Donut)

### 2. Teleportation in Training Mode ✅ CRITICAL FIX
**Problem:** Training mode used `SetActorLocation()` which teleported agents, ignoring physics
**Solution:** Now uses `AIController->MoveToLocation()` to respect movement speed and physics

---

## The Critical Bug

### What Was Wrong (Before)
```cpp
// MocCharacter.cpp:403 - WRONG APPROACH
SetActorLocation(TargetLocation);  // ❌ Instant teleportation!
```

**Problems:**
- Bypassed CharacterMovementComponent entirely
- Ignored MaxWalkSpeed (600 cm/s)
- No collision detection during movement
- Agents phased through walls
- Unrealistic training data (policy learned from broken physics)
- Called every tick = 60+ teleports/second

### What's Correct (After)
```cpp
// MocCharacter.cpp - CORRECT APPROACH
AICtrl->MoveToLocation(TargetLocation, EQSAcceptanceRadius, true, true, false, true);
// ✅ Respects MaxWalkSpeed
// ✅ Uses pathfinding and navigation
// ✅ Respects collision
// ✅ Physics-based movement
```

**Benefits:**
- Agents walk at realistic 6 m/s (MaxWalkSpeed = 600 cm/s)
- Proper collision detection
- Can't phase through walls
- Policy learns from realistic physics
- Movement looks natural

---

## Technical Changes

### Files Modified

**1. MocCharacter.cpp (PerformTacticalAction)**

**Before:**
```cpp
// Training mode
SetActorLocation(TargetLocation);
```

**After:**
```cpp
// Training mode - Use AI navigation (respects MaxWalkSpeed and physics)
AICtrl->MoveToLocation(TargetLocation, EQSAcceptanceRadius, true, true, false, true);
```

**Parameters explained:**
- `TargetLocation`: Best position from EQS query
- `EQSAcceptanceRadius`: 50 cm acceptance radius
- `true`: Stop on overlap
- `true`: Use pathfinding
- `false`: Don't allow partial paths
- `true`: Project destination to navigation

**2. EQS_ASSET_CREATION_GUIDE.md**
- Updated Section 5.1 to recommend "Points: Donut" (360° coverage)
- Added explanation why circular > cone for corner navigation

---

## Movement Speed Breakdown

### Expected Behavior
```
MaxWalkSpeed = 600 cm/s = 6 m/s = 21.6 km/h
```

**Action Frequency:**
- Schola calls `TakeAction()` every decision step
- Typical: 5-10 Hz depending on Schola configuration
- Each action issues new `MoveToLocation()` command

**Actual Movement:**
- Agent starts moving toward target at 600 cm/s
- May be interrupted by next action before reaching target
- This is CORRECT behavior - agents continuously update tactical position

### Why High Action Frequency is OK Now
Before: 60 actions/sec × teleportation = 60 position jumps = broken
After: 60 actions/sec × walking = direction updates = realistic tactical repositioning

The agent doesn't teleport anymore, it just updates its movement target frequently (like a human constantly adjusting their path).

---

## EQS Asset Update Required

You must manually update your EQS asset:

### Steps:
1. Open `Content/Game/AI/EQS/EQS_MOC_TacticalPositioning`
2. **Delete** the "Points in Cone" generator
3. **Add** new generator: Right-click Root → Add Generator → **Points: Donut**
4. **Configure:**
   ```
   Number of Rings: 6
   Points Per Ring: 8
   Ring Distance: 500.0
   Inner Radius: 0.0
   Outer Radius: 3000.0
   ```
5. **Save** the asset

### Why Circular vs Cone?

| Generator Type | Coverage | Corner Behavior |
|----------------|----------|-----------------|
| Points in Cone | 180° (forward only) | ❌ Gets stuck - all samples hit wall |
| Points: Donut | 360° (all directions) | ✅ Finds escape routes backward/sideways |

---

## Testing Checklist

After applying these fixes:

- [ ] Update EQS asset to Points: Donut generator
- [ ] Recompile C++ code
- [ ] Run training session
- [ ] Observe agent movement speed (should be ~6 m/s, not teleporting)
- [ ] Test in tight corners (agents should escape, not stuck)
- [ ] Verify no wall clipping/phasing
- [ ] Check training logs for navigation warnings

---

## Impact on Training

### Before Fix
| Metric | Value |
|--------|-------|
| Movement Type | Teleportation |
| Effective Speed | 100+ m/s (uncontrolled) |
| Physics | Ignored |
| Wall Collision | Phasing through |
| Corner Behavior | Stuck (cone) |

### After Fix
| Metric | Value |
|--------|-------|
| Movement Type | AI Navigation |
| Effective Speed | 6 m/s (MaxWalkSpeed) |
| Physics | Respected |
| Wall Collision | Proper collision detection |
| Corner Behavior | Escapes (circular query) |

---

## Why This Matters for RL Training

**Teleportation creates invalid training data:**
- State transitions don't match real physics
- Policy learns to exploit broken movement
- Transfer to runtime fails (runtime uses real movement)
- Reward signals are meaningless (positions change instantly)

**Proper movement creates valid training data:**
- Realistic state transitions (position changes gradually)
- Policy learns grounded tactics
- Direct transfer to runtime (same movement mechanics)
- Reward signals are meaningful (agent earns them through navigation)

---

**Version:** v10.2
**Last Updated:** 2026-02-14
**Status:** CRITICAL FIX - Required for valid training
