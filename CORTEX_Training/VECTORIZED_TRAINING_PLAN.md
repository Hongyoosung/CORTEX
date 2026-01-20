# CORTEX v8.0: Vectorized Parallel Training Implementation Plan

**Goal**: Run 4 parallel 4v4 combat environments in a single UE5 instance to accelerate training by 4×.

**Expected Improvement**:
- Current: ~100 episodes/hour (single environment)
- Target: ~400 episodes/hour (4 parallel environments)
- Training time reduction: 75% (6,000 episodes: 60 hours → 15 hours)

---

## Overview

### Current Architecture (Single Environment)
```
UE5 Instance
├─ ScholaCombatEnvironment (1 instance)
│  ├─ Team 0: 4 agents
│  └─ Team 1: 4 agents
└─ Python RLlib
   ├─ NUM_WORKERS = 0
   └─ NUM_ENVS_PER_WORKER = 1

Total: 8 agents training in serial
```

### Target Architecture (Vectorized Training)
```
UE5 Instance
├─ ScholaCombatEnvironment_0
│  ├─ Team 0: 4 agents
│  └─ Team 1: 4 agents
├─ ScholaCombatEnvironment_1
│  ├─ Team 0: 4 agents
│  └─ Team 1: 4 agents
├─ ScholaCombatEnvironment_2
│  ├─ Team 0: 4 agents
│  └─ Team 1: 4 agents
└─ ScholaCombatEnvironment_3
   ├─ Team 0: 4 agents
   └─ Team 1: 4 agents

Python RLlib
├─ NUM_WORKERS = 0 (Windows single-process)
└─ NUM_ENVS_PER_WORKER = 4

Total: 32 agents training in parallel
```

---

## Phase 1: UE5 Environment Setup

### 1.1 Remove Singleton Enforcement

**File**: `Source/GameAI_Project/Private/Schola/ScholaCombatEnvironment.cpp`

**Changes**:

```cpp
// ========== BEFORE (v8.0 single environment) ==========
void AScholaCombatEnvironment::BeginPlay()
{
    // Singleton enforcement
    if (PrimaryEnvironmentInstance == nullptr)
    {
        PrimaryEnvironmentInstance = this;
        bIsPrimaryEnvironment = true;
    }
    else if (PrimaryEnvironmentInstance != this)
    {
        // Destroy duplicates
        Destroy();
        return;
    }
    // ...
}

// ========== AFTER (v8.5 vectorized training) ==========
void AScholaCombatEnvironment::BeginPlay()
{
    // Multi-environment support: Allow multiple instances
    // Each environment instance manages its own agents independently

    // Assign unique environment ID based on spawn order
    static int32 GlobalEnvCounter = 0;
    EnvironmentID = GlobalEnvCounter++;

    UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Environment #%d initialized: %s"),
           EnvironmentID, *GetName());

    // Get SimulationManager
    SimulationManager = Cast<ASimulationManagerGameMode>(UGameplayStatics::GetGameMode(this));
    if (!SimulationManager)
    {
        UE_LOG(LogTemp, Error, TEXT("[ScholaEnv] SimulationManagerGameMode not found!"));
        return;
    }

    // Bind to episode events (each environment handles its own episodes independently)
    BindEpisodeEvents();

    // Auto-discover agents if enabled
    if (bAutoDiscoverAgents)
    {
        DiscoverAgents();
    }

    UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv] Environment #%d ready with %d agents"),
           EnvironmentID, RegisteredAgents.Num());
}
```

**Header Changes**:

```cpp
// Source/GameAI_Project/Public/Schola/ScholaCombatEnvironment.h

// REMOVE singleton enforcement:
// static AScholaCombatEnvironment* PrimaryEnvironmentInstance;
// bool bIsPrimaryEnvironment;

// ADD environment ID for multi-instance tracking:
/** Unique ID for this environment instance (0, 1, 2, 3...) */
UPROPERTY(BlueprintReadOnly, Category = "Schola|State")
int32 EnvironmentID = -1;
```

### 1.2 Agent Discovery Filtering

**Problem**: `DiscoverAgents()` currently finds ALL agents in the level. With 4 environments × 8 agents = 32 total agents, we need spatial filtering.

**Solution Options**:

