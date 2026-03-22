# Refactoring Plan: Self-Play → Fixed Opponent Training

**Date:** 2026-03-22
**Branch:** `feature-defendrework`
**Author:** AI-assisted

---

## 1. Problem Statement

### 1.1 Current Architecture: Symmetric Self-Play

Both Red and Blue teams are controlled by the same three RLlib PPO policies (`strike_policy`, `vanguard_policy`, `support_policy`). All 10 agents share weights — when one team improves, it simultaneously changes the opponent for the other team.

```
                    ┌──────────────────────┐
                    │  RLlib PPO Trainer    │
                    │  (3 shared policies)  │
                    └──────┬───────────────┘
                           │
              ┌────────────┴────────────┐
              ▼                         ▼
        Red Team (5)              Blue Team (5)
        [RL policies]             [RL policies]
              │                         │
              └────────── vs ───────────┘
```

### 1.2 Why Self-Play Is Failing

Training has failed across multiple runs (see `training_diagnosis_20260321.md`). The root causes are structural, not tuning problems:

**Cause 1 — Non-Stationarity (6 co-adapting policies)**

Three separate policy networks update simultaneously. Each policy update shifts the environment dynamics for every other policy. The value function must track a moving target — the opponent's changing behavior — on top of the agent's own improvement.

Evidence:
- Support `explained_var` dropped to 0.28 (critic cannot predict returns)
- Vanguard `explained_var` dropped from 0.80 → 0.52 mid-training
- Support `vf_loss` was rising, not converging

**Cause 2 — No Measurable Progress Signal**

In symmetric self-play, both teams improve together. Mean episode reward stays roughly flat even when genuine learning occurs, because the opponent improves at the same rate. There is no stable baseline to measure improvement against.

Evidence:
- episode_reward_mean oscillated without clear upward trend
- No way to distinguish "both teams learning" from "both teams stuck"

**Cause 3 — Reward Signal Instability Amplified by Non-Stationarity**

The reward shaping bugs discovered in `training_diagnosis_20260321.md` (Strike zone penalty, Support bimodal reward, Vanguard conflicting damage signals) were difficult to diagnose precisely because the non-stationary opponent masked whether reward changes improved actual behavior or just exploited a temporarily weaker opponent.

Evidence:
- Support collapse post-mortem (1700 → 1100 episode_mean in 100k steps)
- Strike entropy collapse — locked into avoidance behavior because approaching zones was never positive-EV against a simultaneously-improving opponent

**Cause 4 — Missing Self-Play Infrastructure**

Stable self-play training (as in OpenAI Five, AlphaStar) requires:
- Policy population pools (multiple frozen snapshots as opponents)
- Elo-based matchmaking (training against appropriate difficulty)
- Win-rate tracking against fixed reference agents
- Extensive compute budget (millions of episodes)

None of this infrastructure exists. The current setup is naive symmetric self-play — the least stable variant.

### 1.3 Decision

Switch to **fixed-opponent training**: Blue team (RL) trains against Red team (scripted AI with hardcoded EQS weights). This makes the environment stationary from Blue's perspective, enabling stable value function learning and clear win-rate metrics.

---

## 2. Architecture: Fixed Opponent Design

### 2.1 Target Architecture

```
                    ┌──────────────────────┐
                    │  RLlib PPO Trainer    │
                    │  (3 RL policies)      │
                    └──────┬───────────────┘
                           │
                           ▼
                     Blue Team (5)
                     [RL policies]
                           │
                           │  vs
                           │
                     Red Team (5)
                     [Scripted AI — fixed EQS weights]
                           │
                     No Schola connection
                     No Python policy
                     Pure C++ EQS execution
```

### 2.2 Scripted AI Behavior (Red Team)

The scripted opponent uses the existing EQS system (`UDynamicEQSExecutor`) with **hardcoded per-class weight profiles**. No new EQS queries are needed — the 7-dim weight vector already controls spatial behavior.

#### Strike (Scripted)
| # | Weight | Value | Rationale |
|:--|:-------|:------|:----------|
| 0 | EnemyObjectiveProximity | +0.6 | Push toward contested points |
| 1 | AllyObjectiveProximity | +0.2 | Light friendly-point preference |
| 2 | CoverDensity | +0.5 | Use cover |
| 3 | EnemyVisibility | +0.7 | Maintain LOS for damage |
| 4 | AllyProximity | +0.3 | Moderate grouping |
| 5 | CombatRange | +0.8 | Enforce 800-1500cm optimal range |
| 6 | AssignedBaseProximity | +0.5 | Move toward assigned objective |

