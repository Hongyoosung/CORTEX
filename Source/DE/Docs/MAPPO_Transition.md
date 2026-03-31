# MAPPO Transition Plan

**Project:** DE v10.2 | **Date:** 2026-03-23

---

## 1. Overview

The current training pipeline uses **RLlib PPO with three independent per-role policies** (Strike / Vanguard / Support). Each policy has its own actor and its own **decentralized critic** that only sees that role's 226-dim observation. This is standard independent PPO (IPPO), not coordinated multi-agent training.

**MAPPO (Multi-Agent PPO)** replaces the decentralized critics with a single **centralized critic** that sees the full team global state (~70-dim `FTeamWorldState`). Actors remain per-role and decentralized — inference in UE5 is unchanged.

---

## 2. Benefits of Transitioning to MAPPO

### 2.1 Solves the Credit Assignment Problem
Cooperative rewards (`CoOccupationPenalty`, `BaseCaptureCreditReward`) are team-level signals. A decentralized critic estimating value from only one agent's perspective cannot correctly attribute these shared rewards. The centralized critic sees all 5 agents simultaneously, giving it the full context to assign credit accurately.

### 2.2 Better Value Estimates for Cooperative Play
The centralized critic is trained on `FTeamWorldState` (positions, health, strategies of all 10 agents + map state). This allows it to evaluate team-level configurations — e.g., whether spreading out across bases is more valuable than stacking — something no single-agent critic can judge.

### 2.3 Natural Fit with the Squad Commander Architecture
v10.2 already centralizes **planning** in `ASquadManager` (MCTS on `FTeamWorldState`). MAPPO centralizes **value estimation** using the same state representation. The two are architecturally complementary:
- Commander → decides tactical play
- MAPPO critic → evaluates quality of team outcome → guides actor gradient

### 2.4 Reduces Manual Reward Engineering
The `CoOccupationPenalty` exists specifically to discourage stacking, because the current decentralized critics cannot detect it. With a centralized critic that sees all agent positions, this emergent behavior becomes penalizable through value estimation rather than hand-coded reward shaping.

### 2.5 Enables Sacrificial Play Learning
In v10.2, sacrificial plays (e.g., `BaitStrategy`) are architecturally intended but difficult to train with IPPO — the "bait" agent receives negative individual reward, and its decentralized critic has no way to know whether the team benefited. The centralized critic observes the team outcome, making sacrifice-for-win a learnable signal.

### 2.6 Standard Algorithm for Cooperative MARL
MAPPO is the established baseline for cooperative multi-agent tasks (StarCraft II, Hanabi, etc.) and consistently outperforms IPPO in fully cooperative settings. DE v10.2 is fully cooperative (shared win condition, shared base objectives).

---

## 3. Necessary Steps for Transitioning to MAPPO

### Step 1 — Transmit Global State via Schola Info Channel (C++ Side)

**File:** `AI/Training/TeamDataCollector` (or a new subsystem)

The `FTeamWorldState` (~70 floats) must be sent to Python on every step. Schola's gRPC info channel already supports arbitrary float arrays per environment.

- Serialize `FTeamWorldState::ToTensor()` into a `TArray<float>` (70-dim)
- Inject it into the Schola per-environment info dict under key `"global_state"`
- This must be available at every `step()` call, not only at episode boundaries

**Reference:** `ASquadManager` already holds the current `FTeamWorldState` — read it directly rather than recomputing.

---

### Step 2 — Extract Global State in the Environment Wrapper (Python Side)

**File:** `DE_Training/training/env_wrapper.py`

- On each `step()` return, read `infos[env_id]["global_state"]` (float array, len=70)
- Store as `self._global_state[env_id]` (one per UE5 environment instance)
- Expose global state via a secondary observation key `"state"` or pass it through `info` to the model

```python
# env_wrapper.py — inside step()
global_state = infos[env_id].get("global_state", np.zeros(70, dtype=np.float32))
self._global_state[env_id] = np.array(global_state, dtype=np.float32)
```

---

### Step 3 — Implement the Centralized Critic Module (Python Side)

**File:** `DE_Training/training/policy.py`

Add a `CentralizedCritic` module with input dimension 70 (global state) instead of 226 (single-agent obs).

```python
class CentralizedCritic(nn.Module):
    """
    MAPPO centralized value function.
    Input:  70-dim FTeamWorldState  (all agents + map)
    Output: scalar value estimate
    """
    def __init__(self, state_dim: int = 70, hidden: int = 256):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(state_dim, hidden),
            nn.LayerNorm(hidden),
            nn.ReLU(),
            nn.Linear(hidden, hidden),
            nn.ReLU(),
            nn.Linear(hidden, 1),
        )

    def forward(self, global_state: torch.Tensor) -> torch.Tensor:
        # global_state: (B, 70)
        return self.net(global_state).squeeze(-1)  # (B,)
```

The existing `EntityCentricPolicy` actor is **unchanged**.

---

### Step 4 — Update the RLlib Model Wrapper (Python Side)

**File:** `DE_Training/training/train.py` — `EntityCentricRLlibModel`

- Instantiate one `CentralizedCritic` **shared across all three role policies**
- Override `value_function()` to use the global state injected via `input_dict`
- The global state must be passed in `input_dict["obs"]` as a concatenated or separate tensor, or via the `state` list (RLlib recurrent state mechanism)

