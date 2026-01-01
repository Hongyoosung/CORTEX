# CORTEX v5.0 Refactoring Plan
## Multi-Head Architecture + Individual Strategy Assignment

**Version:** 5.0 | **Date:** 2026-01-02 | **Status:** Phase 1-2 Complete

---

## Executive Summary

Transform from single shared policy to **Multi-Head PPO with Individual Strategy Assignment**:
- MCTS assigns **different strategies to different agents** based on individual context
- Single network with **4 strategy-specific output heads** (shared trunk)
- **Support strategy retained** for ally protection scenarios

---

## Architecture Overview

### Current (v4.0)
```
MCTS → Same objective to all agents → Single PPO head → Actions
```

### Proposed (v5.0)
```
MCTS → Individual objectives per agent → Strategy-gated multi-head PPO → Actions
         Agent1: Assault (healthy)
         Agent2: Defend (good position)
         Agent3: Support (ally critical)
         Agent4: Retreat (wounded)
```

### Network Architecture
```
┌──────────────────────────────────────────────────────────────────┐
│  Input: 64 features (streamlined from 74)                        │
│  ├─ Agent State (7): pos(3), vel(3), health(1)                  │
│  ├─ Combat (1): enemy_dist(1)                                   │
│  ├─ Perception (32): raycasts(16), hit_types(16)                │
│  ├─ Enemy Info (16): count(1), nearby(15)                       │
│  ├─ Tactical (4): has_cover(1), cover_dist(1), cover_dir(2)     │
│  └─ Support Context (4): ally_needs_help(1), ally_health(1),    │
│                          ally_dist(1), ally_dir(1)              │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│  Shared Trunk: [128 → 128 → 64] ReLU                            │
│  (Common features: aiming, cover usage, movement)               │
└──────────────────────────────────────────────────────────────────┘
         ↓              ↓              ↓              ↓
┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐
│  Assault    │ │   Defend    │ │   Support   │ │   Retreat   │
│    Head     │ │    Head     │ │    Head     │ │    Head     │
│ (pos,tgt,   │ │ (pos,tgt,   │ │ (pos,tgt,   │ │ (pos,tgt,   │
│  fire)      │ │  fire)      │ │  fire)      │ │  fire)      │
└─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘
         ↓              ↓              ↓              ↓
   Strategy embedding (one-hot) gates active head
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│  Shared Critic: Value estimate (1)                               │
└──────────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Data Structures (RLTypes.h)

### Changes
| Item | Current | Proposed |
|------|---------|----------|
| Strategy enum | Uses EObjectiveType | Add EStrategyType with explicit values |
| Observation | 74 features | 64 features (streamlined) |
| Network config | Single head | Multi-head config |
| Support context | None | FAllyContext struct |

### Observation Streamlining (74 → 64 features)
**Removed (-14):**
- Rotation (3) - engine handles auto-aim
- Shield (1) - not implemented
- Ammo (1) - deprecated, infinite ammo assumed
- Cooldown (1) - deprecated, handled by engine
- Weapon type (1) - deprecated, single weapon type
- Terrain type (1) - covered by raycast perception
- Objective embedding (4) - replaced by strategy head selection
- Unused padding (2)

**Added (+4):**
- Support context: ally_needs_help, ally_health, ally_dist, ally_direction

### Files to Modify
- `RL/RLTypes.h` - Enums, structs, config

---

## Phase 2: MCTS Individual Assignment

### Enhanced Objective Scoring
```cpp
float CalculateObjectiveScore(FFollowerState Agent, EStrategyType Strategy)
{
    float base = CurrentLogic();

    // Individual context modifiers
    if (Strategy == Assault && Agent.Health > 0.7f && Agent.Ammo > 0.5f)
        base += 0.3f;  // Healthy with ammo should push

    if (Strategy == Retreat && Agent.Health < 0.3f)
        base += 0.4f;  // Wounded should retreat

    if (Strategy == Support) {
        if (FFollowerState* NeedyAlly = FindAllyInNeed())
            base += 0.3f * (1.0f - NeedyAlly->Health);  // More bonus for critical allies
    }

    if (Strategy == Defend && Agent.HasCover && Agent.CoversFlanks)
        base += 0.2f;  // Good defensive position

    return base;
}
```

### Support Assignment Triggers
| Condition | Priority | Assignment |
|-----------|----------|------------|
| Ally health < 30% | High | Nearest healthy agent → Support |
| Ally surrounded (3+ enemies < 10m) | High | Best positioned agent → Support |
| Ally retreating | Medium | One agent → Support cover |
| Solo assault | Medium | One agent → Support flank |

### Files to Modify
- `AI/MCTS/MCTS.cpp` - `CalculateObjectiveScore()`, `GenerateObjectiveAssignments()`
- `Team/TeamLeaderComponent.cpp` - Individual assignment logic

---

## Phase 3: Multi-Head Network (Python)

### Network Definition
```python
class MultiHeadTacticalPolicy(nn.Module):
    def __init__(self, obs_dim=67, hidden=[128, 128, 64]):
        # Shared trunk
        self.trunk = nn.Sequential(
            nn.Linear(obs_dim, 128), nn.ReLU(),
            nn.Linear(128, 128), nn.ReLU(),
            nn.Linear(128, 64), nn.ReLU()
        )

        # Strategy-specific heads (Position, Target, Fire)
        self.assault_head = PolicyHead(64, [4, MAX_ENEMIES+1, 3])
        self.defend_head = PolicyHead(64, [4, MAX_ENEMIES+1, 3])
        self.support_head = PolicyHead(64, [4, MAX_ENEMIES+1, 3])
        self.retreat_head = PolicyHead(64, [4, MAX_ENEMIES+1, 3])

        # Shared critic
        self.critic = nn.Linear(64, 1)

    def forward(self, obs, strategy_idx):
        features = self.trunk(obs)
        heads = [self.assault_head, self.defend_head,
                 self.support_head, self.retreat_head]
        return heads[strategy_idx](features), self.critic(features)