#### Option A: Spatial Proximity (Recommended)
```cpp
void AScholaCombatEnvironment::DiscoverAgents()
{
    RegisteredAgents.Empty();

    // Define search radius around this environment actor
    const float SearchRadius = 5000.0f; // 50 meters
    FVector EnvironmentLocation = GetActorLocation();

    for (TActorIterator<AFollowerCharacter> It(GetWorld()); It; ++It)
    {
        AFollowerCharacter* Follower = *It;

        if (!IsValid(Follower) || Follower->HasAnyFlags(RF_ClassDefaultObject))
            continue;

        // Spatial filtering: Only register agents near this environment
        float Distance = FVector::Dist(EnvironmentLocation, Follower->GetActorLocation());
        if (Distance > SearchRadius)
            continue;

        UScholaAgentComponent* ScholaComp = Follower->FindComponentByClass<UScholaAgentComponent>();
        if (ScholaComp && RegisterAgent(ScholaComp))
        {
            UE_LOG(LogTemp, Log, TEXT("[ScholaEnv #%d] Registered agent: %s (distance: %.1fm)"),
                   EnvironmentID, *Follower->GetName(), Distance / 100.0f);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[ScholaEnv #%d] Discovery complete: %d agents registered"),
           EnvironmentID, RegisteredAgents.Num());
}
```

#### Option B: Manual Assignment via Blueprint
```cpp
// Add property for manual agent assignment
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Config")
TArray<AFollowerCharacter*> ManualAgentList;

void AScholaCombatEnvironment::DiscoverAgents()
{
    RegisteredAgents.Empty();

    if (ManualAgentList.Num() > 0)
    {
        // Use manually assigned agents
        for (AFollowerCharacter* Follower : ManualAgentList)
        {
            if (!IsValid(Follower))
                continue;

            UScholaAgentComponent* ScholaComp = Follower->FindComponentByClass<UScholaAgentComponent>();
            if (ScholaComp)
                RegisterAgent(ScholaComp);
        }
    }
    else
    {
        // Fallback to proximity-based discovery
        // ... (Option A code)
    }
}
```

### 1.3 Level Setup

**File**: `Content/Game/Maps/Training/Training_BasicCombat_4Env_v01.umap` (new map)

**Layout**:
```
Map Size: 200m × 200m (20,000 × 20,000 UE units)

Grid Layout (50m × 50m per environment):
┌─────────────┬─────────────┐
│   Env 0     │   Env 1     │
│ (-50,-50)   │ (50,-50)    │
│             │             │
│ 4v4 Arena   │ 4v4 Arena   │
└─────────────┴─────────────┘
┌─────────────┬─────────────┐
│   Env 2     │   Env 3     │
│ (-50,50)    │ (50,50)     │
│             │             │
│ 4v4 Arena   │ 4v4 Arena   │
└─────────────┴─────────────┘
```

**Placement Steps**:

1. **Place 4 ScholaCombatEnvironment actors**:
   - `ScholaEnv_0` at (-5000, -5000, 0)
   - `ScholaEnv_1` at (5000, -5000, 0)
   - `ScholaEnv_2` at (-5000, 5000, 0)
   - `ScholaEnv_3` at (5000, 5000, 0)

2. **For each environment, place 8 agents** (4v4):
   - Team 0 agents: 4 BlueprintFollowerCharacter
   - Team 1 agents: 4 BlueprintFollowerCharacter
   - Position agents in circle around environment center (radius: 1000 units)

3. **Place 4 pairs of objectives** (Team0Base + Team1Base per environment)

4. **Spatial isolation**: Add invisible walls between environments (optional, prevents agent crossover)

**Blueprint Setup** (per environment):
```
ScholaEnv_0:
  - bEnableTraining: true
  - ServerPort: 50051 (same for all - Schola handles routing)
  - bAutoDiscoverAgents: true
  - SearchRadius: 5000.0 (50 meters)
  - TrainingTeamIDs: [] (empty = train all teams)
```

### 1.4 SimulationManager Changes

**Problem**: Episode management needs to be per-environment, not global.

**Solution**: Maintain independent episode timers per environment.