```python
class EntityCentricRLlibModel(TorchModelV2, nn.Module):
    # Shared centralized critic — class-level singleton across all policy instances
    _shared_critic: Optional[CentralizedCritic] = None

    def __init__(self, obs_space, action_space, num_outputs, model_config, name):
        ...
        # Actor (per-role, unchanged)
        self.policy = _create_policy_network(hidden=hidden, heads=heads)
        # Centralized critic (shared)
        if EntityCentricRLlibModel._shared_critic is None:
            EntityCentricRLlibModel._shared_critic = CentralizedCritic(state_dim=70)
        self.critic = EntityCentricRLlibModel._shared_critic
        self._last_global_state = None

    def forward(self, input_dict, state, seq_lens):
        obs = input_dict["obs"].float()        # (B, 226) — agent obs
        # Global state is passed as the last 70 dims of a padded obs,
        # or extracted from input_dict["obs_flat"] custom slice.
        self._last_global_state = input_dict["obs"].float()[:, :70]  # see Step 5
        means = self.policy(obs)
        log_stds = ...
        return torch.cat([means, log_stds], dim=-1), state

    def value_function(self):
        return self.critic(self._last_global_state)   # (B,)
```

---

### Step 5 — Extend the Observation Space to Carry Global State

**File:** `DE_Training/training/env_wrapper.py`

RLlib requires a fixed observation space. The cleanest approach is to **append the 70-dim global state to each agent's 226-dim obs**, yielding a 296-dim flat vector.

- Observation space: `Box(-inf, inf, shape=(296,), dtype=float32)`
- Layout: `[0:226]` agent obs, `[226:296]` global state (same for all agents in the same env)
- Actor only reads `obs[:226]`; critic only reads `obs[226:296]`
- No change required to `EntityCentricPolicy.forward()` — it receives a 226-dim slice

```python
# env_wrapper.py
OBS_DIM_MAPPO = 226 + 70  # 296

self.observation_space = spaces.Box(
    low=-np.inf, high=np.inf,
    shape=(OBS_DIM_MAPPO,), dtype=np.float32
)

# In step() — per agent obs construction:
agent_obs_296 = np.concatenate([agent_obs_226, global_state_70])
```

Update `EntityCentricRLlibModel.forward()` to slice accordingly:
```python
agent_obs   = obs[:, :226]   # actor input
global_state = obs[:, 226:]  # critic input (70-dim)
```

---

### Step 6 — Update PPO Loss to Use Centralized Value Estimates

RLlib's PPO loss already uses `value_function()` for the critic. Once Steps 3–5 are complete, the centralized critic is automatically used in:
- GAE advantage computation (`use_gae=True` — already set)
- Value function loss (`vf_loss_coeff` — already configured)
- Value clipping (`vf_clip_param` — already configured)

No changes are needed to the PPO loss configuration.

---

### Step 7 — Validate with `--mode validate`

Add a MAPPO-specific validation test to `run_validation()` in `train.py`:

1. Construct a dummy 296-dim obs batch
2. Verify actor output shape: `(B, 7)` from `obs[:, :226]`
3. Verify critic output shape: `(B,)` from `obs[:, 226:]`
4. Verify that all three role policy instances share the **same** `CentralizedCritic` weights (parameter identity check)

---

## 4. Summary of Changes by File (Implemented)

| File | Change | Scope |
|---|---|---|
| `DETeamWorldState.h` (C++) | Fixed `Reserve(70)` → `Reserve(71)`, added `TENSOR_DIM = 71` constant | Bugfix |
| `AI/Training/TeamDataCollector` (C++) | Broadcast `FDETeamWorldState` via Schola info channel (key: `"global_state"`) | **TODO** |
| `env_wrapper.py` | Extract global state from info, append to agent obs (226→297); `AGENT_OBS_DIM=226`, `OBS_DIM=297` | Moderate |
| `policy.py` | Add `CentralizedCritic(state_dim=71)`, `GLOBAL_STATE_DIM=71`, `MAPPO_OBS_DIM=297` | Small addition |
| `train.py` | Dual value estimation: `V = α·V_local + (1-α)·V_central` with learnable α; shared critic across 3 policies; TensorBoard logging of α | Moderate |
| `train.py` (validate) | Tests 11-13: CentralizedCritic shape, MAPPO dim arithmetic, dual value estimation, shared critic identity | Small |

**Architecture decisions:**
- **Dual value heads** — local attention-based critic (256-dim) + centralized MLP critic (71-dim) with learnable mixing α
- **Three separate policies** retained — Strike/Vanguard/Support have opposing spatial objectives
- **ONNX export unchanged** — actor input remains 226-dim; global state suffix is stripped

---

## 5. Risk & Rollback

| Risk | Mitigation |
|---|---|
| Global state not available (C++ broadcast not implemented) | Default to `zeros(71)` in env_wrapper; critic learns on zero input until broadcast is live |
| Shared critic causes gradient interference between roles | Monitor per-role `vf_explained_var` and `mappo/value_mix_alpha`; fall back to 3 role-specific critics if needed |
| 297-dim obs breaks existing checkpoints | Checkpoints store actor weights (226-dim input unchanged); restore actor weights, reinitialize critic + mixing param |
| Mixing α collapses to 0 or 1 | Monitor `value_mix_alpha` in TensorBoard; if it saturates, clamp logit range |
| Schola gRPC info channel size limit | `FDETeamWorldState` is 71 floats = 284 bytes; well within gRPC message limits |
