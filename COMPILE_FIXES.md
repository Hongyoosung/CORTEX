# How to Apply Deadlock Fixes

## Summary
The deadlock requires fixes on **BOTH** Python and UE5 sides:
- **Python:** Wrong AutoResetType + incomplete reset() logic
- **UE5:** Missing OnEpisodeStarted broadcast after episode reset

## Step 1: Python Fixes (Already Applied ✓)

The following changes have been made to `CORTEX_Training/sbdapm_env.py`:
1. Line 86: Changed `AutoResetType.SAME_STEP` → `NEXT_STEP`
2. Lines 319-356: New reset() logic that sends dummy actions before polling
3. Lines 379-384: Poll duration monitoring

**No action needed** - Python fixes are complete.

## Step 2: UE5 Fixes (Requires Recompile)

### Changed File
`Source/GameAI_Project/Private/Core/SimulationManagerGameMode.cpp` (Lines 1453-1458)

### What Changed
Added OnEpisodeStarted broadcast at the end of `StartNewEpisode()`:
```cpp
// 🔥 CRITICAL FIX: Broadcast OnEpisodeStarted so agents send new observations to Python
// Without this, Python's poll() after reset() blocks indefinitely waiting for observations
UE_LOG(LogTemp, Warning, TEXT("[EPISODE START] Broadcasting OnEpisodeStarted(EnvID=%d, Episode=%d)..."),
    EnvironmentID, EnvironmentEpisodeNumber);
OnEpisodeStarted.Broadcast(EnvironmentID, EnvironmentEpisodeNumber);
UE_LOG(LogTemp, Warning, TEXT("[EPISODE START] OnEpisodeStarted.Broadcast() completed - agents can now send observations"));
```

### How to Compile

#### Option A: Full Rebuild (Recommended)
```bash
# 1. Close Unreal Editor if open
# 2. Delete intermediate build files
rmdir /s /q Intermediate
rmdir /s /q Binaries
rmdir /s /q .vs

# 3. Regenerate project files
"C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" -projectfiles -project="C:\Users\Foryoucom\Documents\GitHub\CORTEX\CORTEX.uproject" -game -rocket -progress

# 4. Open CORTEX.sln in Visual Studio
# 5. Build > Rebuild Solution (Development Editor configuration)
```

#### Option B: Quick Compile (Faster)
```bash
# 1. Open CORTEX.sln in Visual Studio
# 2. Locate SimulationManagerGameMode.cpp in Solution Explorer
# 3. Right-click on GameAI_Project project → Build
# 4. Wait for compilation to complete (~1-2 minutes)
```

#### Option C: Use Unreal Editor Live Coding (Fastest for C++ Changes)
```bash
# 1. Open UE5 Editor with CORTEX project
# 2. Make sure Live Coding is enabled (default in UE5)
# 3. Press Ctrl+Alt+F11 (or click "Live Coding" button in editor toolbar)
# 4. Editor will hot-reload the changes without restarting
```

### Verification After Compile

1. Open UE5 Editor
2. Play in Editor (PIE)
3. Check Output Log for this message after episode ends:
   ```
   [EPISODE START] Broadcasting OnEpisodeStarted(EnvID=0, Episode=1)...
   [EPISODE START] OnEpisodeStarted.Broadcast() completed - agents can now send observations
   ```

If you see these logs, the fix is active ✓

## Step 3: Test Training

1. Start UE5 with the compiled changes
2. Start Play in Editor
3. Run Python training:
   ```bash
   cd CORTEX_Training
   python train_rllib.py --iterations 10
   ```

### Expected Results

✅ **Success indicators:**
- First episode completes without hanging
- Step 1000 passes smoothly (no 20s freeze)
- Episodes restart immediately with logs:
  ```
  [SCHOLA RESET] ResetEnvironment() called...
  RESET: Sending dummy actions to trigger new episode...
  RESET: Polling for new episode data...
  RESET: New episode observations received, Agents=32
  ```
- No "WARNING: poll() took >5s" messages
- Training continues smoothly across episodes

❌ **Failure indicators:**
- 20-second freeze still occurs at step 1000
- "WARNING: poll() took >5s" messages
- Deadlock (training stops responding)
- UE5 logs show `[SCHOLA RESET]` but no `[EPISODE START] Broadcasting`

## Troubleshooting

### Issue: Still seeing deadlock after UE5 recompile
**Check:** Did OnEpisodeStarted.Broadcast() get added to `StartNewEpisode()`?
```bash
# Search for the fix in compiled code:
findstr /s /i "OnEpisodeStarted.Broadcast" Source\GameAI_Project\Private\Core\SimulationManagerGameMode.cpp
```
**Expected output:**
```
OnEpisodeStarted.Broadcast(EnvironmentID, EnvironmentEpisodeNumber);
```

### Issue: Compilation errors
**Solution:** Make sure you're editing the correct file:
- Path: `Source/GameAI_Project/Private/Core/SimulationManagerGameMode.cpp`
- Function: `void ASimulationManagerGameMode::StartNewEpisode(int32 EnvironmentID, int32 EnvironmentEpisodeNumber)`
- Location: After the agent count verification log, before closing brace `}`

### Issue: UE5 logs don't show new episode start
**Check:** Is the simulation actually calling `StartNewEpisode()`?
```
# Look for this in UE5 output log:
[StartNewEpisode] Called (EnvID: 0, Episode: 1)
```
If missing, the reset chain is broken earlier.

## What These Fixes Do

### Python Fix
- **Before:** reset() returned dummy observations, next poll() blocked waiting for UE5
- **After:** reset() sends dummy actions → polls for actual new episode data from UE5

### UE5 Fix
- **Before:** StartNewEpisode() reset agents but didn't notify trainers
- **After:** StartNewEpisode() resets agents AND broadcasts event → trainers send observations

### Together
```
Python reset() → UE5 ResetEnvironment() → StartNewEpisode() → OnEpisodeStarted.Broadcast()
                                                                        ↓
                                                               Trainers send observations
                                                                        ↓
                                                               Python poll() receives data
                                                                        ↓
                                                                Training continues ✓
```

## Rollback Plan

If issues persist after both fixes:

1. **UE5 Rollback:**
   ```bash
   git checkout HEAD -- Source/GameAI_Project/Private/Core/SimulationManagerGameMode.cpp
   # Recompile
   ```

2. **Python Rollback:**
   ```bash
   git checkout HEAD -- CORTEX_Training/sbdapm_env.py
   ```

3. **Alternative approach:**
   Use `hard_reset()` on every reset (slower but more reliable):
   - Edit `sbdapm_env.py` line 268: change `if is_first:` to `if True:`
   - This forces full reset every time (~2-5s per reset vs <1s)

## Contact

If deadlock persists after applying both fixes, provide:
1. UE5 Output Log (last 200 lines before freeze)
2. Python training output (last 50 lines before freeze)
3. Confirmation both fixes were applied (check logs for keywords)