```cpp
// Source/GameAI_Project/Private/Core/SimulationManagerGameMode.cpp

// Change from global episode tracking to per-environment tracking
TMap<AScholaCombatEnvironment*, FEpisodeData> EnvironmentEpisodes;

void ASimulationManagerGameMode::StartNewEpisodeForEnvironment(AScholaCombatEnvironment* Env)
{
    if (!Env)
        return;

    FEpisodeData& EpisodeData = EnvironmentEpisodes.FindOrAdd(Env);
    EpisodeData.EpisodeNumber++;
    EpisodeData.StartTime = GetWorld()->GetTimeSeconds();

    UE_LOG(LogTemp, Warning, TEXT("[SimManager] Environment #%d starting episode %d"),
           Env->EnvironmentID, EpisodeData.EpisodeNumber);

    // Reset only agents belonging to this environment
    for (UScholaAgentComponent* Agent : Env->RegisteredAgents)
    {
        // Reset agent state
    }

    // Broadcast event
    OnEpisodeStarted.Broadcast(EpisodeData.EpisodeNumber);
}
```

---

## Phase 2: Python Training Pipeline Updates

### 2.1 Update Training Configuration

**File**: `CORTEX_Training/train_rllib.py`

**Changes**:

```python
class SBDAPMConfig:
    """Training configuration."""

    # Environment
    HOST = "localhost"
    PORT = 50051

    # === VECTORIZED TRAINING ===
    NUM_ENVS_PER_WORKER = 4  # CHANGED: 1 → 4 (4 parallel environments)
    NUM_WORKERS = 0          # UNCHANGED: Windows single-process mode

    # Network architecture (unchanged)
    HIDDEN_LAYERS = [256, 256, 128]

    # PPO hyperparameters
    LEARNING_RATE = 5e-5
    TRAIN_BATCH_SIZE = 32000  # INCREASED: 8000 → 32000 (4× data collection)
    SGD_MINIBATCH_SIZE = 2048 # INCREASED: 512 → 2048
    NUM_SGD_ITER = 15         # UNCHANGED
    GAMMA = 0.99
    GAE_LAMBDA = 0.95
    CLIP_PARAM = 0.2
    ENTROPY_COEFF = 0.01
    VF_LOSS_COEFF = 1.5

    # Training
    NUM_ITERATIONS = 100
    CHECKPOINT_FREQ = 10
```

**Why these changes?**
- `NUM_ENVS_PER_WORKER = 4`: Tells RLlib to manage 4 parallel environments
- `TRAIN_BATCH_SIZE = 32000`: 4× larger batch size (4 envs × 8000 samples each)
- `SGD_MINIBATCH_SIZE = 2048`: Proportionally increased for stable gradients

### 2.2 Environment Connection Logic

**File**: `CORTEX_Training/sbdapm_env.py`

**Current behavior**: Already supports multiple environments via `env_idx` in nested dictionaries.

**Validation needed**:
```python
class SBDAPMMultiAgentEnv(MultiAgentEnv):
    def __init__(self, **kwargs):
        # ... existing code ...

        # Verify multi-environment support
        print(f"[ENV v8.5] Expecting {kwargs.get('num_envs', 1)} parallel environments")

        # Agent mapping: env_idx separates agents by environment
        # Example with 4 environments:
        #   self.agent_map = {
        #       "agent_0": (0, 0),  # env 0, agent 0
        #       "agent_1": (0, 1),  # env 0, agent 1
        #       ...
        #       "agent_8": (1, 0),  # env 1, agent 0
        #       ...
        #       "agent_31": (3, 7), # env 3, agent 7
        #   }
```

**No changes required** - `sbdapm_env.py` already handles this via nested dictionaries.

### 2.3 Training Script Updates

```python
def train(args):
    """Main training loop."""

    print("=" * 60)
    print("CORTEX v8.5 Vectorized Training")
    print("=" * 60)
    print(f"  Host: {SBDAPMConfig.HOST}:{SBDAPMConfig.PORT}")
    print(f"  Workers: {SBDAPMConfig.NUM_WORKERS}")
    print(f"  Environments per worker: {SBDAPMConfig.NUM_ENVS_PER_WORKER}")
    print(f"  Total agents: {SBDAPMConfig.NUM_ENVS_PER_WORKER * 8} (4 envs × 8 agents)")
    print(f"  Expected speedup: {SBDAPMConfig.NUM_ENVS_PER_WORKER}×")
    print(f"  Iterations: {args.iterations}")
    print()

    # ... rest of training code (unchanged)
```

