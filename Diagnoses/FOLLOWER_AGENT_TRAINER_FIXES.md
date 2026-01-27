# FollowerAgentTrainer Multi-Actor Architecture Fixes

## Summary

Updated `FollowerAgentTrainer` to work with the multi-actor Schola environment architecture.

---

## Changes Made

### **File: `FollowerAgentTrainer.cpp`**

#### **1. Removed `GetLogicalEnvironmentID()` Calls**

**Before (Single-Actor Architecture):**
```cpp
// Step 1: Get agent's TeamID
int32 InTeamID = -1;
if (FollowerAgent && FollowerAgent->TeamLeader)
{
    InTeamID = FollowerAgent->TeamLeader->TeamID;
}

// Step 2: Map TeamID to LogicalEnvironmentID (Teams 0,1→Env0 | Teams 2,3→Env1)
int32 EnvironmentID = Env->GetLogicalEnvironmentID(InTeamID);  // ← OLD METHOD
```

**After (Multi-Actor Architecture):**
```cpp
// Multi-actor architecture: Use the environment actor's EnvId directly
// Each actor manages its own environment, so EnvId IS the environment ID
int32 EnvironmentID = Env->EnvId;  // ← Simpler! No mapping needed

// Optional: Get TeamID for logging purposes
int32 InTeamID = -1;
if (FollowerAgent && FollowerAgent->TeamLeader)
{
    InTeamID = FollowerAgent->TeamLeader->TeamID;
}
```

**Why the change?**
- In multi-actor architecture, each `AScholaCombatEnvironment` actor IS a physical environment
- Schola assigns `EnvId` (0, 1, 2, 3) to each actor based on discovery order
- No need to calculate or map team IDs to logical environments

---

#### **2. Updated `GetInfo()` Method**

**Before:**
```cpp
// Pass logical environment ID to Python
int32 LogicalEnvID = -1;
if (ScholaAgent && ScholaAgent->ScholaEnvironment)
{
    AScholaCombatEnvironment* Env = Cast<AScholaCombatEnvironment>(ScholaAgent->ScholaEnvironment);
    if (Env && FollowerAgent && FollowerAgent->TeamLeader)
    {
        int32 InTeamID = FollowerAgent->TeamLeader->TeamID;
        LogicalEnvID = Env->GetLogicalEnvironmentID(InTeamID);  // ← OLD METHOD
    }
}
Info.Add(TEXT("logical_env_id"), FString::FromInt(LogicalEnvID));
```

**After:**
```cpp
// Multi-actor architecture: Pass environment ID to Python
// Each actor IS a physical environment, so we use the actor's EnvId
int32 EnvironmentID = -1;
if (ScholaAgent && ScholaAgent->ScholaEnvironment)
{
    AScholaCombatEnvironment* Env = Cast<AScholaCombatEnvironment>(ScholaAgent->ScholaEnvironment);
    if (Env)
    {
        EnvironmentID = Env->EnvId;  // ← Use EnvId directly
    }
}
Info.Add(TEXT("environment_id"), FString::FromInt(EnvironmentID));

// Also include team ID for debugging
if (FollowerAgent && FollowerAgent->TeamLeader)
{
    Info.Add(TEXT("team_id"), FString::FromInt(FollowerAgent->TeamLeader->TeamID));
}
```

**Why the change?**
- Python now receives `environment_id` (0-3) that matches Schola's environment indices
- Added `team_id` for debugging/logging purposes
- No complex mapping logic needed

---

#### **3. Enhanced Logging**

**Before:**
```cpp
UE_LOG(LogTemp, Warning, TEXT("[ENV %d TERMINATION] %s: Episode TRUNCATED"),
    EnvironmentID, *TrainerConfiguration.Name);
```

**After:**
```cpp
UE_LOG(LogTemp, Warning, TEXT("[ENV %d | Team %d] %s: Episode TRUNCATED"),
    EnvironmentID, InTeamID, *TrainerConfiguration.Name);
```

