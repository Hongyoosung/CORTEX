# Fix Agent Spawn Height Issue

## Problem
Agents spawn at Z=90.3 but NavMesh is at Z=5.0, causing movement validation to fail.

## Solution: Adjust Spawn Locations

### In UE5 Editor:
1. Open `Training_BasicCombat_2v2_v01.umap`
2. Select all `BP_FollowerAgent` actors in Outliner
3. Press `End` key to snap to floor **OR** manually set Z position:
   - Details Panel → Transform → Location Z = 5.0
4. Verify NavMesh coverage (Press `P` - agents should be on green overlay)
5. Save map

### Alternative: Enable Auto-Snap in Blueprint
1. Open `BP_FollowerAgent` blueprint
2. Select Root Component (CapsuleComponent)
3. Details Panel → Find on Ground:
   - ☑ Enable "Find Floor on Startup"
   - ☑ Enable "Snap to Floor"
4. Compile and save

### Verify Fix:
- PIE and check Output Log: Agent Z should be ~5.0, not 90.3
- Movement should work without "No reachable positions" errors