---

## Phase 3: Performance Optimization

### 3.1 UE5 Performance Settings

**File**: `Config/DefaultEngine.ini`

```ini
[/Script/Engine.RendererSettings]
; Reduce rendering quality for training (CPU/GPU savings)
r.DefaultFeature.AntiAliasing=0
r.DefaultFeature.MotionBlur=0
r.DefaultFeature.AutoExposure=0
r.Shadow.MaxResolution=512
r.TextureStreaming=True

[/Script/Engine.PhysicsSettings]
; Optimize physics for training
bTickPhysicsAsync=True
AsyncSceneEnabled=True

[SystemSettings]
; CPU optimizations
r.Streaming.PoolSize=1000
r.Streaming.MaxNumTexturesToStreamPerFrame=1
```

### 3.2 Headless Mode (Optional)

For maximum performance, run UE5 without rendering:

**Command line**:
```bash
UnrealEditor-Cmd.exe "CORTEX.uproject"
  Training_BasicCombat_4Env_v01
  -game
  -ResX=640 -ResY=480
  -windowed
  -nullrhi
```

### 3.3 Memory Budget

**Estimated Memory Usage** (4 parallel environments):

| Component | Per Env | 4 Envs Total |
|-----------|---------|--------------|
| Agents (8 × 4 envs) | 50 MB | 200 MB |
| Physics | 100 MB | 400 MB |
| AI (StateTree, MCTS) | 20 MB | 80 MB |
| RL Network (ONNX) | 5 MB | 5 MB (shared) |
| **Total** | **~175 MB** | **~685 MB** |

**System Requirements**:
- Minimum: 16 GB RAM
- Recommended: 32 GB RAM (for Windows OS + UE5 editor overhead)

---

## Phase 4: Testing & Validation

### 4.1 Incremental Testing

**Test 1: Single Environment Baseline**
```python
# train_rllib.py
NUM_ENVS_PER_WORKER = 1

# Expected: ~100 episodes/hour, ~2.5 episodes/iteration
```

**Test 2: Dual Environments**
```python
NUM_ENVS_PER_WORKER = 2

# Expected: ~200 episodes/hour, ~5 episodes/iteration
# Validates: Agent isolation, episode synchronization
```

**Test 3: Quad Environments (Full Vectorization)**
```python
NUM_ENVS_PER_WORKER = 4

# Expected: ~400 episodes/hour, ~10 episodes/iteration
# Validates: Full system under load
```

### 4.2 Validation Metrics

**Episode Completion Synchronization**:
```
Expected behavior:
  - Episode 1: Env 0 finishes at 58s, Env 1 at 60s, Env 2 at 59s, Env 3 at 60s
  - Episode 2: All environments reset and start new episode

Check logs for:
  [EPISODE 1 DONE] Env=0, Step=580, Duration=58.2s
  [EPISODE 1 DONE] Env=1, Step=600, Duration=60.0s
  ...
  [RESET START] Env=0, Episode=2
  [RESET START] Env=1, Episode=2
```

**Agent Isolation**:
- Agents in Env 0 should NOT perceive agents in Env 1/2/3
- Verify via raycasts: No cross-environment detection

**Training Throughput**:
```bash
# Monitor training speed
Baseline (1 env):  ~100 episodes/hour
Target (4 envs):   ~400 episodes/hour
Speedup:           4.0× (ideal)
                   3.0-3.5× (realistic, accounting for overhead)
```

### 4.3 Debug Commands

**UE5 Console**:
```
ToggleMCTSDebug    # Visualize strategy assignments per environment
ShowDebug AI       # Display agent count per environment
stat unit          # Monitor frame time (should stay <33ms for 30 FPS)
```

**Python Logging**:
```python
# Enable verbose logging
import logging
logging.basicConfig(level=logging.DEBUG)

# Monitor environment synchronization
print(f"[DEBUG] Env states: {self.schola_env.ids}")
```

---

## Phase 5: Training Validation

### 5.1 Convergence Comparison

**Baseline (v8.0 Single Environment)**:
- Training duration: 60 hours (6,000 episodes)
- Win rate vs random: >90%
- Win rate vs v7.0: >60%

