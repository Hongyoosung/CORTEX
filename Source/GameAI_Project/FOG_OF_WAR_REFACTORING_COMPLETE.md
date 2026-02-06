# Fog of War Architecture Refactoring - Complete

**Date:** 2026-02-06
**Status:** ✅ Complete
**Version:** MOC v10.1

---

## Overview

Successfully separated fog-of-war logic from `TeamManager` into a dedicated `AFogOfWarManager` actor. The system now implements a clean separation of concerns with proper delegation patterns.

---

## Changes Made

### 1. **New Files Created**

#### `Public/Team/FogOfWarManager.h`
- **AFogOfWarManager** class with comprehensive fog-of-war management
- **FExploredResource** struct for permanent resource memory
- Separate memory systems for enemies (5-second decay) and resources (permanent)

**Key Features:**
- Dynamic enemy position tracking with 5-second memory decay
- Permanent resource discovery with availability state updates
- Render Target interfaces for visual fog-of-war (prepared for future GPU implementation)
- Debug visualization with fading enemy markers
- Clean separation of Red/Blue team data

**Public API:**
```cpp
// Enemy tracking (5-second decay)
void ReportEnemy(int32 TeamID, AActor* Enemy, FVector Location);
FVector GetLastKnownEnemyPosition(int32 TeamID, AActor* Enemy) const;
bool IsEnemyPositionValid(int32 TeamID, AActor* Enemy) const;
TArray<AActor*> GetRememberedEnemies(int32 TeamID) const;

// Resource tracking (permanent memory)
void ReportResource(int32 TeamID, AActor* Resource, bool bAvailable);
TArray<FExploredResource> GetKnownResources(int32 TeamID) const;
TArray<FVector> GetKnownResourceLocations(int32 TeamID) const;

// Visual system (prepared for render target implementation)
void UpdateVision(int32 TeamID, FVector ViewerLocation, float Radius);
UTextureRenderTarget2D* GetFogTexture(int32 TeamID) const;
```

#### `Private/Team/FogOfWarManager.cpp`
- Complete implementation of fog-of-war logic
- `DecayEnemyPositions()` - Automatic cleanup of expired enemy positions
- `DrawDebugInfo()` - Visual debugging with color-coded team markers
- Uses `FindByPredicate` to prevent duplicate resource entries

---

### 2. **Modified Files**

#### `Public/Team/TeamManager.h`
**Removed:**
- `#include "Team/TeamTypes.h"` (deleted file)
- `LastKnownEnemyPositions` from `FMocTeamState`
- `EnemyPositionTimestamps` from `FMocTeamState`
- `KnownHealthPackLocations` from `FMocTeamState`
- `KnownAmmoLocations` from `FMocTeamState`
- `UpdateSharedKnowledge()` function
- `UpdateEnemyPositionDecay()` function
- `KnowledgeUpdateInterval` property
- `EnemyPositionMemoryDuration` property
- `TimeSinceKnowledgeUpdate` member

**Added:**
- Forward declaration: `class AFogOfWarManager;`
- `AFogOfWarManager* FogOfWarManager` property
- `AFogOfWarManager* GetFogOfWarManager() const` accessor
- Updated function comments to indicate delegation

**Changed:**
- `FMocTeamState` now only contains team core data (agents, score, color)
- Fog-of-war functions now delegate to `FogOfWarManager`

#### `Private/Team/TeamManager.cpp`
**Modified Functions:**
- `BeginPlay()` - Now finds or spawns `AFogOfWarManager`
- `Tick()` - Removed knowledge update loop
- `ResetAllAgents()` - Removed fog-of-war clearing (delegated to manager)
- `ReportEnemySighting()` - Delegates to `FogOfWarManager->ReportEnemy()`
- `ReportResourceDiscovery()` - Delegates to `FogOfWarManager->ReportResource()`
- `GetLastKnownEnemyPosition()` - Delegates to `FogOfWarManager`
- `IsEnemyPositionValid()` - Delegates to `FogOfWarManager`

**Removed Functions:**
- `UpdateSharedKnowledge()`
- `UpdateEnemyPositionDecay()`

**Added:**
- `#include "Team/FogOfWarManager.h"`
- Updated includes to use new paths (`Team/`, `Core/`, `Actors/`)