#### Vanguard (Scripted)
| # | Weight | Value | Rationale |
|:--|:-------|:------|:----------|
| 0 | EnemyObjectiveProximity | +0.8 | Aggressive push |
| 1 | AllyObjectiveProximity | +0.1 | Low — Vanguard goes forward |
| 2 | CoverDensity | +0.2 | Low — melee needs to close distance |
| 3 | EnemyVisibility | +0.4 | Moderate — needs to find enemies |
| 4 | AllyProximity | +0.3 | Stay near teammates |
| 5 | CombatRange | -0.5 | Negative — seek close range (melee) |
| 6 | AssignedBaseProximity | +0.6 | Push assigned base |

#### Support (Scripted)
| # | Weight | Value | Rationale |
|:--|:-------|:------|:----------|
| 0 | EnemyObjectiveProximity | -0.3 | Avoid enemy zones |
| 1 | AllyObjectiveProximity | +0.4 | Stay near friendly zones |
| 2 | CoverDensity | +0.7 | Prioritize cover |
| 3 | EnemyVisibility | -0.2 | Avoid enemy LOS |
| 4 | AllyProximity | +0.9 | Strong ally grouping (heal range) |
| 5 | CombatRange | -0.3 | Stay back from combat |
| 6 | AssignedBaseProximity | +0.3 | Light objective pull |

These weights produce reasonable but imperfect behavior — enough to be a meaningful training opponent without being optimal.

### 2.3 Difficulty Tiers (Curriculum)

| Tier | Name | Modification | When |
|:-----|:-----|:-------------|:-----|
| 0 | Passive | All weights = 0 except AssignedBaseProximity = +0.5 | 0–100k steps |
| 1 | Basic | Weights above, no attack ability usage | 100k–500k steps |
| 2 | Standard | Full weights + normal combat | 500k–2M steps |
| 3 | Aggressive | Weights with boosted EnemyObjectiveProximity (+0.2) | 2M+ steps |

Tier transitions are driven by **Blue team win rate** (rolling 100-episode window):
- Tier 0 → 1: win rate > 70%
- Tier 1 → 2: win rate > 65%
- Tier 2 → 3: win rate > 60%

---

## 3. Implementation Plan

### Phase 1: C++ — Scripted AI Controller Component

**New file:** `Public/Components/DEScriptedAIComponent.h` / `Private/Components/DEScriptedAIComponent.cpp`

A `UActorComponent` attached to Red team agents that overrides the Schola action pipeline:

```
UDEScriptedAIComponent
├── ScriptedWeights: TMap<EDEClassType, FDEEQSWeightParameters>
├── CurrentTier: int32
├── SetDifficultyTier(int32 Tier)
├── GetScriptedWeights(EDEClassType Class) → FDEEQSWeightParameters
└── TickComponent() → applies weights to owning ADEAgent via UpdateTacticalWeights()
```

Key design decisions:
- **Component, not subclass.** Attaches to existing `ADEAgent` — avoids duplicating the 497-line agent class.
- **No Schola connection.** Red team agents skip `UDEScholaAgent` observation/action cycle. The component directly writes EQS weights each tick.
- **Shared combat system.** Attack/Heal abilities still run through the existing `DEAIController` behavior tree. Only movement policy is scripted.

#### Files Modified

| File | Change |
|:-----|:-------|
| `DEMatchManager.h/.cpp` | Add `bUseScriptedOpponent` flag. When true, Red team agents get `UDEScriptedAIComponent` instead of Schola registration. |
| `DESquadManager.h/.cpp` | No change — role assignment remains round-robin for both teams. |
| `DEAgent.h/.cpp` | Add `bIsScriptedAI` flag. When set, `ComputeStepReward()` is skipped (no reward needed for scripted agents). |

### Phase 2: C++ — Match Manager Integration

**File:** `Private/Team/DEMatchManager.cpp`

Changes to `SpawnTeam()`:
1. Check `bUseScriptedOpponent` and team ID.
2. For Red team (scripted): skip Schola agent creation, attach `UDEScriptedAIComponent`.
3. For Blue team (RL): unchanged — full Schola pipeline.

Changes to `ResetTeams()`:
1. Red team: reset scripted component weights (in case tier changed).
2. Blue team: unchanged.

New method `SetOpponentDifficulty(int32 Tier)`:
- Iterates Red team agents, calls `UDEScriptedAIComponent::SetDifficultyTier(Tier)`.
- Called from Python via a Schola info channel (see Phase 3).

### Phase 3: Python — Environment Wrapper Changes

**File:** `DE_Training/training/env_wrapper.py`

Current state: `DEEntityCentricEnv` registers all 10 agents (5 Red + 5 Blue) as RLlib multi-agent entries.

Changes:
1. **Filter agent registration.** Only register Blue team agents (5 agents instead of 10). Red team agents have no Schola connection — they never appear in the Python obs/action dicts.
2. **Win rate tracking.** Add rolling win-rate computation from episode terminal info:
   ```python
   self._win_history = deque(maxlen=100)
   # In step(): if episode done, check info["winner_team_id"]
   # self._win_history.append(1 if blue_won else 0)
   # win_rate = sum(self._win_history) / len(self._win_history)
   ```