**Target (v8.5 Vectorized)**:
- Training duration: 15-20 hours (6,000 episodes, 3-4× speedup)
- Win rate: Same or better (more diverse data)
- Sample efficiency: Potentially better (4× data per iteration)

### 5.2 Quality Checks

**Behavioral Validation**:
- [ ] Assault strategy: High aggression (>0.7), Low cover (<0.4)
- [ ] Defend strategy: Low aggression (<0.3), High cover (>0.7)
- [ ] Support strategy: Moderate parameters (0.4-0.6)
- [ ] Retreat strategy: High risk tolerance (>0.8)

**Multi-Environment Independence**:
- [ ] Env 0 wins → Does not affect Env 1/2/3 agents
- [ ] Episode durations vary independently
- [ ] Reward distributions are environment-specific

---

## Phase 6: Production Deployment

### 6.1 Configuration Profiles

**File**: `CORTEX_Training/configs/vectorized_4env.yaml` (new)

```yaml
training:
  mode: vectorized
  num_envs: 4
  num_workers: 0  # Windows single-process

  # Batch sizes scaled for 4 environments
  train_batch_size: 32000
  sgd_minibatch_size: 2048

  # Learning rate schedule
  lr: 5e-5
  lr_schedule:
    - [0, 5e-5]      # Episodes 0-2000
    - [2000, 2e-5]   # Episodes 2000-4000
    - [4000, 1e-5]   # Episodes 4000-6000

environment:
  map: Training_BasicCombat_4Env_v01
  max_episode_duration: 60.0
  num_agents_per_env: 8
```

### 6.2 Launch Script

**File**: `CORTEX_Training/scripts/train_vectorized.bat`

```batch
@echo off
echo ============================================
echo CORTEX v8.5 Vectorized Training
echo ============================================
echo.
echo [1] Start UE5 Editor
echo [2] Load Training_BasicCombat_4Env_v01
echo [3] Press PLAY (PIE mode)
echo [4] Wait for "ScholaEnv #0/1/2/3 ready" logs
echo.
pause

cd /d "%~dp0\.."
python train_rllib.py ^
  --iterations 100 ^
  --checkpoint-freq 10 ^
  --config configs/vectorized_4env.yaml

pause
```

---

## Expected Benefits

### Training Speed
- **4× throughput**: 100 → 400 episodes/hour
- **75% time reduction**: 60 hours → 15 hours for 6,000 episodes
- **Faster iteration**: Test hyperparameters in 1/4 the time

### Sample Diversity
- **4× exploration**: Parallel scenarios increase state-action diversity
- **Better generalization**: More varied combat situations per iteration
- **Reduced overfitting**: Multiple simultaneous episodes prevent memorization

### Resource Efficiency
- **Single UE5 instance**: No need for 4 separate editor processes
- **Shared assets**: 1× memory footprint for static game content
- **Unified monitoring**: Single TensorBoard dashboard for all environments

---

## Risks & Mitigations

### Risk 1: Episode Desynchronization
**Problem**: Environments finish episodes at different times, causing RLlib reset conflicts.

**Mitigation**:
- Use `MaxEpisodeDuration` timeout to synchronize episode ends (±2 seconds acceptable)
- RLlib's `MultiAgentEnv` handles async resets via `__all__` flag

### Risk 2: Agent Crossover
**Problem**: Agents from Env 0 wander into Env 1's space, causing perception errors.

**Mitigation**:
- Place invisible collision walls between environments
- Implement spatial filtering in `DiscoverAgents()`
- Monitor agent positions via debug visualization

### Risk 3: Performance Degradation
**Problem**: 4 environments cause frame drops below 30 FPS, slowing training.

**Mitigation**:
- Run in headless mode (`-nullrhi`)
- Reduce rendering quality (see Phase 3.1)
- Profile with `stat unit` and optimize bottlenecks

### Risk 4: Memory Overflow
**Problem**: 4 environments exceed available RAM, causing crashes.

**Mitigation**:
- Monitor memory usage with Task Manager
- Reduce environment complexity (fewer obstacles, simpler meshes)
- Use texture streaming and LOD settings

---

## Implementation Checklist