#### `Public/Characters/MocCharacter.h`
**Added:**
- `float VisionRange` property (default 3000.0f = 30 meters)
- EditorVisible category for vision configuration

#### `Private/Characters/MocCharacter.cpp`
**Modified:**
- `Tick()` - Now calls `FogOfWarManager->UpdateVision()` every frame (when alive)
- Added `#include "Team/FogOfWarManager.h"`
- Updated includes to use new paths

---

## Architecture Design

### **Separation of Concerns**

```
TeamManager (Team Management)
    ├─ Spawn/Respawn agents
    ├─ Track team scores
    ├─ Manage team state
    └─ Reference to FogOfWarManager

FogOfWarManager (Vision & Memory)
    ├─ Enemy position memory (5-second decay)
    ├─ Resource discovery (permanent)
    ├─ Visual fog rendering (prepared)
    └─ Team knowledge isolation

MocCharacter (Agent)
    └─ Reports vision to FogOfWarManager every tick
```

### **Memory Systems**

#### **Dynamic Memory (Enemies)**
- **Duration:** 5 seconds after last sighting
- **Decay:** Automatic cleanup via `DecayEnemyPositions()`
- **Query:** `IsEnemyPositionValid()` checks age
- **Use Case:** Tactical planning based on recent intel

#### **Permanent Memory (Resources)**
- **Duration:** Forever (once discovered)
- **State Updates:** Availability refreshed when in sight
- **Duplicate Prevention:** Uses `FindByPredicate`
- **Use Case:** Strategic resource planning

---

## Integration Points

### **1. TeamManager → FogOfWarManager**
```cpp
// Spawning/Finding manager
BeginPlay() → Find or spawn AFogOfWarManager

// Delegation pattern
ReportEnemySighting() → FogOfWarManager->ReportEnemy()
GetLastKnownEnemyPosition() → FogOfWarManager->GetLastKnownEnemyPosition()
```

### **2. MocCharacter → FogOfWarManager**
```cpp
// Vision updates (every tick)
Tick() → FogOfWarManager->UpdateVision(TeamID, Location, VisionRange)
```

### **3. GameMode Integration**
```cpp
// MocGameMode should provide TeamManager reference
TeamManager->GetFogOfWarManager() → Access to fog system
```

---

## Performance Considerations

### **Optimizations Implemented:**
1. **Tick Group:** `TG_PrePhysics` for early processing
2. **Decay Check Interval:** 0.5 seconds (configurable via `DecayCheckInterval`)
3. **Separate Team Data:** No cross-team lookups
4. **Vision Updates:** Per-agent, not centralized (distributes load)

### **Expected Performance:**
- **10 agents:** ~10 vision updates/frame (minimal cost)
- **Enemy decay checks:** Every 0.5s (not every frame)
- **Resource queries:** O(1) lookup via TMap/TArray
- **Debug rendering:** Only when `bShowDebugInfo = true`

---

## Configuration Properties

### **FogOfWarManager Properties:**
```cpp
float EnemyMemoryDuration = 5.0f;          // Enemy memory duration
float DecayCheckInterval = 0.5f;           // How often to clean up
FVector2D MapSize = (15000, 15000);        // For render target mapping
int32 RenderTargetResolution = 512;        // Texture resolution
bool bShowDebugInfo = false;               // Debug visualization
```

### **MocCharacter Properties:**
```cpp
float VisionRange = 3000.0f;               // Vision radius (30m)
```

---

## Future Work (Visual Rendering)

### **Render Target Implementation (TODO):**
The visual fog-of-war system is prepared but not yet implemented. Future implementation should:

1. **Initialize Render Targets:**
   - Create `UTextureRenderTarget2D` in `BeginPlay()`
   - Set resolution to `RenderTargetResolution`
   - Format: `RTF_RGBA8` or `RTF_R8`

2. **UpdateVision() GPU Implementation:**
   - Convert world position to UV coordinates
   - Draw circle at UV position with radius
   - Use Canvas or compute shader for drawing
   - Apply fog decay over time

3. **Material Integration:**
   - Create post-process material
   - Sample fog texture for team
   - Blend with world rendering
   - Apply fog color/intensity

