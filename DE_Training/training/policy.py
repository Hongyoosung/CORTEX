"""
Entity-Centric Policy for DE v10.2+ (entity-centric refactor).

Architecture:
    Self encoder     : Linear(7, 64)
    Ally encoder     : Linear(8, 64) + MultiheadAttention(64, heads=4)
    Enemy encoder    : Linear(5, 64) + MultiheadAttention(64, heads=4)
    Base encoder     : Linear(7, 64) + MultiheadAttention(64, heads=4)
    Policy head      : Linear(64*4, 256) → ReLU → Linear(256, 128) → ReLU → Linear(128, 7) → Tanh
    Value head       : Linear(64*4, 256) → ReLU → Linear(256, 1)
    Learnable log_std: (7,)

NOTE: Strategy encoder removed. Three separate model instances are trained
per role (Assault / Defend / Support). Each model only receives observations
for its assigned role, so the strategy one-hot at [191:194] is always constant
and encoding it adds no information.

Input: 194-dim flat array (from C++ FDEObservationV2::ToFlatArray)
Layout:
    [  0:  7]  Self token (7)
    [  7: 71]  Ally tokens  8×8  (64)  — [rel_pos(3), health, alive, is_assault, is_defend, is_support]
    [ 71:111]  Enemy tokens 8×5  (40)
    [111:167]  Base tokens  8×7  (56)
    [167:175]  Ally mask    8    (0=present, 1=padding)
    [175:183]  Enemy mask   8
    [183:191]  Base mask    8
    [191:194]  Strategy one-hot  [assault, defend, support]  (3)

Output: 7-dim EQS weights in [-1, 1]
    [0] EnemyObjectiveProximity
    [1] AllyObjectiveProximity
    [2] CoverDensity
    [3] EnemyVisibility
    [4] AllyProximity
    [5] CombatRange
    [6] AssignedBaseProximity

ONNX export: fixed-shape (B, 194) → (B, 7) for UE5 NNE compatibility.
"""

import os
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from typing import List, Tuple, Dict
from dataclasses import dataclass
from collections import defaultdict

# ── Observation layout constants (must match DEObservationTypes.h) ──────────
OBS_DIM          = 194
SELF_DIM         = 7
ALLY_DIM         = 8    # [rel_pos(3), health, alive, is_assault, is_defend, is_support]
ENEMY_DIM        = 5
BASE_DIM         = 7
STRATEGY_DIM     = 3
MAX_ALLIES       = 8
MAX_ENEMIES      = 8
MAX_BASES        = 8
EQS_DIM          = 7
HIDDEN           = 64

SELF_START       = 0
ALLY_START       = 7           # 7  + 8*8 = 71
ENEMY_START      = 71          # 71 + 8*5 = 111
BASE_START       = 111         # 111 + 8*7 = 167
ALLY_MASK_START  = 167
ENEMY_MASK_START = 175
BASE_MASK_START  = 183
STRATEGY_START   = 191         # 183 + 8  = 191; [191:194] strategy one-hot

EQS_LABELS = [
    "EnemyObjectiveProximity",
    "AllyObjectiveProximity",
    "CoverDensity",
    "EnemyVisibility",
    "AllyProximity",
    "CombatRange",
    "AssignedBaseProximity",
]


# ── Transition dataclass ─────────────────────────────────────────────────────

@dataclass
class Transition:
    state:      np.ndarray   # (170,) padded flat observation
    action:     np.ndarray   # (7,)  EQS weights in [-1, 1]
    reward:     float
    next_state: np.ndarray   # (170,)
    done:       bool
    log_prob:   float = 0.0


# ── Policy network ───────────────────────────────────────────────────────────