```

### Training Modifications
- Strategy index passed with observation
- Each head trained only when that strategy is active
- Shared trunk learns common features across all strategies

### Files to Modify
- `CORTEX_Training/sbdapm_env.py` - Observation space, strategy embedding
- `CORTEX_Training/train_rllib.py` - Custom model registration

---

## Phase 4: Strategy-Specific Rewards

### Assault Rewards
| Event | Reward | Notes |
|-------|--------|-------|
| Kill enemy | +15.0 | Primary objective |
| Damage dealt | +0.1 per 10 | Incremental progress |
| Advance toward objective | +0.5/sec | Distance reduction |
| Reach objective | +20.0 | Mission success |
| Death | -8.0 | Less penalty (expected risk) |

### Defend Rewards
| Event | Reward | Notes |
|-------|--------|-------|
| Kill enemy | +10.0 | Standard |
| Maintain position (< 5m from objective) | +0.3/sec | Primary objective |
| Suppress enemy | +3.0 | Denying enemy movement |
| Leave position (> 10m) | -2.0/sec | Abandoning post |
| Death | -12.0 | Critical loss (defensive anchor) |

### Support Rewards
| Event | Reward | Notes |
|-------|--------|-------|
| Protected ally survives | +15.0 | Primary objective |
| Kill enemy threatening ally | +12.0 | Direct protection |
| Maintain support distance (5-15m from ally) | +0.2/sec | Optimal positioning |
| Ally dies while assigned | -20.0 | Failed mission |
| Draw enemy fire (aggro) | +5.0 | Distraction value |

### Retreat Rewards
| Event | Reward | Notes |
|-------|--------|-------|
| Increase distance from enemies | +0.3/sec | Primary objective |
| Reach safe zone | +10.0 | Mission success |
| Covering fire while retreating | +3.0 | Tactical bonus |
| Survival | +5.0/episode | Successful extraction |
| Death | -15.0 | Failed retreat |

### Files to Modify
- `RL/RewardCalculator.cpp` - Strategy-specific reward calculation
- `RL/RewardCalculator.h` - New reward constants

---

## Phase 5: C++ Inference Updates

### Head Selection at Runtime
```cpp
FMacroAction URLPolicyNetwork::GetAction(const FObservation& Obs, EStrategyType Strategy)
{
    // Append strategy index to input
    TArray<float> Input = Obs.ToFeatureVector();
    int32 StrategyIdx = static_cast<int32>(Strategy);

    // Run shared trunk
    TArray<float> Features = RunTrunk(Input);

    // Select appropriate head based on strategy
    TArray<float> Logits = RunHead(Features, StrategyIdx);

    return DecodeAction(Logits);
}
```

### ONNX Model Changes
- Single model with 4 output branches
- Strategy index as additional input (or use dynamic axis selection)
- Export from PyTorch with proper head indexing

### Files to Modify
- `RL/RLPolicyNetwork.cpp` - Head selection, inference
- `RL/RLPolicyNetwork.h` - Updated interface

---

## Phase 6: Support Context Observation

### Ally Context Calculation
```cpp
FAllyContext CalculateSupportContext(AAgent* Agent)
{
    FAllyContext Context;

    // Find ally most in need
    AAgent* NeedyAlly = nullptr;
    float WorstHealth = 1.0f;

    for (AAgent* Ally : GetTeammates(Agent))
    {
        if (Ally->Health < WorstHealth && Ally != Agent)
        {
            WorstHealth = Ally->Health;
            NeedyAlly = Ally;
        }
    }

    if (NeedyAlly)
    {
        Context.bAllyNeedsHelp = (WorstHealth < 0.5f);
        Context.AllyHealth = WorstHealth;
        Context.AllyDistance = FVector::Dist(Agent->GetLocation(), NeedyAlly->GetLocation());
        Context.AllyDirection = (NeedyAlly->GetLocation() - Agent->GetLocation()).GetSafeNormal();
    }

    return Context;
}
```

### Files to Modify
- `Observation/TacticalObserver.cpp` - Support context calculation
- `Observation/ObservationElement.h` - FAllyContext struct

---

## Implementation Order

| Phase | Description | Dependencies | Est. Complexity | Status |
|-------|-------------|--------------|-----------------|--------|
| **1** | RLTypes.h refactor | None | Low | ✅ Complete |
| **2** | MCTS individual assignment | Phase 1 | Medium | ✅ Complete |
| **3** | Python multi-head network | Phase 1 | Medium | ⏳ Pending |
| **4** | Strategy-specific rewards | Phase 2 | Medium | ⏳ Pending |
| **5** | C++ inference updates | Phase 3 | High | ⏳ Pending |
| **6** | Support context observation | Phase 1 | Low | ⏳ Pending |

### Critical Path
```
Phase 1 → Phase 2 ──┬──→ Phase 4 → Testing
                    │