---

## Testing Checklist

### **✅ Completed:**
- [x] Enemy positions tracked for 5 seconds
- [x] Resources permanently remembered
- [x] No duplicate resource entries
- [x] TeamManager delegates correctly
- [x] MocCharacter updates vision every tick
- [x] Debug visualization works
- [x] Separate Red/Blue team data

### **⚠️ Needs Testing:**
- [ ] Integration with AI perception system
- [ ] EQS context updates (see note below)
- [ ] Resource availability state updates
- [ ] Episode reset behavior
- [ ] Multi-agent spawn scenarios

---

## Known Issues

### **1. MocEQSContext.cpp Outdated**
**File:** `Private/EQS/MocEQSContext.cpp`

**Issues Found:**
- Uses old `MocChar->TeamID` (should use `GetTeamID()`)
- References non-existent `GetSharedKnowledge()` function
- Uses undefined `FEnemyInfo` structure
- Uses `GetTeamMembers()` which may not exist

**Status:** Legacy code requiring separate refactoring
**Impact:** EQS queries for enemies/resources will fail
**Action Required:** Update EQS context to use new fog-of-war API

### **Recommended EQS Fix:**
```cpp
// Example fix for UEnvQueryContext_MocEnemies::ProvideContext()
AFogOfWarManager* FoWManager = TeamManager->GetFogOfWarManager();
if (FoWManager)
{
    TArray<AActor*> RememberedEnemies = FoWManager->GetRememberedEnemies(MyTeamID);
    TArray<FVector> EnemyPositions;
    for (AActor* Enemy : RememberedEnemies)
    {
        FVector Pos = FoWManager->GetLastKnownEnemyPosition(MyTeamID, Enemy);
        if (!Pos.IsZero())
        {
            EnemyPositions.Add(Pos);
        }
    }
    UEnvQueryItemType_Point::SetContextHelper(ContextData, EnemyPositions);
}
```

---

## Migration Guide

### **For Existing Code:**

**Old Code:**
```cpp
// Getting enemy positions
FMocTeamState TeamState = TeamManager->GetTeamState(TeamID);
FVector EnemyPos = TeamState.LastKnownEnemyPositions[Enemy];
```

**New Code:**
```cpp
// Getting enemy positions
AFogOfWarManager* FoWManager = TeamManager->GetFogOfWarManager();
FVector EnemyPos = FoWManager->GetLastKnownEnemyPosition(TeamID, Enemy);
```

**Old Code:**
```cpp
// Getting resource locations
TArray<FVector> HealthPacks = TeamState.KnownHealthPackLocations;
```

**New Code:**
```cpp
// Getting resource locations
TArray<FExploredResource> Resources = FoWManager->GetKnownResources(TeamID);
// Or just locations:
TArray<FVector> Locations = FoWManager->GetKnownResourceLocations(TeamID);
```

---

## Summary

**✅ Objectives Achieved:**
1. ✅ Separated fog-of-war logic into dedicated manager
2. ✅ Implemented 5-second enemy memory decay
3. ✅ Implemented permanent resource memory
4. ✅ Prepared visual rendering interfaces
5. ✅ Clean delegation pattern from TeamManager
6. ✅ Per-agent vision updates from MocCharacter
7. ✅ Debug visualization system

**📊 Code Statistics:**
- **New Files:** 2 (FogOfWarManager.h, .cpp)
- **Modified Files:** 4 (TeamManager.h, .cpp, MocCharacter.h, .cpp)
- **Removed Code:** ~150 lines (fog-of-war from TeamManager)
- **Added Code:** ~450 lines (dedicated fog-of-war system)
- **Net Change:** +300 lines (better organization)

**🎯 Architecture Benefits:**
- Clear separation of concerns
- Reusable fog-of-war system
- Easier to test in isolation
- Prepared for visual rendering
- No coupling between team management and vision

---

## Contact & Support

For questions about this refactoring:
- Review this document
- Check `FogOfWarManager.h` for API reference
- Enable `bShowDebugInfo` for visual debugging
- Review Git commit for detailed changes

**Next Steps:**
1. Test with full 5v5 scenarios
2. Fix EQS context integration
3. Implement visual fog rendering
4. Add network replication (if needed)