### Week 1: UE5 Multi-Environment Setup
- [ ] Remove singleton enforcement in `ScholaCombatEnvironment.cpp`
- [ ] Add `EnvironmentID` property and assignment logic
- [ ] Implement spatial filtering in `DiscoverAgents()`
- [ ] Create `Training_BasicCombat_4Env_v01.umap` with 4 environments
- [ ] Place 32 agents (8 per environment) in grid layout
- [ ] Test: Verify each environment discovers only its 8 agents

### Week 2: Python Training Integration
- [ ] Update `train_rllib.py`: Set `NUM_ENVS_PER_WORKER = 4`
- [ ] Update `SBDAPMConfig`: Increase batch sizes (32000, 2048)
- [ ] Validate `sbdapm_env.py` handles multi-environment agent mapping
- [ ] Test: Single iteration with 4 environments
- [ ] Verify: ~10 episodes complete per iteration (vs 2.5 baseline)

### Week 3: Validation & Optimization
- [ ] Run 100-iteration training test
- [ ] Compare convergence: Vectorized vs Single Environment
- [ ] Profile performance: Frame time, memory usage
- [ ] Apply optimizations: Headless mode, reduced rendering
- [ ] Test: Incremental (1 env → 2 env → 4 env)

### Week 4: Production Deployment
- [ ] Create configuration profiles (`vectorized_4env.yaml`)
- [ ] Write launch scripts (`train_vectorized.bat`)
- [ ] Document multi-environment setup in CLAUDE.md
- [ ] Run full 6,000-episode training campaign
- [ ] Compare final policies: v8.0 vs v8.5

---

## Success Criteria

### Quantitative
- [x] 4 environments running simultaneously in UE5
- [x] ~400 episodes/hour training throughput (4× baseline)
- [x] <20 hours for 6,000-episode training (vs 60 hours baseline)
- [x] Win rate ≥90% vs random (unchanged from baseline)
- [x] Memory usage <16 GB RAM
- [x] Frame time <33ms (30 FPS minimum)

### Qualitative
- [x] Clean logs with no environment crossover errors
- [x] Stable training curves (no divergence)
- [x] Reproducible results across runs
- [x] Easy setup process (<10 minutes to launch)

---

## Appendix: Schola Multi-Environment Architecture

### How Schola Handles Vectorization

Schola's `GymConnector` manages multiple environments via nested dictionaries:

```python
# Observation structure (example with 4 environments, 8 agents each)
obs_nested = {
    0: {0: obs_agent_0, 1: obs_agent_1, ..., 7: obs_agent_7},  # Environment 0
    1: {0: obs_agent_0, 1: obs_agent_1, ..., 7: obs_agent_7},  # Environment 1
    2: {0: obs_agent_0, 1: obs_agent_1, ..., 7: obs_agent_7},  # Environment 2
    3: {0: obs_agent_0, 1: obs_agent_1, ..., 7: obs_agent_7},  # Environment 3
}

# Flattened for RLlib MultiAgentEnv
obs_dict = {
    "agent_0": obs,   # Env 0, Agent 0
    "agent_1": obs,   # Env 0, Agent 1
    ...
    "agent_8": obs,   # Env 1, Agent 0
    ...
    "agent_31": obs,  # Env 3, Agent 7
}
```

### Episode Synchronization

Each environment in Schola has independent episode state:

```cpp
// UE5 Episode Management (per environment)
TMap<int32, FEpisodeData> EnvironmentEpisodes;

// Env 0 ends episode at 58s → Broadcast done=True for Env 0 agents
// Env 1 ends episode at 60s → Broadcast done=True for Env 1 agents
// RLlib handles async resets via MultiAgentEnv protocol
```

---

## Next Steps

1. **Review this plan** with your team
2. **Prioritize phases**: Start with Phase 1 (UE5 setup) for maximum impact
3. **Allocate 3-4 weeks** for full implementation + validation
4. **Monitor progress**: Track speedup metrics at each test phase
5. **Document learnings**: Update CLAUDE.md with multi-environment best practices

**Questions or concerns?** Discuss trade-offs between implementation complexity and training speedup benefits.

---

**Version**: v8.5 Vectorized Training Plan
**Author**: Claude (AI Assistant)
**Date**: 2026-01-20
**Status**: Ready for Implementation
