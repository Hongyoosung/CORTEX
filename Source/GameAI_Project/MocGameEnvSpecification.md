
***

# **Section 0: Game Environment Specification**

## **0.1 Game Mode Overview**

**MOC Arena** is a 5v5 tactical domination shooter built in Unreal Engine 5.6, designed as a controlled testbed for hierarchical multi-agent reinforcement learning research. 

### Core Mechanics
- **Team Composition**: 5 agents per team (Red vs Blue)
- **Win Condition**: First team to reach **300 points** OR highest score after **600 seconds** (10 minutes)
- **Respawn System**: 5-second delay upon death, spawn at team base
- **Real-time Execution**: 60 FPS simulation, agents operate at 10Hz decision frequency

### Design Philosophy
The environment balances **academic rigor** (reproducible state spaces, controlled variables) with **tactical depth** (resource management, positional play, team coordination) to validate hierarchical RL strategies in a dynamic adversarial setting. 

***

## **0.2 Map Architecture**

### Spatial Layout
- **Map Size**: 150m × 150m (15,000 × 15,000 UE5 units)
- **Topology**: Symmetrical tri-lane design inspired by MOBA/tactical shooter hybrids
- **NavMesh Coverage**: 85% traversable area (excluding walls, buildings, water hazards)

### Capture Points (5 Total)
Capture points are cylindrical zones (radius: 5m, height: 3m) that grant territory control and passive score generation.

| Point ID | Name | Initial Owner | Strategic Value |
|----------|------|---------------|-----------------|
| **Point A** | Red Base | Red Team | Spawn protection zone|
| **Point B** | North Outpost | Neutral | High ground advantage (+10% accuracy)  |
| **Point C** | Center Plaza | Neutral | Maximum resource density |
| **Point D** | South Outpost | Neutral | Cover-rich defensive position  [oreateai]|
| **Point E** | Blue Base | Blue Team | Spawn protection zone |

### Capture Mechanics
- **Capture Time**: **20 seconds** of continuous majority presence
- **Contestation**: If both teams present, capture progress **pauses** (does not regress)
- **Progress Decay**: If capturing team fully withdraws, progress decays at 50%/second
- **Scoring**: 
  - Neutral → Owned: **+25 points** (one-time)
  - Owned point generates **+1 point/second** (passive income)
  - Enemy captures your point: **-25 points** (immediate penalty)

**Example Scenario**: Red team holds B, C, D (3 points) → generates **+3 points/second** passive income, creating strategic pressure on Blue team to contest. 

***

## **0.3 Agent Specification**

### Character Attributes
```cpp
struct FAgentStats {
    // Combat
    float MaxHealth = 100.0f;
    float MovementSpeed = 600.0f;  // UE5 units/second (~6 m/s)
    float SprintSpeed = 900.0f;    // 1.5x multiplier
    
    // Weapon System (Hitscan rifle)
    float FireRate = 0.15f;        // 6.67 rounds/second
    float Damage = 15.0f;          // 7 shots to kill at full health
    float Accuracy = 0.85f;        // Base hit probability
    float EffectiveRange = 5000.0f; // 50m optimal range
    
    // Perception
    float VisionRange = 8000.0f;    // 80m sight radius
    float VisionAngle = 90.0f;      // 90-degree FOV cone
    float HearingRange = 3000.0f;   // 30m audio detection
};
```

### Action Space
Agents execute **hybrid actions** combining:
1. **Auto-battle**: Automatically aim and fire when enemies are detected within sight and range.
2. **EQS-Driven**: Tactical positioning (generated from 8-10 dim weights) 

***

## **0.4 Interactive Elements**

### Health Packs
- **Spawn Locations**: 12 fixed positions across map (4 near each neutral point, 2 per lane)
- **Heal Amount**: +40 HP (instant)
- **Respawn Timer**: 30 seconds after pickup
- **Visual Indicator**: Green holographic cross, visible through walls within 20m

### Ammo Crates
- **Spawn Locations**: 8 fixed positions (concentrated near capture points)
- **Ammo Refill**: +60 rounds (magazine capacity: 30 rounds, max reserve: 120)
- **Respawn Timer**: 20 seconds
- **Visual Indicator**: Orange holographic box