3. **Curriculum trigger.** Emit `win_rate` as a custom metric. Tier transitions handled in `train.py` callback.
4. **Difficulty control.** Send tier via Schola info channel (or env config on reset).

### Phase 4: Python — Training Script Changes

**File:** `DE_Training/training/train.py`

Changes:
1. **Policy count.** Still 3 policies (`strike_policy`, `vanguard_policy`, `support_policy`) — but only for Blue team's 5 agents.
2. **Win rate logging.** Add TensorBoard scalars:
   - `global/win_rate` (rolling 100-episode)
   - `global/opponent_tier`
3. **Curriculum callback.** RLlib custom callback that checks win rate and calls `SetOpponentDifficulty()` when thresholds are met.
4. **Remove self-play artifacts.** The `policy_mapping_fn` no longer needs to handle Red team agent IDs.

### Phase 5: Reward Metric — Win Rate as Primary

**New TensorBoard scalars:**
| Scalar | Source | Purpose |
|:-------|:-------|:--------|
| `global/win_rate` | Python (rolling 100-ep) | Primary portfolio metric |
| `global/opponent_tier` | Python | Track curriculum progression |
| `global/blue_score_mean` | C++ via info | Score margin |
| `global/captures_mean` | C++ via info | Objective activity |
| `{role}/reward/episode_mean` | RLlib (existing) | Per-role learning signal |

Win rate replaces `episode_reward_mean` as the primary convergence indicator.

---

## 4. What Does NOT Change

| System | Status |
|:-------|:-------|
| **Entity-centric policy architecture** | Unchanged — same `EntityCentricPolicy` with self-attention + cross-attention |
| **Per-role policy specialization** | Unchanged — 3 separate policies for Strike/Vanguard/Support |
| **Observation format** | Unchanged — 226-dim flat array from `FDEObservationV2::ToFlatArray()` |
| **Action format** | Unchanged — 7-dim EQS weights in [-1, 1] |
| **Reward shaping** | Unchanged — all per-class dense/sparse rewards remain active for Blue team |
| **ONNX export** | Unchanged — trained models export identically |
| **EQS spatial reasoning** | Unchanged — same 48-sample, 8-test query system |
| **Schola gRPC protocol** | Unchanged for Blue team — Red team simply doesn't use it |
| **GAS ability system** | Unchanged — both teams use identical combat mechanics |

---

## 5. Risk Assessment

| Risk | Likelihood | Mitigation |
|:-----|:-----------|:-----------|
| Scripted AI too easy → RL overfits to exploit patterns | Medium | Curriculum tiers + slight weight randomization (±0.1 noise per episode) |
| Scripted AI too hard at Tier 3 → RL cannot win | Low | Tune weights empirically; can add intermediate tiers |
| RL policies don't generalize beyond scripted opponent | Medium | Acceptable for portfolio; optional self-play fine-tuning later |
| Schola agent filtering breaks env wrapper | Low | Blue-only agent count is known (5); straightforward filter |

---

## 6. Success Criteria

| Metric | Target | Rationale |
|:-------|:-------|:----------|
| Win rate vs Tier 2 | > 60% | RL agents beat competent scripted AI |
| Win rate vs Tier 3 | > 50% | RL agents competitive against aggressive AI |
| Role differentiation | Visual inspection | Strike maintains range, Vanguard pushes objectives, Support follows allies |
| Training convergence | explained_var > 0.5 all roles | Critic can predict returns in stationary environment |
| Training time to Tier 2 convergence | < 2M steps | Practical within compute budget |

---

## 7. File Summary

### New Files
| File | Purpose |
|:-----|:--------|
| `Public/Components/DEScriptedAIComponent.h` | Scripted AI component header |
| `Private/Components/DEScriptedAIComponent.cpp` | Hardcoded per-class EQS weights, tier system |

### Modified Files (C++)
| File | Change |
|:-----|:-------|
| `Public/Team/DEMatchManager.h` | Add `bUseScriptedOpponent`, `SetOpponentDifficulty()` |
| `Private/Team/DEMatchManager.cpp` | Conditional scripted component attachment in `SpawnTeam()` |
| `Public/Characters/DEAgent.h` | Add `bIsScriptedAI` flag |
| `Private/Characters/DEAgent.cpp` | Skip reward computation when `bIsScriptedAI` |

### Modified Files (Python)
| File | Change |
|:-----|:-------|
| `DE_Training/training/env_wrapper.py` | Filter to Blue-only agents, win rate tracking |
| `DE_Training/training/train.py` | Win rate logging, curriculum callback, remove self-play mapping |