Phase 1 → Phase 3 ──┴──→ Phase 5 → Integration

Phase 1 → Phase 6 ───────────────→ (parallel)
```

---

## Testing Strategy

### Unit Tests
1. Multi-head forward pass (each head produces valid output)
2. Strategy gating (correct head activated)
3. Individual assignment (different agents get different strategies)
4. Reward calculation per strategy

### Integration Tests
1. Full training loop with multi-head network
2. MCTS → Individual assignment → Execution pipeline
3. Support scenario: wounded ally triggers Support assignment

### Behavioral Validation
| Scenario | Expected Behavior |
|----------|-------------------|
| All healthy, clear path | Majority Assault, 1 Defend |
| One agent wounded | Wounded → Retreat, another → Support |
| Defensive position | Most agents → Defend |
| Enemy flank detected | 1-2 agents → reposition to counter |

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Training instability (4 heads) | High | Curriculum: single head first, then unlock |
| Head interference | Medium | Separate optimizers per head initially |
| Support overassignment | Medium | Cooldown on Support triggers |
| ONNX export complexity | Medium | Test export early, fallback to 4 separate models |

---

## Success Metrics

| Metric | v4.0 Baseline | v5.0 Target |
|--------|---------------|-------------|
| Win rate vs scripted AI | ~60% | >75% |
| Wounded agent survival | ~30% | >60% |
| Team coordination score | 0.4 | >0.7 |
| Strategy diversity (entropy) | 0.2 | >0.8 |

---

## Appendix: File Change Summary

| File | Changes |
|------|---------|
| `RL/RLTypes.h` | +EStrategyType, +FAllyContext, +FMultiHeadConfig |
| `AI/MCTS/MCTS.cpp` | Enhanced CalculateObjectiveScore() |
| `Team/TeamLeaderComponent.cpp` | Individual assignment logic |
| `RL/RewardCalculator.cpp` | Strategy-specific reward functions |
| `RL/RLPolicyNetwork.cpp` | Multi-head inference |
| `Observation/TacticalObserver.cpp` | Support context |
| `CORTEX_Training/sbdapm_env.py` | Observation space update |
| `CORTEX_Training/train_rllib.py` | Custom multi-head model |

---

**Document Version:** 1.0 | **Author:** Claude | **Review Status:** Pending User Approval
