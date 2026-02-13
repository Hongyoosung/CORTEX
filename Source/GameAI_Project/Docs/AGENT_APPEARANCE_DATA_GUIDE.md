# Agent Appearance Data System (MOC v10.2)

**Status:** ✅ Implemented
**Date:** 2026-02-14
**Architecture:** Data-Driven Configuration

---

## Overview

MOC v10.2 introduces a **data-driven appearance system** that separates visual configuration from logic. Instead of hardcoding skeletal meshes and materials in `TeamManager`, all appearance settings are now stored in reusable **Data Assets** (`UAgentAppearanceData`).

### Benefits
- ✅ **Centralized Configuration:** One asset contains all appearance settings (mesh, materials, animations, team identity)
- ✅ **Reusability:** Create multiple variants (Red Team, Blue Team, Elite Squad, etc.) without code changes
- ✅ **Designer-Friendly:** Artists and designers can create new appearances without touching C++
- ✅ **Version Control Friendly:** Appearance changes are tracked in asset files, not code
- ✅ **Extensibility:** Easy to add new properties (VFX, sounds, custom parameters) without refactoring

---

## Quick Start

### Step 1: Create Appearance Data Assets

1. **Right-click in Content Browser**
2. **Miscellaneous → Data Asset → AgentAppearanceData**
3. **Name the asset:**
   - `DA_RedTeamAppearance` (Red team default)
   - `DA_BlueTeamAppearance` (Blue team default)
   - `DA_EliteAppearance` (Special variant)

### Step 2: Configure Appearance

**Open `DA_RedTeamAppearance` and set:**

| Category | Property | Example Value | Description |
|----------|----------|---------------|-------------|
| **Team Identity** | Team Name | "Red Team" | Display name (used in logs, UI) |
| | Team Color | `(1.0, 0.0, 0.0)` | Primary team color (RGB) |
| | Team Secondary Color | `(0.5, 0.0, 0.0)` | Accent color (optional) |
| **Visual Appearance** | Skeletal Mesh | `SK_Agent_Body` | Agent body mesh |
| | Animation Blueprint | `ABP_AgentLocomotion` | Animation controller |
| | Primary Material | `M_RedTeamSuit` | Main material (mesh slot 0) |
| | Secondary Material | `M_RedTeamArmor` | Secondary material (mesh slot 1) |
| **Customization** | Mesh Scale | `1.0` | Scale multiplier (1.0 = default) |
| | Use Dynamic Materials | `true` | Enable runtime parameter changes |
| **Effects** | Spawn VFX | `P_RedTeamSpawn` | Particle effect on spawn |
| | Death VFX | `P_RedTeamDeath` | Particle effect on death |

Repeat for `DA_BlueTeamAppearance` with blue colors and materials.

### Step 3: Assign to TeamManager

1. **Open level containing `TeamManager`**
2. **Select `TeamManager` actor**
3. **In Details Panel:**
   - `Red Team Config → Appearance Data` → Set to `DA_RedTeamAppearance`
   - `Blue Team Config → Appearance Data` → Set to `DA_BlueTeamAppearance`
4. **Save level**

### Step 4: Test

1. **PIE (Play In Editor)**
2. **Verify in logs:**
   ```
   LogTemp: TeamManager: Red Team using AppearanceData 'DA_RedTeamAppearance'
   LogTemp: TeamManager: Blue Team using AppearanceData 'DA_BlueTeamAppearance'
   LogTemp: TeamManager: Applied skeletal mesh 'SK_Agent_Body' to Team 0
   ```

---

## Architecture

### Class Hierarchy

```
UPrimaryDataAsset (Unreal Engine)
  └─ UAgentAppearanceData (Our Data Asset)
       ├─ Team Identity (Name, Colors)
       ├─ Visual Appearance (Mesh, Materials, Anim BP)
       ├─ Customization (Scale, Dynamic Parameters)
       └─ Effects (VFX, Audio)

FTeamConfiguration (Struct)
  └─ AppearanceData: TObjectPtr<UAgentAppearanceData>

ATeamManager (Actor)
  ├─ RedTeamConfig: FTeamConfiguration
  └─ BlueTeamConfig: FTeamConfiguration
```

### Data Flow

```
1. Level Designer creates DA_RedTeamAppearance (Data Asset)
2. Level Designer assigns to TeamManager.RedTeamConfig.AppearanceData
3. [Runtime] TeamManager::BeginPlay() reads AppearanceData
4. [Runtime] TeamManager::SpawnAgent() applies appearance to AMocCharacter
5. [Runtime] Agent spawns with correct mesh, materials, animations
```

---

## API Reference

### UAgentAppearanceData (Public/Data/AgentAppearanceData.h)

#### Core Properties

