# Dynamic EQS

**Engine:** UE5.6 | **Language:** C++17 | **Platform:** Windows | **Schola:** 2.0.1

Dynamic EQS is a plugin + game environment that connects a Python-trained RL policy to Unreal Engine 5's native **Environment Query System (EQS)**. Instead of hand-coded heuristics, agents learn to output **7 continuous EQS weights** that control where they move on the map.

---

## What is Dynamic EQS?

Standard EQS evaluates candidate positions using fixed, designer-authored scoring weights. Dynamic EQS replaces those fixed weights with values inferred by a neural network at runtime.

**Core loop:**
1. Agent observes a 170-dim entity-centric state (positions, health, visibility, strategy)
2. `EntityCentricPolicy` (217K params, Python/ONNX) infers 7 EQS weight values in `[-1, 1]`
3. Weights are injected as named float parameters into a UE5 EQS query
4. EQS scores 48 candidate locations using 8 weighted tests → best location selected
5. Agent pathfinds to that location via UE5 NavMesh

The policy is trained with PPO (RLlib) against a 5v5 capture-point game environment. During inference, the trained ONNX model runs inside UE5 via NNE.

---

## Plugin Architecture

Dynamic EQS extends the **Schola** plugin (gRPC bridge between UE5 and Python):

```
Schola (base)                    Dynamic EQS (plugin)          Game (this project)
─────────────────────────────────────────────────────────────────────────────────
UInferenceComponent          →   UDynamicEQSAgentComponent  →  UDEScholaAgent
UBoxObserver                 →   UDynamicEQSObserverBase    →  UDETacticalObserver
UBoxActuator                 →   UDynamicEQSActuatorBase    →  UDETacticalParameterActuator
```

**Two execution modes:**

| Mode | EQS Execution | Policy Source |
| :--- | :--- | :--- |
| Training | `ExecuteSynchronousQuery()` (blocking) | Python RLlib via gRPC |
| Inference | Async BT task callback | Local ONNX via UE5 NNE |

---

## Game Environment (5v5 Capture-Point Arena)

The test environment is a 5v5 team deathmatch / capture-point game:

- **Teams:** Red vs Blue, 5 agents each (`ADECharacter` pawns)
- **Objectives:** 5 capture points (A–E) distributed across the map
- **Scoring:** Points for captures (+25) and passive income per owned point. First to 300 or highest at 600 seconds wins.
- **Respawn:** Individual death has a timer; full team wipe triggers a 5-second group respawn.
- **Abilities (GAS):** `DEGA_Attack` (projectile + ammo + cooldown), `DEGA_Heal` (range-based ally heal with Niagara beam)

### Agent Components

Each `ADECharacter` pawn has:
- `UDEScholaAgent` — Schola RL interface; holds `CommandedStrategy` (Assault/Defend/Support)
- `UDEEQSExecutor` — Wraps UE5 EQS; applies weights from policy
- `UAbilitySystemComponent` — GAS for abilities

Each `ADEAIController` runs:
- AI Perception (sight, hearing, damage) → combat target selection
- `BT_DEAgent` behavior tree → movement (EQS), attack, heal tasks

---

## Observation Space (170-dim, entity-centric)

Built by `UDETacticalObserver` → `FDEObservationV2::ToFlatArray()`:

```
[  0:  7]  Self token        pos/7500×3, health, vel/600×3
[  7: 47]  Ally tokens       8×5 — rel_pos/8000×3, health, alive        (padded)
[ 47: 87]  Enemy tokens      8×5 — rel_pos/8000×3, visible, confidence  (padded)
[ 87:143]  Base tokens       8×7 — rel_pos/15000×2, rel_z/1000, ownership,
                                   cap_progress, is_assigned, strategic_val (padded)
[143:151]  Ally mask         8   — 0=present, 1=padding
[151:159]  Enemy mask        8
[159:167]  Base mask         8
[167:170]  Strategy one-hot  3   — [assault, defend, support]
```

---

## Action Space (7-dim EQS weights)

Policy output → `UDETacticalParameterActuator` → injected as named EQS float params:

| Index | Weight | Effect |
| :--- | :--- | :--- |
| 0 | EnemyObjectiveProximity | Approach vs. avoid enemy base |
| 1 | AllyObjectiveProximity | Stay near vs. abandon friendly base |
| 2 | CoverDensity | Seek vs. avoid cover |
| 3 | EnemyVisibility | Expose vs. hide from enemies |
| 4 | AllyProximity | Group up vs. solo play |
| 5 | CombatRange | Preferred engagement distance |
| 6 | AssignedBaseProximity | Move toward vs. ignore assigned capture point |

---

## Reward System