### Strategic Implications
- **Resource Control**: Teams controlling Center Plaza (Point C) gain access to 3 health packs + 2 ammo crates within 15m 
- **Scout Strategy**: High-value pickups become priority targets for Scout role to deny enemy resources 
- **Retreat Strategy**: Knowledge of nearest health pack location critical for low-HP agents

***

## **0.5 Observation Space & Information Sharing**

### Individual Agent Observation (52-dim Base State)
Each agent receives **partial observations** encoded as:

```python
observation = {
    # Self State (7-dim)
    'self_health': float,          # Normalized 0-1
    'self_ammo': float,            # Normalized 0-1
    'self_position': (x, y, z),    # World coordinates
    'self_velocity': float,         # Current speed
    
    # Allies (4 × 8-dim = 32-dim, sorted by proximity)
    'ally_positions': [(x, y, z), ...],     # Relative positions
    'ally_health': [float, ...],
    'ally_active_strategy': [one_hot_5dim, ...],  # strategy awareness
    'ally_last_seen': [float, ...],         # Seconds since observation
    
    # Enemies (5 × 5-dim = 25-dim, only if visible)
    'enemy_positions': [(x, y, z), ...],    # Relative positions (or NULL)
    'enemy_health': [float, ...],           # Estimated from damage dealt
    'enemy_last_seen': [float, ...],        # Fog of war timer
    
    # Objectives (5 × 3-dim = 15-dim)
    'point_ownership': [red/neutral/blue, ...],
    'point_capture_progress': [0-1, ...],
    'point_contested': [bool, ...],
    
    # Resources (20-dim)
    'visible_healthpacks': [(x, y, z), available?, ...],  # Max 4 tracked
    'visible_ammocrates': [(x, y, z), available?, ...],   # Max 4 tracked
}
```

### Information Sharing Protocol
**Centralized Training, Decentralized Execution (CTDE)** with **communication layer**:

#### Training Phase (Full Observability)
All agents access **privileged information**:
- Complete enemy positions, health, strategies (omniscient view)
- Ground truth capture progress for all 5 points
- Enables Value Network to learn global state evaluation

#### Execution Phase (Partial Observability)
Agents rely on **vision-based observations** + **team communication**:

```cpp
struct FSharedTeamKnowledge {
    // Broadcasted every 0.5 seconds
    TArray<FVector> LastKnownEnemyPositions;  // Max age: 5 seconds
    TArray<EStrategyType> AllyActiveStrategies; // Real-time
    TArray<bool> AllyNeedsSupport;              // HP < 30% flag
    FVector SuggestedRallyPoint;                // Vote-based consensus
};
```

**Communication Rules**: 
1. **Ally Information**: **Perfect sharing** (instantaneous, no delay) - simulates voice comms
2. **Enemy Information**: **Vision-dependent** with **5-second memory decay** - fog of war
3. **Objective Status**: **Globally visible** (UI assumption) - all agents know capture progress
4. **Resource Status**: **Shared only if ally within 30m** of pickup - requires proximity

### Observability Design Rationale
- **Partial Enemy Observation**: Forces Scout strategy to provide value through information gathering 
- **Perfect Ally Awareness**: Enables coordination without explicit communication channels (simplification for Phase 1) 
- **Decay Mechanics**: Punishes static strategies, rewards Scout roles that maintain map pressure 

***

## **0.6 Reward Structure**

### Individual Agent Rewards (Per-Step)
Agents receive **strategy-conditioned rewards** during training: 

| Event | Assault | Defend | Support | Scout | Retreat |
|-------|---------|--------|--------|---------|-------|---------|
| Enemy Kill | **+10** | +5 | +3 | +5 | -2 |
| Assist (damage) | +3 | +2 | +4 | +2 | 0 |
| Death | -20 | -15 | -10 | -8 | **-30** |
| Capture Point | +15 | **+20** | +10 | +5 | 0 |
| Lose Point | -25  | **-30** | -15 | -5 | 0 |
| Heal Ally (40+ HP) | 0 | +3 | **+12** | 0 | 0 |
| Reveal Enemy (first sight) | +2 | +1 | +1 | **+7** | 0 |
| Pickup Deny (take before enemy) | +1 | +1 | +2 | **+5** | 0 |
| Survive at <30% HP (per 5s) | -5 | -3 | -2 | 0 | +1 | **+20** |
| Distance to assigned target | -0.01/m | -0.01/m | -0.015/m | -0.03/m | N/A |