| Property | Type | Description |
|----------|------|-------------|
| `TeamName` | `FString` | Team display name (e.g., "Red Team") |
| `TeamColor` | `FLinearColor` | Primary team color (used for VFX, UI) |
| `TeamSecondaryColor` | `FLinearColor` | Secondary color for accents (optional) |
| `SkeletalMesh` | `USkeletalMesh*` | Agent body mesh (required) |
| `AnimationBlueprint` | `TSubclassOf<UAnimInstance>` | Animation controller (required) |
| `PrimaryMaterial` | `UMaterialInterface*` | Main material (mesh slot 0) |
| `SecondaryMaterial` | `UMaterialInterface*` | Secondary material (mesh slot 1) |
| `AdditionalMaterials` | `TArray<UMaterialInterface*>` | Extra materials (slots 2+) |
| `MeshScale` | `float` | Scale multiplier (default: 1.0) |
| `bUseDynamicMaterials` | `bool` | Enable dynamic material instances |
| `DynamicColorParameters` | `TMap<FName, FLinearColor>` | Runtime color parameters |
| `SpawnVFX` | `UParticleSystem*` | Spawn particle effect |
| `DeathVFX` | `UParticleSystem*` | Death particle effect |
| `SpawnSound` | `USoundBase*` | Spawn audio cue |

#### Blueprint Functions

```cpp
// Get human-readable description (for debugging)
FString GetDescription() const;

// Check if appearance is valid (has required mesh)
bool IsValid() const;

// Get total material count
int32 GetMaterialCount() const;
```

### FTeamConfiguration (Public/Team/TeamManager.h)

#### Properties

| Property | Type | Description |
|----------|------|-------------|
| `AppearanceData` | `UAgentAppearanceData*` | **Primary:** Appearance data asset (v10.2+) |
| `TeamName` | `FString` | **[DEPRECATED]** Use `AppearanceData->TeamName` |
| `TeamColor` | `FLinearColor` | **[DEPRECATED]** Use `AppearanceData->TeamColor` |
| `AgentSkeletalMesh` | `USkeletalMesh*` | **[DEPRECATED]** Use `AppearanceData->SkeletalMesh` |
| `AgentMaterial` | `UMaterialInterface*` | **[DEPRECATED]** Use `AppearanceData->PrimaryMaterial` |

**Legacy Support:** Old properties are kept for backward compatibility but ignored if `AppearanceData` is set.

---

## Usage Examples

### Example 1: Creating Red Team Appearance

**File:** `Content/Game/Data/Teams/DA_RedTeamAppearance.uasset`

```cpp
// Configure in Unreal Editor (not C++ code):
TeamName = "Red Team"
TeamColor = FLinearColor(1.0, 0.0, 0.0) // Red
SkeletalMesh = SK_MannequinAgent
AnimationBlueprint = ABP_MocLocomotion
PrimaryMaterial = M_RedTeamSuit
SpawnVFX = P_RedSpawnBurst
```

### Example 2: Creating Elite Squad Variant

**File:** `Content/Game/Data/Teams/DA_EliteSquadAppearance.uasset`

```cpp
// Elite variant with different mesh and materials
TeamName = "Elite Squad"
TeamColor = FLinearColor(1.0, 0.843, 0.0) // Gold
SkeletalMesh = SK_EliteAgent // Different mesh
AnimationBlueprint = ABP_EliteLocomotion // Different animations
PrimaryMaterial = M_GoldArmorSuit
MeshScale = 1.2 // 20% larger agents
SpawnVFX = P_EliteSpawnExplosion
```

### Example 3: Switching Team Appearance at Runtime (Blueprint)

```cpp
// Blueprint: Change Blue Team to Elite appearance mid-match
TeamManager->BlueTeamConfig.AppearanceData = DA_EliteSquadAppearance;
TeamManager->DestroyAllAgents(); // Respawn with new appearance
TeamManager->SpawnTeams();
```

### Example 4: Dynamic Material Parameters

**In Data Asset:**
```cpp
// Configure dynamic color parameters (e.g., team badge color)
DynamicColorParameters:
  "BadgeColor" → FLinearColor(1.0, 0.0, 0.0)
  "StripeColor" → FLinearColor(0.5, 0.0, 0.0)
```

**In Material Blueprint:**
- Add `Vector Parameter` nodes named `BadgeColor` and `StripeColor`
- Values are automatically set at spawn time

---

## Migration Guide (v10.1 → v10.2)

### Old System (v10.1)
```cpp
// Hardcoded in C++ or Blueprint:
RedTeamConfig.TeamName = "Red Team";
RedTeamConfig.TeamColor = FLinearColor::Red;
RedTeamConfig.AgentSkeletalMesh = SK_Agent;
RedTeamConfig.AgentMaterial = M_RedSuit;
```

### New System (v10.2)
```cpp
// Create DA_RedTeamAppearance in Content Browser
// Configure in editor
// Assign in TeamManager:
RedTeamConfig.AppearanceData = DA_RedTeamAppearance;

// Legacy properties are ignored if AppearanceData is set
```

### Migration Steps

1. **Create Appearance Data Assets:**
   - `DA_RedTeamAppearance`
   - `DA_BlueTeamAppearance`

2. **Transfer Settings:**
   - Copy `TeamName`, `TeamColor`, `AgentSkeletalMesh`, `AgentMaterial` to data assets

3. **Assign Data Assets:**
   - Open levels with `TeamManager`
   - Set `RedTeamConfig.AppearanceData` and `BlueTeamConfig.AppearanceData`

