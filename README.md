# Dynamic EQS — RL-Based EQS Weight Optimization for UE5

**Engine:** UE5.6 | **Language:** C++17 / Python | **Platform:** Windows / Linux (AWS) | **Schola:** 2.0.1

## Overview

Dynamic EQS replaces hand-tuned UE5 EQS scoring weights with values inferred by reinforcement learning policies at runtime. Agents learn optimal spatial positioning in a 5v5 capture-point game through dynamically tuned EQS parameters.

The system connects UE5 to Ray RLlib via the Schola plugin (gRPC bridge), trains role-specific policies using **MAPPO (Multi-Agent PPO)**, and supports cloud-scale parallel training on AWS.

## Architecture

```
┌──────────────────────────────────────────────────────┐
│  Ray RLlib (MAPPO)                                   │
│  3 Actor policies: Strike / Vanguard / Support       │
│  Centralized Critic (71-dim global team state)       │
└───────────────────┬──────────────────────────────────┘
                    │ gRPC (Schola)
┌───────────────────▼──────────────────────────────────┐
│  Dynamic EQS Plugin (reusable middleware)             │
│  Policy output (7-dim) → EQS weight injection        │
└───────────────────┬──────────────────────────────────┘
                    │
┌───────────────────▼──────────────────────────────────┐
│  UE5 EQS (48 samples, 8 weighted tests)              │
│  → Best tactical position → NavMesh navigation       │
└──────────────────────────────────────────────────────┘
```

**Plugin layer** extends Schola with EQS-specific abstractions:

```
Schola (base)                  Dynamic EQS (plugin)            Game (this project)
───────────────────────────────────────────────────────────────────────────────────
UInferenceComponent         →  UDynamicEQSAgentComponent    →  UDEScholaAgent
UBoxObserver                →  UDynamicEQSObserverBase      →  UDETacticalObserver
UBoxActuator                →  UDynamicEQSActuatorBase      →  UDETacticalParameterActuator
```

Uses `FInstancedStruct` for game-logic decoupling — zero compile-time dependency on game modules.

## Observation & Action

**Observation (218-dim entity-centric):**

| Range | Dim | Content |
|---|---|---|
| `[0:7]` | 7 | Self token — pos, velocity, health |
| `[7:71]` | 64 | Ally tokens (8x8) — rel pos, health, alive, class one-hot |
| `[71:135]` | 64 | Enemy tokens (8x8) — rel pos, health, visibility, class one-hot |
| `[135:191]` | 56 | Capture point tokens (8x7) — rel pos, ownership, progress, assignment, value |
| `[191:215]` | 24 | Padding masks (ally/enemy/base, 8 each) |
| `[215:218]` | 3 | Class one-hot (strike, vanguard, support) |

**Action (7-dim continuous):** EQS weights in [-1, 1] — enemy/ally base proximity, cover density, enemy visibility, ally proximity, combat range, assigned base proximity.

## MAPPO Training

- **3 independent Actor policies** (`strike_policy`, `vanguard_policy`, `support_policy`) with per-entity-type Self-Attention + Cross-Attention encoders
- **Centralized Critic** on 71-dim global team state (all agent positions/health/strategies + map state)
- **Dual Value Estimation:** learnable mix coefficient blends local value (agent obs) and central value (global state)
- **Curriculum:** auto-promotes ScriptedAI opponent tier (Basic → Standard → Aggressive) based on rolling win rate thresholds (55%, 65%)
- **Evaluation:** `eval_live.py` runs best vs. latest checkpoints in parallel against ScriptedAI Tier 3

## Agent Roles & Rewards

| Role | Archetype | Key Reward Signals |
|---|---|---|
| **Strike** | Ranged DPS | Base approach, zone presence, capture bonus, too-close penalty |
| **Vanguard** | Melee Tank | Base approach, zone presence, melee engagement bonus |
| **Support** | Rear Healer | Ally-chase (5-step target cache), heal reward, rear-positioning bonus |

Combat abilities use priority scoring: Attack targets weakest/highest-priority enemies (Support-class first), Heal targets lowest-health allies.

## AWS Parallel Training

| Component | Role |
|---|---|
| **Docker** | UE5 headless Linux + CUDA 12.1 base + Xvfb virtual display |
| **Ray Cluster** | Head (g4dn.xlarge GPU, on-demand) + Workers (c5.2xlarge CPU, Spot x0-4) |
| **S3** | Checkpoint sync via s3fs FUSE mount |
| **Terraform** | VPC, subnets, IAM, security groups provisioning |
| **Monitoring** | TensorBoard / W&B |

## Tech Stack

| Category | Technologies |
|---|---|
| Game Engine | UE5.6 (C++17) |
| RL Framework | Ray RLlib 2.7, PyTorch, MAPPO |
| UE5-Python Bridge | Schola Plugin (gRPC) |
| NN Inference | ONNX Runtime via UE5 NNE |
| Ability System | UE5 GAS (GameplayAbility, GameplayEffect, GameplayTag) |
| Cloud | AWS (EC2, EKS), Docker, Terraform |

## Project Structure

```
CORTEX/
├── Source/DE/                    # UE5 C++ game module
│   ├── Public/Characters/        # ADECharacter (agent pawn)
│   ├── Public/Schola/            # DEScholaAgent, Observer, Actuator
│   ├── Public/Actors/            # DECapturePoint
│   ├── Public/Team/              # DEMatchManager
│   └── Docs/                     # Detailed technical documentation
├── Source/DynamicEQS/            # Reusable EQS plugin
├── DE_Training/training/         # Python training scripts (Ray RLlib)
│   ├── train.py                  # Training entry point
│   ├── eval_live.py              # Live evaluation pipeline
│   └── policy.py                 # EntityCentricPolicy (attention-based)
└── aws/                          # Dockerfile, cluster.yaml, Terraform
```

## Prerequisites

- Unreal Engine 5.6, C++17 (MSVC 2019+)
- Schola 2.0.1 plugin
- Python 3.8+, PyTorch 2.0+, `ray[rllib]`
- (Optional) AWS CLI, Docker, Terraform for cloud training

---

**Last Updated:** 2026-03-28
