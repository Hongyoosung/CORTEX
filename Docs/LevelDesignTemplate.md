# SBDAPM Level Design Template
**Version:** v3.0 | **Date:** 2025-11-27

---

## Table of Contents
1. [Level Categories](#level-categories)
2. [Required Actors & Components](#required-actors--components)
3. [Naming Conventions](#naming-conventions)
4. [Blueprint Setup Guide](#blueprint-setup-guide)
5. [Curriculum Level Specifications](#curriculum-level-specifications)
6. [Evaluation Level Specifications](#evaluation-level-specifications)
7. [Testing Checklist](#testing-checklist)

---

## Level Categories

### 1. Training Levels (Self-Play)
**Purpose:** Agent learning via self-play competition
**Path:** `Content/Maps/Training/`
**Config:** Symmetrical layout, balanced spawns, collect experiences

### 2. Evaluation Levels (Baseline Comparison)
**Purpose:** Benchmark CORTEX against baseline AIs
**Path:** `Content/Maps/Evaluation/`
**Config:** Varied terrain, objective types, no experience collection

### 3. Demonstration Levels (Showcase)
**Purpose:** Video capture, presentations, debugging
**Path:** `Content/Maps/Demo/`
**Config:** Cinematic cameras, debug visualization enabled

---

## Required Actors & Components

### Core Actors (Every Level Must Have)

#### 1. **Game Mode Override**
- **Class:** `ASimulationManagerGameMode` or custom derived class
- **Location:** World Settings → GameMode Override
- **Purpose:** Team registration, win/loss detection, experiment tracking

#### 2. **Team Leader Actors** (2 per level)
- **Blueprint:** `BP_TeamLeader_Alpha` and `BP_TeamLeader_Bravo`
- **Components Required:**
  - `UTeamLeaderComponent`
  - Tags: `TeamLeader`, `TeamAlpha` or `TeamBravo`
- **Configuration:**
  ```
  TeamLeaderComponent:
    - TeamName: "Alpha Team" / "Bravo Team"
    - TeamColor: Blue / Red
    - MaxFollowers: 4
    - MCTSSimulations: 500
    - bAsyncMCTS: true
    - bContinuousPlanning: true
    - ContinuousPlanningInterval: 1.5s
    - bEnableDebugDrawing: false (true for demo levels)
  ```

#### 3. **AI Agent Actors** (8 total: 4 per team)
- **Blueprint:** `BP_AIAgent` (duplicated 8 times)
- **Components Required:**
  - `UFollowerAgentComponent`
  - `UHealthComponent`
  - `UWeaponComponent`
  - `UAgentPerceptionComponent`
  - `URewardCalculator`
  - `URLPolicyNetwork`
  - `AAIController`
- **Configuration:**
  ```
  FollowerAgentComponent:
    - TeamLeaderTag: TeamAlpha / TeamBravo
    - bAutoRegisterWithLeader: true
    - bUseRLPolicy: true
    - bCollectExperiences: true (training) / false (evaluation)
    - bEnableDebugDrawing: false

  HealthComponent:
    - MaxHealth: 100.0
    - bRegenerationEnabled: false

  WeaponComponent:
    - Damage: 25.0
    - FireRate: 3.0
    - Range: 5000.0
    - Accuracy: 0.85

  RewardCalculator:
    - IndividualRewardWeight: 1.0
    - CoordinationRewardWeight: 1.0
    - ObjectiveRewardWeight: 1.0
  ```

#### 4. **Spawn Points** (2 zones)
- **Actor:** `PlayerStart` or custom `BP_SpawnZone`
- **Tags:** `TeamASpawn`, `TeamBSpawn`
- **Layout:** Opposite corners/sides (symmetrical for training)
- **Radius:** 500cm spacing between spawn points in same zone

#### 5. **Objective Markers** (varies by level type)
- **Capture Zones:**
  - Actor: `BP_CaptureZone` or generic actor with trigger volume
  - Tag: `CaptureZone`
  - Radius: 1000-3000cm (configurable)
  - Visual: Sphere/cylinder marker

- **Defend Zones:**
  - Actor: Generic actor
  - Tag: `DefendZone`
  - Radius: 1500cm

- **Rescue Targets:**
  - Actor: Wounded agent actor
  - Tag: `RescueTarget`
  - Health: <50% max health

#### 6. **Cover Elements** (Optional but Recommended)
- **Walls:** 200-400cm height, 50cm thickness
- **Boxes:** 100-150cm cubes
- **Barriers:** Low cover (100cm), partial concealment
- **Placement:** EQS auto-detects, but strategic placement improves learning

#### 7. **Nav Mesh Bounds Volume**
- **Coverage:** Entire playable area + 1000cm margin
- **Settings:** Agent Radius: 50cm, Agent Height: 180cm

---

## Naming Conventions

### Map Files
```
[Category]_[Type]_[Size]_[Variant].umap

Examples:
- Training_BasicCombat_2v2_v01.umap
- Training_CoverUsage_4v4_v01.umap
- Eval_Symmetric_4v4_Urban.umap
- Demo_Flanking_4v4_Showcase.umap
```

### Actor Naming
```
Team Leaders:
- TL_Alpha
- TL_Bravo

AI Agents:
- Agent_A1, Agent_A2, Agent_A3, Agent_A4 (Team Alpha)
- Agent_B1, Agent_B2, Agent_B3, Agent_B4 (Team Bravo)

Objectives:
- Obj_Capture_Central
- Obj_Defend_North
- Obj_Rescue_Alpha_Down

Spawn Points:
- Spawn_Alpha_01, Spawn_Alpha_02, ...
- Spawn_Bravo_01, Spawn_Bravo_02, ...
```

---

## Blueprint Setup Guide

### Step 1: Create Base Game Mode (Do Once)

**File:** `Content/Blueprints/Core/BP_ExperimentGameMode.uasset`

**Parent Class:** `ASimulationManagerGameMode`

**Overrides:**
1. `BeginPlay`:
   ```
   - Call parent BeginPlay
   - Initialize ExperimentTracker component
   - Set experiment metadata (map name, timestamp)
   - Register team leaders
   ```

2. `Tick`:
   ```
   - Check win conditions every 1.0s
   - If all Team Alpha dead → Team Bravo wins
   - If all Team Bravo dead → Team Alpha wins
   - If timeout (600s default) → Draw
   - Call OnEpisodeEnd when match ends
   ```

3. `OnEpisodeEnd`:
   ```
   - Record final metrics
   - Export experiences to JSON
   - Log to CSV via ExperimentTracker
   - Trigger next episode (if batch run)
   ```

### Step 2: Create Team Leader Blueprint (Do Once)

**File:** `Content/Blueprints/AI/BP_TeamLeader.uasset`

**Parent Class:** `AActor`

**Components:**
- `UTeamLeaderComponent`
- `UObjectiveManager` (auto-created by TeamLeaderComponent)
- `UCurriculumManager` (auto-created)

**Event Graph:**
1. `BeginPlay`:
   ```
   - Set TeamName based on tag (Alpha/Bravo)
   - Discover world objectives (auto-called)
   - Log initialization
   ```

2. `Tick`:
   ```
   - Continuous planning (handled by component)
   - Debug drawing (if enabled)
   ```

### Step 3: Create AI Agent Blueprint (Do Once)

**File:** `Content/Blueprints/AI/BP_AIAgent.uasset`

**Parent Class:** `ACharacter`

**Components:**
- `UFollowerAgentComponent`
- `UHealthComponent`
- `UWeaponComponent`
- `UAgentPerceptionComponent`
- `URewardCalculator`
- `UFollowerStateTreeComponent`

**AI Controller:** `AAIController`

**Event Graph:**
1. `BeginPlay`:
   ```
   - Register with team leader (auto if bAutoRegisterWithLeader=true)
   - Initialize RL policy
   - Bind health events to reward calculator
   ```

2. `Tick`:
   ```
   - Update observation
   - Execute StateTree (command-driven)
   - Calculate rewards (via RewardCalculator)
   ```

3. `OnDeath`:
   ```
   - Signal event to team leader
   - Unregister from team
   - Export experiences
   - Disable AI
   ```

### Step 4: Per-Level Setup

**For Each New Level:**

1. **Create Map File**
   - File → New Level → Default
   - Save as: `Content/Maps/Training/Training_[Name]_4v4.umap`

2. **Set Game Mode**
   - World Settings → GameMode Override → `BP_ExperimentGameMode`

3. **Place Team Leaders** (2 actors)
   - Drag `BP_TeamLeader` into level (×2)
   - Rename: `TL_Alpha`, `TL_Bravo`
   - Add tags: `TeamLeader` + `TeamAlpha`/`TeamBravo`
   - Set TeamColor: Blue (Alpha), Red (Bravo)

4. **Place AI Agents** (8 actors)
   - Drag `BP_AIAgent` into level (×8)
   - Rename: `Agent_A1-A4`, `Agent_B1-B4`
   - Position at spawn zones
   - Set `TeamLeaderTag`:
     - Agents A1-A4: `TeamAlpha`
     - Agents B1-B4: `TeamBravo`

5. **Place Objectives**
   - Capture: Drag actor, add tag `CaptureZone`
   - Defend: Drag actor, add tag `DefendZone`
   - Rescue: Use AI agent, tag `RescueTarget`, set health <50%

6. **Build Navigation**
   - Add `Nav Mesh Bounds Volume` covering play area
   - Press `P` to visualize nav mesh (green overlay)
   - Build → Build Paths

7. **Lighting & Post-Processing**
   - Add `Directional Light` (sun)
   - Add `Sky Atmosphere`
   - Add `Post Process Volume` (Infinite Extent = true)

8. **Test Run**
   - PIE (Play in Editor)
   - Verify agents register with leaders (check logs)
   - Verify MCTS runs (look for "🎯 [OBJECTIVE MCTS]" logs)
   - Verify combat works (agents engage when in range)

---

## Curriculum Level Specifications

### Phase 01: Fundamentals (2v2)

#### T1_BasicCombat_2v2
**Learning Goal:** Basic shooting, health management, death detection

**Layout:**
```
Dimensions: 5000 × 5000 cm (50m × 50m)
Terrain: Flat
Cover: None
Objectives: None (pure elimination)

Spawn A: (0, 0, 100)       Spawn B: (4000, 4000, 100)
```

**Success Criteria:**
- Agents learn to aim and fire at enemies
- Win rate converges to ~50% (balanced)
- Average episode length: 30-60s

---

#### T2_CoverUsage_2v2
**Learning Goal:** Use EQS cover system, minimize damage taken

**Layout:**
```
Dimensions: 6000 × 6000 cm
Terrain: Flat
Cover: 8 boxes (150cm cubes) scattered
Objectives: None (elimination)

Cover Positions:
- (1500, 1500, 0)
- (1500, 4500, 0)
- (3000, 3000, 0)
- ... (symmetrical)
```

**Success Criteria:**
- Agents use cover >60% of combat time
- Damage taken per episode decreases by 30%
- FormationCoherence > 0.5

---

#### T3_Positioning_3v3
**Learning Goal:** Formation coherence, spatial awareness

**Layout:**
```
Dimensions: 8000 × 8000 cm
Terrain: Flat with elevation changes (ramps)
Cover: 12 boxes + 4 walls
Objectives: None (elimination)

High Ground: Center platform (500cm elevation)
```

**Success Criteria:**
- FormationCoherence > 0.6
- Agents seek high ground when available
- Coordination rate (combined fire) > 10%

---

### Phase 02: Coordination (3v3)

#### T4_Crossfire_3v3
**Learning Goal:** Combined fire, target prioritization

**Layout:**
```
Dimensions: 7000 × 7000 cm
Terrain: L-shaped corridors
Cover: Corners and pillars
Objectives: None (elimination)

Layout forces crossfire opportunities (multiple angles of attack)
```

**Success Criteria:**
- Combined fire rate > 25%
- Kill/Death ratio > 1.2 (better than random)
- MCTS selects coordinated objectives

---

#### T5_Flanking_4v4
**Learning Goal:** Multi-angle attacks, distraction tactics

**Layout:**
```
Dimensions: 10000 × 10000 cm
Terrain: Central obstacle (building/hill)
Cover: Perimeter walls
Objectives: 1 CaptureZone (center)

Forces teams to split and flank around obstacle
```

**Success Criteria:**
- Agents split into 2+ subgroups
- Flanking kills > 40% of total kills
- Objective capture time < 90s

---

#### T6_Rescue_4v4
**Learning Goal:** RescueAlly objective, role specialization

**Layout:**
```
Dimensions: 8000 × 8000 cm
Terrain: Mixed (open + buildings)
Cover: Dense near center
Objectives: 2 RescueTargets per team (wounded allies)

Initial setup: 3 healthy + 1 wounded per team
```

**Success Criteria:**
- MCTS assigns "Eliminate" to frontline, "Rescue" to rear
- Rescue success rate > 70%
- Wounded agents survive >60s

---

### Phase 03: Strategic (4v4)

#### T7_Capture_4v4
**Learning Goal:** CaptureObjective, offensive coordination

**Layout:**
```
Dimensions: 12000 × 12000 cm
Terrain: Urban (buildings, streets)
Cover: Dense
Objectives: 3 CaptureZones (A, B, C)

Win Condition: Capture 2/3 zones OR eliminate enemy team
```

**Success Criteria:**
- Strategic objective assignment (2 agents capture, 2 defend)
- Zone capture time < 120s
- Win rate via capture > 40% (vs elimination)

---

#### T8_Defend_4v4
**Learning Goal:** DefendObjective under pressure

**Layout:**
```
Dimensions: 10000 × 10000 cm
Terrain: Fortified position (one team) vs open approach (other team)
Cover: Asymmetric (defenders have advantage)
Objectives: 1 DefendZone (defenders hold for 300s)

Team Alpha: Defenders (spawn at DefendZone)
Team Bravo: Attackers (spawn 5000cm away)
```

**Success Criteria:**
- Defenders hold zone >80% of time
- Defenders form defensive perimeter
- Attackers coordinate assault (flanking, suppression)

---

#### T9_Mixed_4v4
**Learning Goal:** Dynamic objective switching

**Layout:**
```
Dimensions: 15000 × 15000 cm
Terrain: Mixed (urban + open)
Cover: Strategic placement
Objectives:
- 2 CaptureZones
- 1 DefendZone
- 2 RescueTargets (spawn mid-match)

Objectives activate sequentially to force re-planning
```

**Success Criteria:**
- MCTS adapts to new objectives within 2 planning cycles
- Multiple objective types completed in single episode
- Strategic experience count > 10 per episode

---

### Phase 04: Final Challenge

#### T10_FullScenario_4v4
**Learning Goal:** All mechanics, complex decision-making

**Layout:**
```
Dimensions: 20000 × 20000 cm
Terrain: Realistic battlefield (trenches, bunkers, hills)
Cover: Realistic placement
Objectives:
- 3 CaptureZones (progressive unlock)
- 2 DefendZones
- 3 RescueTargets
- Dynamic enemy reinforcements

Win Condition: Complete 3 objectives OR eliminate all enemies
Time Limit: 600s (10 minutes)
```

**Success Criteria:**
- Win rate > 60% vs curriculum start
- All reward types activated (individual, coordination, strategic)
- MCTS latency < 50ms despite complexity
- Emergent tactics visible (flanking, suppression, rescue)

---

## Evaluation Level Specifications

### Eval_Symmetric_4v4
**Purpose:** Baseline comparison in fair environment

**Layout:**
```
Perfectly symmetrical
- Mirror spawns
- Equal cover distribution
- Central CaptureZone
- No terrain advantage

Used for: Win rate vs rule-based AI
```

---

### Eval_Urban_4v4
**Purpose:** Test cover usage, close-quarters combat

**Layout:**
```
Dense urban environment
- Narrow streets
- Buildings with windows
- Multiple floors
- Limited sightlines

Used for: EQS cover system effectiveness
```

---

### Eval_Open_4v4
**Purpose:** Test long-range engagement, positioning

**Layout:**
```
Open terrain
- Sparse cover
- Long sightlines (>8000cm)
- High ground positions
- Minimal obstacles

Used for: Formation coherence, positioning
```

---

### Eval_Mixed_Objectives_4v4
**Purpose:** Test MCTS objective assignment

**Layout:**
```
All objective types present:
- 2 CaptureZones
- 1 DefendZone
- 2 Rescue points
- 1 Eliminate target

Used for: Strategic decision quality
```

---

## Testing Checklist

### Pre-Play Checklist
- [ ] Game Mode set to `BP_ExperimentGameMode`
- [ ] 2 Team Leaders placed with correct tags
- [ ] 8 AI Agents placed (4 per team)
- [ ] TeamLeaderTag set correctly on all agents
- [ ] Objectives tagged correctly
- [ ] Nav Mesh built (press `P` to verify)
- [ ] Spawn points separated by >500cm
- [ ] ExperimentTracker component added to GameMode

### Post-Play Validation
- [ ] Logs show "Registered follower" (×8)
- [ ] Logs show "🎯 [OBJECTIVE MCTS] STARTED" (periodic)
- [ ] Agents engage enemies when in range
- [ ] Agents use cover (if available)
- [ ] Death events logged
- [ ] Episode ends with winner declared
- [ ] CSV file generated in `Saved/Experiments/`
- [ ] Experience JSON exported to `Saved/Experiences/`

### Performance Benchmarks
- [ ] MCTS latency < 50ms (check logs "✓ [PERFORMANCE]")
- [ ] Frame rate >30 FPS with 8 agents
- [ ] No crashes during 10-episode run
- [ ] Memory usage stable (<8GB total)

---

## Advanced: Scripted Scenarios

### Dynamic Events Blueprint

For advanced curriculum, add event triggers:

**Example: Reinforcement Spawn**
```
Event: After 120s, spawn 2 additional enemies
Implementation:
  - Timer: 120s
  - SpawnActor: BP_AIAgent ×2
  - Register with Team Bravo
  - Signal event: EnemyReinforcements (priority 9)
```

**Example: Objective Progression**
```
Event: After Capture_A complete, activate Defend_B
Implementation:
  - Bind to Capture_A completion delegate
  - Activate Obj_Defend_B
  - Signal event: NewObjective (priority 8)
```

---

## Troubleshooting

### Agents Don't Register with Leader
- Check `TeamLeaderTag` matches leader's tag
- Verify leader has `TeamLeader` tag
- Check `bAutoRegisterWithLeader = true`

### MCTS Never Runs
- Check `bContinuousPlanning = true`
- Verify `ContinuousPlanningInterval > 0`
- Check agents are alive (`GetAliveFollowers().Num() > 0`)

### Agents Don't Move
- Verify Nav Mesh covers play area (press `P`)
- Check AI Controller is set
- Verify StateTree component present

### No Objectives Assigned
- Check objective tags are correct
- Verify `DiscoverWorldObjectives()` called
- Check `ObjectiveManager` initialized

---

## Quick Reference Card

```
MINIMUM VIABLE LEVEL:
1. Game Mode: BP_ExperimentGameMode
2. Team Leaders: 2 (tags: TeamLeader, TeamAlpha/TeamBravo)
3. AI Agents: 8 (TeamLeaderTag set)
4. Nav Mesh: Covers play area
5. Win Condition: Elimination or objective

RECOMMENDED ADDITIONS:
6. Cover elements (boxes, walls)
7. Objectives (CaptureZone, DefendZone, RescueTarget)
8. Elevation variation (ramps, platforms)
9. Lighting (Directional Light + Sky)

DEBUG VERIFICATION:
- PIE → Output Log → Filter "TEAM LEADER"
- Should see: "Registered follower" ×8
- Should see: "🎯 [OBJECTIVE MCTS] STARTED" within 2s
- Should see: "🎯 [OBJECTIVE ASSIGNMENT]" with objectives
```

---

**End of Template**

For questions or issues, check:
- `CLAUDE.md` - System architecture reference
- `Source/GameAI_Project/Team/TeamLeaderComponent.cpp:26-293` - Initialization code
- Output logs during PIE (filter by "TEAM LEADER", "FOLLOWER", "MCTS")