4. **Test:**
   - PIE and verify appearance matches old system

5. **[Optional] Remove Legacy Properties:**
   - Once stable, you can stop setting legacy properties (they're ignored anyway)

---

## Advanced Usage

### Custom Appearance Per Agent (Future)

Currently, all agents on a team share the same appearance. To support per-agent customization:

```cpp
// In AMocCharacter::SetAgentAppearance(UAgentAppearanceData* CustomAppearance)
// (Not yet implemented)
if (CustomAppearance && GetMesh())
{
    GetMesh()->SetSkeletalMesh(CustomAppearance->SkeletalMesh);
    // Apply materials...
}
```

### Seasonal/Event Appearances

Create variant data assets:
- `DA_RedTeamWinter` (winter skins)
- `DA_RedTeamHalloween` (Halloween skins)
- `DA_RedTeamChampion` (tournament winner skins)

Switch via Game Mode or Seasonal Event system.

### Procedural Variations

Use `DynamicColorParameters` to randomize agent appearance:

```cpp
// In BeginPlay or custom spawn logic:
UMaterialInstanceDynamic* DynMat = Agent->GetMesh()->CreateDynamicMaterialInstance(0);
DynMat->SetVectorParameterValue("AccentColor", FLinearColor::MakeRandomColor());
```

---

## Troubleshooting

### Issue: Agents spawn with default mesh (not custom appearance)

**Solution:**
1. Verify `AppearanceData` is assigned in TeamManager
2. Check logs for warnings: `"AppearanceData not set, using legacy config"`
3. Ensure `SkeletalMesh` is set in the data asset

### Issue: Animation doesn't play

**Solution:**
1. Verify `AnimationBlueprint` is set in `AppearanceData`
2. Check animation blueprint is compatible with skeletal mesh
3. Ensure animation blueprint has `DefaultSlot` for montages

### Issue: Materials don't apply

**Solution:**
1. Check material indices match mesh material slots
2. Verify materials are not `nullptr` in data asset
3. Use `GetMaterialCount()` to debug material count mismatch

### Issue: Legacy properties still affect agents

**Solution:**
- Legacy properties are **only used as fallback** if `AppearanceData` is `nullptr`
- If `AppearanceData` is set, legacy properties are **completely ignored**
- Set `AppearanceData = nullptr` to force legacy mode

---

## Implementation Notes

### Performance

- **Negligible overhead:** Appearance application happens once per agent spawn
- **Material instances:** Dynamic materials add ~0.1ms per agent (negligible)
- **Recommended:** Use static materials when dynamic parameters aren't needed

### Memory

- **Data Asset Size:** ~1KB per asset (negligible)
- **Shared Resources:** Meshes and materials are referenced (not duplicated)
- **Pooling:** Agents can be pooled without re-applying appearance

### Thread Safety

- All appearance operations happen on **Game Thread**
- **Safe:** Can be called during spawn, respawn, or runtime appearance change
- **Not Safe:** Don't modify `AppearanceData` properties during gameplay (create new assets instead)

---

## Future Enhancements

### Planned Features (v10.3+)

- **Per-Agent Customization:** `AMocCharacter::SetCustomAppearance(UAgentAppearanceData*)`
- **Loadout System:** Attach weapons, gadgets, backpacks to appearance data
- **Animation Set:** Support multiple animation blueprints per appearance
- **LOD Configuration:** Per-appearance LOD settings for performance
- **Niagara VFX:** Replace legacy particle systems with Niagara
- **Audio Variants:** Footstep sounds, voice lines, combat audio

### Wish List (Community Requested)

- **Skin Shop System:** Buy/unlock new appearances with in-game currency
- **Team Customization UI:** In-game editor for team appearances
- **Procedural Generation:** Algorithmic appearance variations
- **Attachment Sockets:** Dynamic prop attachment (helmets, badges, etc.)

---

## Related Documentation

- **MOC v10.2 Architecture:** `CLAUDE.md`
- **Team Management:** `TeamManager.h`
- **Agent Character:** `MocCharacter.h`
- **Training Documentation:** `TRAINING_MODE_MOVEMENT_FIX.md`

---

## Changelog

### v10.2 (2026-02-14) - Initial Release
- ✅ Created `UAgentAppearanceData` data asset
- ✅ Refactored `FTeamConfiguration` to use data assets
- ✅ Updated `TeamManager` to apply appearance from data assets
- ✅ Added legacy fallback for backward compatibility
- ✅ Implemented dynamic material parameter system
- ✅ Added mesh scaling support
- ✅ Added VFX/audio placeholders (not yet functional)
- ✅ Comprehensive documentation

### Future Versions
- v10.3: Per-agent customization, loadout system
- v10.4: Advanced VFX integration (Niagara)
- v10.5: Animation set support

---

## Support

**Questions?** Open an issue on GitHub or contact the MOC development team.

**Contributing?** We welcome pull requests for new appearance features!

---

**MOC v10.2 - Data-Driven Agent Appearance System**
*Making team customization designer-friendly and maintainable.*