class EntityCentricPolicy(nn.Module):
    """
    Permutation-invariant policy using per-entity-type attention encoders.

    Takes a flat 170-dim observation, unpacks entity sets, attends over each
    set independently, concatenates with the self encoding and strategy embedding,
    and passes through a shared policy/value head.
    """

    LOG_STD_MIN = -2.5   # σ_min ≈ 0.08
    LOG_STD_MAX = -0.7   # σ_max ≈ 0.50

    def __init__(self, hidden: int = HIDDEN, heads: int = 4):
        super().__init__()
        self.hidden = hidden

        # Per-entity-type linear encoders
        self.self_enc     = nn.Linear(SELF_DIM,  hidden)
        self.ally_enc     = nn.Linear(ALLY_DIM,  hidden)
        self.enemy_enc    = nn.Linear(ENEMY_DIM, hidden)
        self.base_enc     = nn.Linear(BASE_DIM,  hidden)

        # Per-entity-type cross-attention (self token queries each entity set)
        self.ally_attn  = nn.MultiheadAttention(hidden, heads, batch_first=True)
        self.enemy_attn = nn.MultiheadAttention(hidden, heads, batch_first=True)
        self.base_attn  = nn.MultiheadAttention(hidden, heads, batch_first=True)

        combined_dim = hidden * 4  # self + ally_ctx + enemy_ctx + base_ctx

        # Action head: combined → 7-dim EQS weights in [-1, 1]
        self.action_head = nn.Sequential(
            nn.Linear(combined_dim, 256), nn.ReLU(),
            nn.Linear(256, 128),          nn.ReLU(),
            nn.Linear(128, EQS_DIM),      nn.Tanh(),
        )

        # Value head
        self.value_head = nn.Sequential(
            nn.Linear(combined_dim, 256), nn.ReLU(),
            nn.Linear(256, 1),
        )

        # Learnable log standard deviation
        self.log_std = nn.Parameter(torch.zeros(EQS_DIM) - 1.5)

        total = sum(p.numel() for p in self.parameters())
        print(f"[EntityCentricPolicy] params={total:,}  hidden={hidden}  heads={heads}")

    # ── Internal helpers ─────────────────────────────────────────────────────

    def _unpack(
        self,
        flat: torch.Tensor,
    ) -> Tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor,
               torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]:
        """
        Unpack (B, 170) flat observation into entity tensors, bool masks, and strategy index.

        Returns:
            self_obs      : (B, 7)
            allies        : (B, 8, 5)
            enemies       : (B, 8, 5)
            bases         : (B, 8, 7)
            ally_mask     : (B, 8) bool — True means padding (ignore in attention)
            enemy_mask    : (B, 8) bool
            base_mask     : (B, 8) bool
            strategy_idx  : (B,)  long — argmax of the 3-dim one-hot at [167:170]
        """
        B = flat.shape[0]
        self_obs = flat[:, SELF_START:ALLY_START]                               # (B, 7)
        allies   = flat[:, ALLY_START:ENEMY_START].view(B, MAX_ALLIES, ALLY_DIM)   # (B, 8, 5)
        enemies  = flat[:, ENEMY_START:BASE_START].view(B, MAX_ENEMIES, ENEMY_DIM) # (B, 8, 5)
        bases    = flat[:, BASE_START:ALLY_MASK_START].view(B, MAX_BASES, BASE_DIM) # (B, 8, 7)

        # Masks: C++ writes 0.0=present, 1.0=padding.  PyTorch attn: True=ignore.
        # Use > 0.5 threshold (not .bool()) so 0.0 stays False and 1.0 stays True.
        ally_mask  = flat[:, ALLY_MASK_START:ENEMY_MASK_START] > 0.5   # (B, 8)
        enemy_mask = flat[:, ENEMY_MASK_START:BASE_MASK_START] > 0.5   # (B, 8)
        base_mask  = flat[:, BASE_MASK_START:STRATEGY_START] > 0.5     # (B, 8)

        # Safety: if every slot in a mask is True (all padding), unmask slot 0
        # so MultiheadAttention never receives an all-True mask (which produces NaN).
        def _safe_mask(m: torch.Tensor) -> torch.Tensor:
            all_masked = m.all(dim=1, keepdim=True)   # (B, 1)
            return m & ~all_masked                     # unmask slot 0 when all-True

        ally_mask  = _safe_mask(ally_mask)
        enemy_mask = _safe_mask(enemy_mask)
        base_mask  = _safe_mask(base_mask)

        # Strategy one-hot → index (0=Assault, 1=Defend, 2=Support)
        strategy_onehot = flat[:, STRATEGY_START:STRATEGY_START + STRATEGY_DIM]  # (B, 3)
        strategy_idx    = strategy_onehot.argmax(dim=1)                           # (B,)

        return self_obs, allies, enemies, bases, ally_mask, enemy_mask, base_mask, strategy_idx

    def _encode(
        self,
        flat: torch.Tensor,
    ) -> torch.Tensor:
        """
        Encode flat obs into combined feature vector.

        Returns: (B, hidden*5)
        """
        self_obs, allies, enemies, bases, ally_mask, enemy_mask, base_mask, _ = self._unpack(flat)

        s = self.self_enc(self_obs)               # (B, hidden)
        q = s.unsqueeze(1)                        # (B, 1, hidden) — query for attention

        a_enc = self.ally_enc(allies)             # (B, 8, hidden)
        e_enc = self.enemy_enc(enemies)           # (B, 8, hidden)
        b_enc = self.base_enc(bases)              # (B, 8, hidden)

        # key_padding_mask: True = ignore (padding slot)
        a_ctx, _ = self.ally_attn(q, a_enc, a_enc,
                                  key_padding_mask=ally_mask)    # (B, 1, hidden)
        e_ctx, _ = self.enemy_attn(q, e_enc, e_enc,
                                   key_padding_mask=enemy_mask)  # (B, 1, hidden)
        b_ctx, _ = self.base_attn(q, b_enc, b_enc,
                                  key_padding_mask=base_mask)    # (B, 1, hidden)

        return torch.cat([s,
                          a_ctx.squeeze(1),
                          e_ctx.squeeze(1),
                          b_ctx.squeeze(1)], dim=-1)             # (B, hidden*4)

    # ── Public interface ─────────────────────────────────────────────────────

    def forward(self, flat_obs: torch.Tensor) -> torch.Tensor:
        """
        Args:
            flat_obs: (B, 170) padded flat observation

        Returns:
            eqs_weights: (B, 7) in range [-1, 1]
        """
        return self.action_head(self._encode(flat_obs))

    def get_value(self, flat_obs: torch.Tensor) -> torch.Tensor:
        """Returns value estimates (B,)."""
        return self.value_head(self._encode(flat_obs)).squeeze(-1)

    def get_std(self) -> torch.Tensor:
        """Current standard deviation (7,)."""
        return torch.exp(torch.clamp(self.log_std, self.LOG_STD_MIN, self.LOG_STD_MAX))

    def sample_action(
        self, flat_obs: torch.Tensor
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        """
        Sample actions from Gaussian policy.

        Returns:
            actions   : (B, 7) clamped to [-1, 1]
            log_probs : (B,)
        """
        means = self.forward(flat_obs)
        dist  = torch.distributions.Normal(means, self.get_std())
        raw   = dist.rsample()
        lp    = dist.log_prob(raw).sum(dim=-1)
        return raw.clamp(-1.0, 1.0), lp

    def compute_log_prob(
        self, flat_obs: torch.Tensor, actions: torch.Tensor
    ) -> torch.Tensor:
        """Returns log probabilities (B,) for given actions."""
        means = self.forward(flat_obs)
        dist  = torch.distributions.Normal(means, self.get_std())
        return dist.log_prob(actions).sum(dim=-1)

    # ── ONNX export ──────────────────────────────────────────────────────────

    def export_onnx(self, filepath: str, batch_size: int = 1):
        """
        Export to ONNX for UE5 NNE inference (fixed shape: B×170 → B×7).

        Args:
            filepath   : Output .onnx path
            batch_size : Export batch size (UE5 NNE typically uses 1)
        """
        self.eval()
        dummy = torch.zeros(batch_size, OBS_DIM)
        torch.onnx.export(
            self,
            dummy,
            filepath,
            input_names=['observation'],
            output_names=['eqs_weights'],
            dynamic_axes={
                'observation': {0: 'batch_size'},
                'eqs_weights': {0: 'batch_size'},
            },
            opset_version=14,
        )
        print(f"[ONNX] Exported → {filepath}  (input: B×{OBS_DIM}, output: B×{EQS_DIM})")  # B×194 → B×7


# ── Replay buffer ─────────────────────────────────────────────────────────────

class ReplayBuffer:
    """Fixed-capacity replay buffer."""

    def __init__(self, capacity: int = 100_000):
        from collections import deque
        self._buf: deque = defaultdict(lambda: None)
        self._buf = []
        self._maxlen = capacity

    def add(self, t: Transition):
        if len(self._buf) >= self._maxlen:
            self._buf.pop(0)
        self._buf.append(t)

    def sample(self, n: int) -> List[Transition]:
        idx = np.random.choice(len(self._buf), min(n, len(self._buf)), replace=False)
        return [self._buf[i] for i in idx]

    def __len__(self) -> int:
        return len(self._buf)


# ── PPO Trainer ───────────────────────────────────────────────────────────────

class PPOTrainer:
    """
    Standalone PPO trainer for EntityCentricPolicy.

    For RLlib-based training see train.py::create_ppo_config().
    """

    def __init__(
        self,
        policy: EntityCentricPolicy,
        learning_rate: float = 3e-4,
        clip_epsilon: float = 0.2,
        value_coef: float = 0.5,
        entropy_coef: float = 0.01,
        max_grad_norm: float = 0.5,
        device: str = 'cuda' if torch.cuda.is_available() else 'cpu',
    ):
        self.policy     = policy.to(device)
        self.device     = device
        self.clip_eps   = clip_epsilon
        self.val_coef   = value_coef
        self.ent_coef   = entropy_coef
        self.max_gnorm  = max_grad_norm
        self.optimizer  = optim.Adam(policy.parameters(), lr=learning_rate)

    def compute_advantages(
        self,
        rewards: torch.Tensor,
        values:  torch.Tensor,
        dones:   torch.Tensor,
        gamma:   float = 0.99,
        lam:     float = 0.95,
    ) -> Tuple[torch.Tensor, torch.Tensor]:
        """Compute GAE advantages and returns."""
        adv = torch.zeros_like(rewards)
        last_adv = 0.0
        for t in reversed(range(len(rewards))):
            nv   = 0.0 if t == len(rewards) - 1 else values[t + 1].item()
            d    = rewards[t] + gamma * nv * (1 - dones[t]) - values[t]
            adv[t] = d + gamma * lam * (1 - dones[t]) * last_adv
            last_adv = adv[t].item()
        return adv, adv + values

    def update(
        self,
        batch:  List[Transition],
        epochs: int = 10,
    ) -> Dict[str, float]:
        """Run PPO update epochs over batch. Returns mean metrics dict."""
        states    = torch.tensor(np.array([t.state  for t in batch]), dtype=torch.float32, device=self.device)
        actions   = torch.tensor(np.array([t.action for t in batch]), dtype=torch.float32, device=self.device)
        rewards   = torch.tensor([t.reward    for t in batch],        dtype=torch.float32, device=self.device)
        dones     = torch.tensor([float(t.done) for t in batch],      dtype=torch.float32, device=self.device)
        old_lp    = torch.tensor([t.log_prob  for t in batch],        dtype=torch.float32, device=self.device)

        with torch.no_grad():
            old_vals = self.policy.get_value(states)

        adv, returns = self.compute_advantages(rewards, old_vals, dones)
        adv = (adv - adv.mean()) / (adv.std() + 1e-8)

        metrics = defaultdict(list)
        for _ in range(epochs):
            new_lp  = self.policy.compute_log_prob(states, actions)
            new_val = self.policy.get_value(states)
            std     = self.policy.get_std()
            entropy = (0.5 * torch.log(2 * np.pi * np.e * std ** 2)).sum()

            ratio  = torch.exp(new_lp - old_lp)
            surr1  = ratio * adv
            surr2  = torch.clamp(ratio, 1 - self.clip_eps, 1 + self.clip_eps) * adv
            p_loss = -torch.min(surr1, surr2).mean()
            v_loss = nn.MSELoss()(new_val, returns)
            loss   = p_loss + self.val_coef * v_loss - self.ent_coef * entropy

            self.optimizer.zero_grad()
            loss.backward()
            nn.utils.clip_grad_norm_(self.policy.parameters(), self.max_gnorm)
            self.optimizer.step()

            metrics['policy_loss'].append(p_loss.item())
            metrics['value_loss'].append(v_loss.item())
            metrics['entropy'].append(entropy.item())
            metrics['total_loss'].append(loss.item())
            metrics['approx_kl'].append((old_lp - new_lp).mean().item())

        return {k: float(np.mean(v)) for k, v in metrics.items()}


# ── Collation helper for variable-entity batches ──────────────────────────────

def collate_fn(obs_list: List[np.ndarray]) -> torch.Tensor:
    """
    Stack a list of 170-dim flat observations into a (B, 170) tensor.
    All observations are already fixed-shape (padded by C++), so this is
    a straightforward stack. The function exists as a named hook so callers
    can replace it if the serialization changes.
    """
    arr = np.stack(obs_list, axis=0).astype(np.float32)
    return torch.from_numpy(arr)