Computed C++-side by `UDERewardSubsystem`, sent to Python via Schola:

| Signal | Value | Trigger |
| :--- | :--- | :--- |
| BaseOccupationReward | +2.0/step | Sole ally within 2000cm of uncontrolled base |
| CoOccupationPenalty | −0.5/step | 2+ allies stacking same base |
| BaseCaptureCreditReward | +5.0 sparse | Agent that flipped base ownership |
| UndefendedBasePenalty | −1.0/step | Friendly base with no nearby ally (shared) |
| AssignedBaseReachReward | +1.0 sparse | First time reaching assigned base |

Python-side scaling: `reward × 0.01`, clipped to `[−5, 5]`.

---

## Policy Network (`EntityCentricPolicy`)

Located at `DE_Training/training/policy.py`. Permutation-invariant via per-entity-type attention:

```
Input: (B, 170) flat observation

Self encoder     : Linear(7 → 64)
Ally encoder     : Linear(5 → 64) + MultiheadAttention(64, heads=4)
Enemy encoder    : Linear(5 → 64) + MultiheadAttention(64, heads=4)
Base encoder     : Linear(7 → 64) + MultiheadAttention(64, heads=4)
Strategy encoder : Embedding(3 → 64)  [via argmax of one-hot at [167:170]]

Combined: concat(self, ally_ctx, enemy_ctx, base_ctx, strategy) → (B, 320)

Action head : Linear(320→256) → ReLU → Linear(256→128) → ReLU → Linear(128→7) → Tanh
Value head  : Linear(320→256) → ReLU → Linear(256→1)
log_std     : learnable (7,), clamped to [−2.5, −0.7]

Output: (B, 7) EQS weights in [−1, 1]
Parameters: ~217K
```

ONNX export: fixed shape `(B, 170) → (B, 7)` for UE5 NNE inference.

---

## Training

```bash
cd DE_Training/training

# RLlib PPO (requires UE5 in PIE mode)
python train.py --mode rllib --iterations 100

# Validate shapes + ONNX export
python train.py --mode validate

# Evaluate checkpoint
python train.py --mode eval --checkpoint de_policy.pt
```

### Key Hyperparameters (`DETrainingConfig`)

```python
LEARNING_RATE    = 3e-4   # anneals to 5e-5 over 4M steps
TRAIN_BATCH_SIZE = 8000
MINIBATCH_SIZE   = 512
NUM_SGD_ITER     = 6
GAMMA            = 0.99
GAE_LAMBDA       = 0.95
CLIP_PARAM       = 0.2
ENTROPY_COEFF    = 0.01   # anneals to 0.0005 over 2.5M steps
VF_LOSS_COEFF    = 0.5
GRAD_CLIP        = 0.5
```

### Output

Results saved to `DE_Training/training_results/YYYYMMDD_HHMMSS/`:
- `de_policy_entity_centric.onnx` — policy for UE5 NNE
- `best/` — best reward checkpoint
- `latest/` — most recent checkpoint
- `tb/` — TensorBoard logs

```bash
tensorboard --logdir DE_Training/training_results
```

---

## File Structure

```
CORTEX/
├── Source/GameAI_Project/
│   ├── Public/
│   │   ├── Characters/             # DECharacter (agent pawn)
│   │   ├── AI/AIController/        # DEAIController (perception + BT)
│   │   ├── AI/EQS/                 # DEEQSExecutor
│   │   ├── Schola/
│   │   │   ├── Components/         # DEScholaAgent
│   │   │   ├── Observers/          # DETacticalObserver
│   │   │   └── Actuators/          # DETacticalParameterActuator
│   │   ├── GAS/                    # DEAttributeSet, DEGA_Attack, DEGA_Heal
│   │   ├── Team/                   # DEMatchManager, DESquadManager
│   │   ├── Actors/                 # DECapturePoint
│   │   └── Types/                  # DEEQSTypes, DEObservationTypes, DERewardTypes
│   └── CLAUDE.md
│
└── DE_Training/
    └── training/
        ├── policy.py               # EntityCentricPolicy (217K params)
        ├── env_wrapper.py          # DEEntityCentricEnv (Schola 2.0.1)
        └── train.py                # Entry point (rllib / validate / eval)
```

---

## Prerequisites

- Unreal Engine 5.6
- C++17 (MSVC 2019+)
- Schola 2.0.1 plugin
- Python 3.8+, PyTorch 2.0+, `ray[rllib]`

---

## Documentation

- **`Source/GameAI_Project/CLAUDE.md`** — Full technical specification
- **`DE_Training/training/README.md`** — Training setup and troubleshooting

---

**Last Updated:** 2026-03-18