**Why the change?**
- Logs now show both Environment ID (0-3) and Team ID (0-7)
- Makes debugging easier - you can see which environment AND which team
- Example: `[ENV 0 | Team 1] FollowerAgent_0: Episode COMPLETED`

---

#### **4. Updated Comments**

**Before:**
```cpp
// v8.5 VECTORIZED TRAINING: Per-environment episode termination
// Check if THIS AGENT'S ENVIRONMENT has finished, not all environments
```

**After:**
```cpp
// Multi-actor architecture: Each environment (actor) terminates independently
// Check if THIS actor's environment has finished
```

**Why the change?**
- Removed confusing "v8.5 VECTORIZED TRAINING" terminology
- Clarified that we're using multi-actor architecture, not logical environments
- Simpler, more direct explanation

---

## How It Works Now

### **Episode Termination Flow:**

```
1. SimulationManager detects team elimination in Team 1
   ↓
2. SimulationManager sets termination flags for Environment 0
   (because Actor 0 manages Teams [0, 1])
   ↓
3. FollowerAgentTrainer for agents in Teams 0 and 1:
   - Gets EnvironmentID = Env->EnvId  (= 0)
   - Checks SimManager->IsEnvironmentEpisodeEnding(0)  ← Returns true
   - Returns EAgentTrainingStatus::Completed
   ↓
4. Schola sends done=True for all agents in Environment 0
   ↓
5. Python receives termination signal for Environment 0 only
   - Environments 1, 2, 3 continue running
```

### **Environment ID Mapping:**

| Actor Name | EnvId (Schola) | Manages Teams | Agent IDs |
|------------|----------------|---------------|-----------|
| ScholaEnv_0_Teams_0_1 | 0 | [0, 1] | 0-7 |
| ScholaEnv_1_Teams_2_3 | 1 | [2, 3] | 8-15 |
| ScholaEnv_2_Teams_4_5 | 2 | [4, 5] | 16-23 |
| ScholaEnv_3_Teams_6_7 | 3 | [6, 7] | 24-31 |

**Key Point:** `EnvId` is assigned by Schola based on actor discovery order, not calculated from team IDs.

---

## Benefits

✅ **Simpler Code:**
- No complex team-to-environment mapping logic
- Directly use `Env->EnvId` assigned by Schola

✅ **Clearer Logs:**
- Shows both Environment ID and Team ID
- Example: `[ENV 2 | Team 5] FollowerAgent_20: Episode COMPLETED`

✅ **Correct Termination:**
- Each environment terminates independently
- Uses Schola's assigned `EnvId` (0-3)

✅ **Debugging Info:**
- Python receives both `environment_id` and `team_id` in info dict
- Easier to track which agents belong to which environment

---

## Testing

After compiling, verify in UE5 logs:

```
[ENV 0 | Team 0] FollowerAgent_0: Status=Running (step 100)
[ENV 0 | Team 1] FollowerAgent_4: Status=Running (step 100)
[ENV 1 | Team 2] FollowerAgent_8: Status=Running (step 100)
[ENV 1 | Team 3] FollowerAgent_12: Status=Running (step 100)

... (Team 1 eliminated) ...

[ENV 0 | Team 0] FollowerAgent_0: Episode COMPLETED (team elim)
[ENV 0 | Team 1] FollowerAgent_4: Episode COMPLETED (team elim)

← Environment 1, 2, 3 continue running!
```

---

## Files Changed

```
Modified:
  ✓ Source/GameAI_Project/Private/Schola/FollowerAgentTrainer.cpp
    - Removed GetLogicalEnvironmentID() calls
    - Simplified to use Env->EnvId directly
    - Enhanced logging with both EnvId and TeamId
    - Updated comments to reflect multi-actor architecture
```

---

## Summary

`FollowerAgentTrainer` now correctly:
1. Uses `Env->EnvId` assigned by Schola (not calculated from TeamID)
2. Reports `environment_id` and `team_id` to Python
3. Checks termination flags for the correct environment
4. Logs both Environment ID and Team ID for debugging

The trainer is now fully compatible with the multi-actor Schola environment architecture.