| Strategy | Primary Reward              | Secondary Reward            | Penalty                       |
| -------- | --------------------------- | --------------------------- | ----------------------------- |
| Assault  | Enemy kills (+10)           | Objective capture (+15)     | Death (-20) arxiv​            |
| Defend   | Objective retention (+8)    | Ally survival bonus (+3)    | Objective loss (-25)          |
| Support  | Ally HP healed (+0.02/pt)   | Team fight win assist (+12) | Ally death nearby (-8) arxiv​ |
| Scout    | Fog reveal area (+0.1/tile) | Enemy detection (+7)        | Detection by enemy (-10) mun​ |
| Retreat  | Survival when HP<30% (+20)  | Successful regroup (+6)     | Death while retreating (-15)  |


### Team-Level Rewards (Shared)
All agents receive bonus for:
- **Match Victory**: +100 (distributed equally)
- **Strategy Diversity Bonus**: +2 per step if ≥3 distinct strategies active 
- **Objective Majority**: +0.5 per step if controlling ≥3 points 

### Sparse vs Dense Rewards
- **Sparse**: Kills, captures, match outcome (training stability in late-game)
- **Dense**: Distance shaping, damage dealt, time alive (early exploration)
- **Curriculum**: Phase 1a uses dense rewards → Phase 1b transitions to sparse (alleviates reward hacking)

***

## **0.7 Episode Termination Conditions**

An episode ends when:
1. **Score Victory**: Any team reaches **300 points**
2. **Timeout**: 600 seconds (10 minutes) elapsed → higher score wins (tie = draw)
3. **Team Elimination**: All 5 agents dead simultaneously (rare due to staggered spawns)

### Episode Statistics Logged
```cpp
struct FEpisodeMetrics {
    int32 WinningTeam;                     // 0=Red, 1=Blue, -1=Draw
    float Duration;                        // Seconds
    TMap<EStrategyType, int32> StrategyUsageCounts;  // Per team
    float AvgTeamHealthAtEnd;
    int32 TotalPointCaptures;
    int32 TotalKills;
    TArray<float> PointControlTimeline;    // Control % over time
};
```

***

## **0.8 Technical Implementation Details**

### UE5 Integration
```cpp
// Core game loop
void AMocGameMode::Tick(float DeltaTime) {
    // 1. Update capture point states
    for (ACapturePoint* Point : CapturePoints) {
        Point->UpdateCaptureProgress(DeltaTime);
        if (Point->CaptureCompleted()) {
            OnPointCaptured(Point->GetOwningTeam());
        }
    }
    
    // 2. Respawn resource pickups
    ResourceManager->UpdateSpawnTimers(DeltaTime);
    
    // 3. Broadcast shared team knowledge
    if (TimeSinceLastBroadcast >= 0.5f) {
        RedTeam->UpdateSharedKnowledge();
        BlueTeam->UpdateSharedKnowledge();
        TimeSinceLastBroadcast = 0.0f;
    }
    
    // 4. Check win conditions
    if (RedScore >= 300 || BlueScore >= 300 || MatchTimer >= 600.0f) {
        EndMatch();
    }
}
```

### Performance Budget
- **Per-Agent AI Cost**: 15ms~ per frame (MCTS + RL inference)
- **Environment Update**: 1.5ms per frame (capture points, resources, spawns)
- **Total Frame Budget**: 16.67ms (60 FPS target)
- **Headroom**: ~7ms for rendering/physics

***

## **0.9 Validation Environment**

### Benchmark Scenarios (for Ablation Studies)
1. **Symmetric Start**: Both teams spawn, all points neutral → tests balanced play
2. **Comeback Challenge**: Red team starts with 0 points, Blue with 150 → tests adaptation
3. **Resource Scarcity**: Only 4 health packs spawn (vs 12 default) → tests Retreat/Support value
4. **Fog Stress Test**: Vision range reduced to 40m → tests Scout strategy necessity

### Comparison Baselines
- **Scripted FSM**: Hand-coded 3-state machine (Attack nearest → Defend if losing → Retreat if low HP)
- **Single-Head PPO**: Standard RL without strategy specialization (v9.0 baseline)
- **Random Policy**: Uniform action sampling (sanity check)

***
