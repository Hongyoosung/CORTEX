"""
DE Entity-Centric Environment Wrapper — Schola 2.0.1

Wraps Schola/UE5 connection for the entity-centric refactor (v10.2+).

Key differences from the old v10.2 wrapper:
  - Observation: 226-dim flat (from C++ FDEObservationV2::ToFlatArray)
  - Action:       7-dim EQS weights in [-1, 1]  (7th = AssignedBaseProximity)
  - No strategy routing: single EntityCentricPolicy handles all agents
  - No uniform strategy assignment: strategies come from UE5 Squad Commander
    and are encoded implicitly in the base tokens (is_assigned_target field)

Schola 2.0.1 API:
  - Connection: gRPCProtocol(url, port) + UnrealEditor()
  - Init:       protocol.start() → simulator.start() → send_startup_msg()
  - Definition: ids, agent_types, obs_defns, action_defns = get_definition()
  - Reset:      observations, infos = send_reset_msg()
  - Step:       obs, rew, term, trunc, infos, init_obs, init_info = send_action_msg()
  - IDs:        agent IDs are strings from C++ (e.g., "env0_agent0")
  - Data:       obs[env_id][agent_id_str], rew[env_id][agent_id_str], etc.

Reward source:
  - Primary: CumulativeLifetimeReward delta from info channel
    (captures cooperative base rewards that Schola may drop on SAME_STEP resets)
  - Fallback: Schola step reward
"""

from gymnasium import spaces
import numpy as np
import os
import time
from typing import Dict, Tuple, Optional
from collections import deque

try:
    from ray.rllib.env.multi_agent_env import MultiAgentEnv
    RLLIB_AVAILABLE = True
except ImportError:
    RLLIB_AVAILABLE = False
    class MultiAgentEnv:
        pass

try:
    from schola.core.protocols.protobuf.gRPC import gRPCProtocol
    from schola.core.simulators.unreal.editor import UnrealEditor
    from gymnasium.vector.vector_env import AutoresetMode
    SCHOLA_AVAILABLE = True
except ImportError:
    SCHOLA_AVAILABLE = False
    print("Warning: schola not installed. Run: pip install schola")


# ── Layout constants (must match DEObservationTypes.h) ───────────────────────
# DE_OBS_V2_DIM = 7 + 8*9 + 8*8 + 8*7 + 8+8+8 + 3 = 226
# Layout: Self(7) + Allies(8×9=72) + Enemies(8×8=64) + Bases(8×7=56) +
#         AllyMask(8) + EnemyMask(8) + BaseMask(8) + Strategy(3)
# Strategy one-hot starts at: 7+72+64+56+24 = 223
AGENT_OBS_DIM = 226
ACTION_DIM    = 7

# MAPPO: global state from FDETeamWorldState::ToTensor() = 71-dim
# Appended after agent obs → total 297-dim
GLOBAL_STATE_DIM = 71
OBS_DIM          = AGENT_OBS_DIM + GLOBAL_STATE_DIM   # 297

# ── Strategy registry ─────────────────────────────────────────────────────────
# Maps agent_id_str → strategy index (0=Strike, 1=Vanguard, 2=Support).
# Populated by DEEntityCentricEnv on reset() and step() so that train.py's
# policy_mapping_fn can route each agent to the correct per-role policy.
# Works correctly when num_workers=0 (all code in one process).
AGENT_STRATEGY_REGISTRY: Dict[str, int] = {}
STRATEGY_ONEHOT_START = 223  # obs[223:226] = strategy one-hot [strike, vanguard, support]


class DEEntityCentricEnv(MultiAgentEnv):
    """
    DE Entity-Centric Multi-Agent Environment (Schola 2.0.1).

    Connects to UE5 via Schola gRPC. All agents share a single
    EntityCentricPolicy (no per-strategy routing).

    Works regardless of whether Schola is installed — raises ImportError at
    instantiation time (not at import time) if schola is missing.
    """

    def __init__(self, **kwargs):
        if not SCHOLA_AVAILABLE:
            raise ImportError(
                "schola package is not installed. "
                "Run: pip install /path/to/Schola-2.0.1/Resources/python[rllib]"
            )

        super().__init__()

        host           = kwargs.get("host", "localhost")
        port           = self._resolve_port(kwargs)
        self.num_envs  = kwargs.get("num_envs", 4)

        # Individual-respawn guard: wait this many seconds after send_reset_msg()
        # before sending the first step action.  Individual respawns mean agents
        # arrive at different times; if the first send_action_msg arrives while
        # some agents are still spawning, Schola's GymConnector times out and
        # skips the step (returning nothing → Python hangs indefinitely).
        # Override via POST_RESET_DELAY env-var or 'post_reset_delay' kwarg.
        self._post_reset_delay: float = float(
            kwargs.get("post_reset_delay",
                       os.environ.get("POST_RESET_DELAY", "4.0"))
        )

        print(f"[DEEntityCentricEnv] Connecting to {host}:{port}  "
              f"num_envs={self.num_envs}")
        print(f"[DEEntityCentricEnv] Obs={OBS_DIM}-dim (agent={AGENT_OBS_DIM} + global={GLOBAL_STATE_DIM}), Action={ACTION_DIM}-dim EQS")

        try:
            self._protocol = gRPCProtocol(url=host, port=port)
            self._simulator = UnrealEditor()
            self._protocol.start()
            self._simulator.start(self._protocol.properties)
            self._protocol.send_startup_msg(auto_reset_type=AutoresetMode.DISABLED)

            self._ids, _, obs_defns, self._action_defns = self._protocol.get_definition()

            self._single_action_spaces = {}
            _defns_iterable = self._action_defns.values() if isinstance(self._action_defns, dict) else self._action_defns
            for env_spaces in _defns_iterable:
                self._single_action_spaces.update(env_spaces)

            print("[DEEntityCentricEnv] Connected.")

            actual = len(self._ids)
            if actual != self.num_envs:
                print(f"[DEEntityCentricEnv] Auto-adjust: expected {self.num_envs}, "
                      f"got {actual} from UE5")
                self.num_envs = actual

            total = sum(len(a) for a in self._ids)
            print(f"[DEEntityCentricEnv] {self.num_envs} envs, {total} agents total")

        except Exception as e:
            print(f"[DEEntityCentricEnv] Connection failed: {e}")
            raise

        # Agent ID mapping: agent_id_str (C++) ↔ (env_idx, agent_id_str)
        # flat_id == agent_id_str (C++ string like "env0_agent0")
        self.agent_map:    Dict[str, Tuple[int, str]] = {}
        self.reverse_map:  Dict[Tuple[int, str], str] = {}
        self._agent_ids:   set = set()
        self._update_agent_map()

        # Spaces
        self._obs_space = spaces.Box(
            low=-np.inf, high=np.inf,
            shape=(OBS_DIM,),
            dtype=np.float32,
        )
        self._action_space = spaces.Box(
            low=-1.0, high=1.0,
            shape=(ACTION_DIM,),
            dtype=np.float32,
        )

        # Episode tracking
        self._first_reset_done   = False
        self._training_start_time = None
        self._env_episode_steps   = {i: 0 for i in range(self.num_envs)}
        self._env_episode_start   = {i: None for i in range(self.num_envs)}
        self._env_episodes_done   = {i: 0 for i in range(self.num_envs)}
        self._env_done_flags      = {i: False for i in range(self.num_envs)}
        self._agent_ep_rewards:   Dict[str, float] = {}
        self._prev_cumulative:    Dict[str, float] = {}
        self._reward_debug_count  = 0

        # Win rate tracking (for curriculum scheduler)
        self._rl_episode_wins:  int = 0
        self._rl_episodes_total: int = 0

        # Per-strategy reward tracking
        # Keys: 0=Strike, 1=Vanguard, 2=Support  (StrategyType from UE5 info)
        STRATEGY_NAMES = {0: "strike", 1: "vanguard", 2: "support"}
        self._strategy_names = STRATEGY_NAMES
        self._agent_strategy: Dict[str, int] = {}
        self._strategy_ep_rewards: Dict[int, list] = {k: [] for k in STRATEGY_NAMES}
        self._step_rewards_by_strategy: Dict[int, list] = {k: [] for k in STRATEGY_NAMES}

        # Per reward-component tracking (UE5 may send these in info)
        REWARD_COMPONENTS = [
            "BaseOccupationReward", "CoOccupationPenalty",
            "BaseCaptureCreditReward",
            "AssignedBaseReachReward",
        ]
        self._reward_components = REWARD_COMPONENTS
        self._component_ep_sums: Dict[str, list] = {c: [] for c in REWARD_COMPONENTS}

        # MAPPO: per-env global state cache (71-dim FDETeamWorldState)
        self._global_state: Dict[int, np.ndarray] = {
            i: np.zeros(GLOBAL_STATE_DIM, dtype=np.float32) for i in range(self.num_envs)
        }

        self._completed_envs: set       = set()   # sub-envs whose match has ended
        self._schola_env_done: Dict[int, str] = {}  # per-step: sub-envs Schola terminated
        self._max_episode_steps        = 1000
        self._max_episode_steps_synced = False
        self._force_timeout            = True

        self.step_durations = deque(maxlen=100)
        self.poll_durations = deque(maxlen=100)
        self._last_step_return_time = None
        self._step_call_count       = 0
        self._total_step_count      = 0

        print("[DEEntityCentricEnv] Init complete.")

    # ── Spaces ───────────────────────────────────────────────────────────

    @property
    def observation_space(self):
        return self._obs_space

    @property
    def action_space(self):
        return self._action_space

    # ── Agent map ────────────────────────────────────────────────────────

    def _resolve_port(self, kwargs) -> int:
        base = kwargs.get("base_port")
        if base is not None:
            try:
                from ray.rllib.evaluation.rollout_worker import get_global_worker
                w = get_global_worker()
                return base + max(0, (w.worker_index if w else 0) - 1)
            except Exception:
                return base
        return kwargs.get("port", 50051)

    def _update_agent_map(self):
        self.agent_map.clear()
        self.reverse_map.clear()
        self._agent_ids.clear()
        # In Schola 2.0.1, _ids[env_idx] is a list of string agent IDs from C++
        # e.g., ["env0_agent0", "env0_agent1", ...]
        for env_idx, agent_list in enumerate(self._ids):
            for agent_id_str in agent_list:
                self.agent_map[agent_id_str] = (env_idx, agent_id_str)
                self.reverse_map[(env_idx, agent_id_str)] = agent_id_str
                self._agent_ids.add(agent_id_str)
        print(f"[DEEntityCentricEnv] Agent map: {len(self._agent_ids)} agents")

    def _agents_for_env(self, env_idx: int):
        return [a for a, (eidx, _) in self.agent_map.items() if eidx == env_idx]

    # ── MAPPO global state ────────────────────────────────────────────────

    def _extract_global_state(self, info_nested, env_idx: int) -> np.ndarray:
        """
        Extract FDETeamWorldState tensor from Schola info channel.

        The C++ side broadcasts the 71-dim global state under key "global_state"
        in each agent's info dict as a comma-separated float string.
        All agents in the same team share the same global state, so we read
        from the first agent that has the key.

        Falls back to zeros if unavailable (e.g., before UE5 build is updated).

        Returns: (GLOBAL_STATE_DIM,) float32
        """
        if isinstance(info_nested, list):
            env_info = info_nested[env_idx] if env_idx < len(info_nested) else {}
        else:
            env_info = info_nested.get(env_idx, {})

        # global_state is in each agent's info dict as a comma-separated string
        gs_raw = None
        if isinstance(env_info, dict):
            # Check each agent's info for the key
            for agent_info in env_info.values():
                if isinstance(agent_info, dict):
                    gs_raw = agent_info.get("global_state")
                    if gs_raw is not None:
                        break

        if gs_raw is not None:
            try:
                if isinstance(gs_raw, str):
                    # C++ sends comma-separated floats: "0.123,0.456,..."
                    arr = np.fromstring(gs_raw, sep=',', dtype=np.float32)
                else:
                    # May already be a list/array if Schola parses it
                    arr = np.asarray(gs_raw, dtype=np.float32).flatten()

                if len(arr) >= GLOBAL_STATE_DIM:
                    return arr[:GLOBAL_STATE_DIM]
                elif len(arr) > 0:
                    return np.pad(arr, (0, GLOBAL_STATE_DIM - len(arr)))
            except (ValueError, TypeError):
                pass

        return self._global_state.get(env_idx, np.zeros(GLOBAL_STATE_DIM, dtype=np.float32))

    def _append_global_state(self, obs_d: dict) -> dict:
        """
        Append per-env global state (71-dim) to each agent's 226-dim obs,
        producing 297-dim MAPPO observations.

        All agents in the same sub-env share the same global state suffix.
        """
        mappo_obs = {}
        for fid, agent_obs in obs_d.items():
            env_idx = self.agent_map[fid][0]
            gs = self._global_state.get(env_idx, np.zeros(GLOBAL_STATE_DIM, dtype=np.float32))
            mappo_obs[fid] = np.concatenate([agent_obs, gs])
        return mappo_obs

    # ── Observation extraction ────────────────────────────────────────────

    def _extract_obs(
        self,
        flat_id:    str,
        obs_nested,
        info_nested,
    ) -> Tuple[np.ndarray, dict]:
        """
        Extract a single agent's observation and info dict from Schola 2.0.1
        nested data. Returns AGENT_OBS_DIM (226) obs — global state is
        appended later by _append_global_state().

        obs_nested: List[Dict[agent_id_str, obs]] or Dict[env_id, Dict[agent_id_str, obs]]
        info_nested: same structure, values are Dict[str,str]

        Returns:
            obs  : (AGENT_OBS_DIM,) float32  (226-dim, without global state)
            info : dict
        """
        env_idx, agent_id_str = self.agent_map[flat_id]

        # Support both list (reset) and dict (step) indexing
        if isinstance(obs_nested, list):
            env_data = obs_nested[env_idx] if env_idx < len(obs_nested) else {}
        else:
            env_data = obs_nested.get(env_idx, {})
        raw = env_data.get(agent_id_str, None)

        if raw is not None:
            if isinstance(raw, dict):
                raw = list(raw.values())[0]
            obs = np.asarray(raw, dtype=np.float32).flatten()
        else:
            obs = np.zeros(AGENT_OBS_DIM, dtype=np.float32)

        # Pad or truncate to exactly AGENT_OBS_DIM (226)
        if len(obs) < AGENT_OBS_DIM:
            obs = np.pad(obs, (0, AGENT_OBS_DIM - len(obs)))
        else:
            obs = obs[:AGENT_OBS_DIM]

        if isinstance(info_nested, list):
            info_env = info_nested[env_idx] if env_idx < len(info_nested) else {}
        else:
            info_env = info_nested.get(env_idx, {})
        raw_info = info_env.get(agent_id_str, {})
        info = {"env_id": env_idx, **(raw_info if isinstance(raw_info, dict) else {})}

        return obs, info

    def _format_actions(self, actiondict: dict) -> dict:
        """
        Convert {flat_id: action_array} → {env_id: {agent_id_str: action}} for
        send_action_msg. Handles both Box and Dict action spaces.
        """
        fmt = {}
        for fid, action in actiondict.items():
            if fid not in self.agent_map:
                continue
            ei, ai = self.agent_map[fid]
            if ei not in fmt:
                fmt[ei] = {}

            if isinstance(action, np.ndarray) and len(action) == ACTION_DIM:
                a = np.clip(action, -1.0, 1.0).astype(np.float32)
            else:
                a = np.zeros(ACTION_DIM, dtype=np.float32)

            # Adapt to action space format
            space = self._single_action_spaces.get(ai)
            if space is not None and hasattr(space, 'spaces'):
                # Dict action space — wrap in {key: array}
                keys = list(space.spaces.keys())
                if keys:
                    fmt[ei][ai] = {k: a for k in keys}
                else:
                    fmt[ei][ai] = a
            else:
                # Box or unknown — pass array directly
                fmt[ei][ai] = a

        # Pad completed sub-envs with dummy actions so the protocol
        # message always contains entries for every environment.
        for ei in range(self.num_envs):
            if ei not in fmt:
                fmt[ei] = {}
                for aid in self._agents_for_env(ei):
                    _, agent_id_str = self.agent_map[aid]
                    zeros = np.zeros(ACTION_DIM, dtype=np.float32)
                    space = self._single_action_spaces.get(agent_id_str)
                    if space is not None and hasattr(space, 'spaces'):
                        keys = list(space.spaces.keys())
                        fmt[ei][agent_id_str] = {k: zeros for k in keys} if keys else zeros
                    else:
                        fmt[ei][agent_id_str] = zeros

        # Sort by env_id so protobuf repeated field positions match UE5 env indices.
        return {k: fmt[k] for k in sorted(fmt.keys())}

    # ── Core interface ────────────────────────────────────────────────────

    def reset(self, *, seed=None, options=None):
        t0 = time.time()
        print(f"[RESET] time={time.strftime('%H:%M:%S')}  "
              f"first={not self._first_reset_done}")

        now = time.time()
        if self._training_start_time is None:
            self._training_start_time = now

        # Reset all tracking state
        for ei in range(self.num_envs):
            self._env_episode_steps[ei] = 0
            self._env_episode_start[ei]  = now
            self._env_done_flags[ei]     = False
        self._completed_envs.clear()
        for fid in self._agent_ids:
            self._agent_ep_rewards[fid] = 0.0
        self._prev_cumulative.clear()

        # Always send a full reset to C++ (DISABLED mode requires explicit resets)
        obs_list, infos_list = self._protocol.send_reset_msg()

        # Individual-respawn guard: Schola returns as soon as the reset message
        # is acknowledged, but UE5 spawns each agent separately.  Sending the
        # first action before all agents have finished BeginPlay causes
        # GymConnector to time out and return nothing, hanging send_action_msg.
        if self._post_reset_delay > 0.0:
            print(f"[RESET] Waiting {self._post_reset_delay:.1f}s for individual spawns…")
            time.sleep(self._post_reset_delay)

        if not self._first_reset_done:
            self._first_reset_done = True
            self._update_agent_map()
            for fid in self._agent_ids:
                self._agent_ep_rewards[fid] = 0.0

        obs_d, info_d = self._process_obs(obs_list, infos_list)

        # Extract per-env global state from info channel (MAPPO)
        for ei in range(self.num_envs):
            self._global_state[ei] = self._extract_global_state(infos_list, ei)

        # Append global state to each agent obs (226 → 297)
        obs_d = self._append_global_state(obs_d)

        # Populate strategy registry from reset obs so policy_mapping_fn
        # can route agents to the correct per-role policy immediately.
        strat_counts = {0: 0, 1: 0, 2: 0}
        for fid, obs in obs_d.items():
            one_hot = obs[STRATEGY_ONEHOT_START:STRATEGY_ONEHOT_START + 3]
            strategy_idx = int(np.argmax(one_hot))
            AGENT_STRATEGY_REGISTRY[fid] = strategy_idx
            self._agent_strategy[fid] = strategy_idx
            strat_counts[strategy_idx] = strat_counts.get(strategy_idx, 0) + 1
        STRAT_NAMES = {0: "strike", 1: "vanguard", 2: "support"}
        print(f"[RESET] Strategy distribution from obs one-hot: "
              f"{{{', '.join(f'{STRAT_NAMES[k]}={v}' for k,v in strat_counts.items())}}}")
        # Sample one obs to show actual one-hot values for diagnosis
        if obs_d:
            sample_id = next(iter(obs_d))
            print(f"[RESET] Sample obs[223:226] for {sample_id}: "
                  f"{obs_d[sample_id][STRATEGY_ONEHOT_START:STRATEGY_ONEHOT_START+3]}")

        # Sync cumulative baseline to avoid phantom delta on first step
        for fid in self._agent_ids:
            clf = info_d.get(fid, {}).get('CumulativeLifetimeReward')
            if clf is not None:
                try:
                    self._prev_cumulative[fid] = float(clf)
                except (ValueError, TypeError):
                    pass

        print(f"[RESET] Complete ({time.time()-t0:.2f}s, agents={len(obs_d)})")
        return obs_d, info_d

    def step(self, actiondict: dict):
        self._step_call_count += 1
        step_start = time.time()

        if self._last_step_return_time is not None:
            gap = step_start - self._last_step_return_time
            if gap > 30.0:
                print(f"[FREEZE] RLlib took {gap:.1f}s between steps "
                      f"(step #{self._step_call_count})")
            elif gap > 5.0:
                print(f"[FREEZE] RLlib gap {gap:.1f}s at step #{self._step_call_count}")

        try:
            fmt = self._format_actions(actiondict)

            self._total_step_count += 1
            steps_s0 = self._env_episode_steps.get(0, 0)
            if self._total_step_count <= 5 or self._total_step_count % 500 == 0 or steps_s0 <= 2:
                print(f"[STEP {self._total_step_count}] poll()  "
                      f"env_steps={dict(self._env_episode_steps)}")

            poll_t = time.time()
            # Schola 2.0.1: send_action_msg returns 7 values
            result = self._protocol.send_action_msg(fmt, self._single_action_spaces)
            self.poll_durations.append(time.time() - poll_t)

            obs_d, rew_d, term_d, trunc_d, info_d = self._parse_full(result)

            # Extract per-env global state from info channel (MAPPO)
            info_n = result[4]
            for ei in range(self.num_envs):
                self._global_state[ei] = self._extract_global_state(info_n, ei)

            # Append global state to each agent obs (226 → 297)
            obs_d = self._append_global_state(obs_d)

            if not self._max_episode_steps_synced:
                self._sync_max_steps(info_d)

            # Filter out agents from already-completed sub-envs — RLlib
            # already received their terminal transition; don't send stale data.
            for ei in list(self._completed_envs):
                for aid in self._agents_for_env(ei):
                    obs_d.pop(aid, None)
                    rew_d.pop(aid, None)
                    info_d.pop(aid, None)
                    # term/trunc already False from suppression; leave as-is

            for ei in range(self.num_envs):
                if ei in self._completed_envs:
                    continue  # already done — skip

                agents = self._agents_for_env(ei)
                if not agents:
                    continue

                self._env_episode_steps[ei] += 1
                for aid in agents:
                    self._agent_ep_rewards[aid] = (
                        self._agent_ep_rewards.get(aid, 0.0) + rew_d.get(aid, 0.0)
                    )

                # Check for match end: Schola env-level termination OR MatchEnded info
                schola_ended = ei in self._schola_env_done
                match_ended_info = any(
                    str(info_d.get(aid, {}).get('MatchEnded', 'false')).lower() == 'true'
                    for aid in agents
                )
                ue5_end = schola_ended or match_ended_info
                timed_out = (self._force_timeout and
                             self._env_episode_steps[ei] >= self._max_episode_steps)

                if ue5_end or timed_out:
                    if schola_ended:
                        reason = f"Schola {self._schola_env_done[ei]} (all agents in sub-env)"
                    elif match_ended_info:
                        reason = "MatchEnded info from UE5"
                    else:
                        reason = f"timeout({self._max_episode_steps})"
                    print(f"[STEP] Episode end: env={ei} step={self._env_episode_steps[ei]} — {reason}")
                    for aid in agents:
                        if ue5_end:
                            term_d[aid] = True
                        else:
                            trunc_d[aid] = True
                    self._log_episode(ei, agents, term_d, trunc_d)
                    self._completed_envs.add(ei)
                    self._env_episodes_done[ei] += 1

            all_done = len(self._completed_envs) == self.num_envs
            if all_done:
                term_d['__all__']  = True
                trunc_d['__all__'] = True

            # Periodic progress log
            if any(self._env_episode_steps[i] % 100 == 0 and
                   self._env_episode_steps[i] > 0
                   for i in range(self.num_envs)
                   if i not in self._completed_envs):
                self._log_progress()

            # NaN/Inf guard
            for aid, r in rew_d.items():
                if aid != '__all__' and (np.isnan(r) or np.isinf(r)):
                    print(f"[NaN GUARD] reward for {aid} = {r} at step {self._step_call_count}")
                    rew_d[aid] = 0.0
            for aid, o in obs_d.items():
                if isinstance(o, np.ndarray) and (np.any(np.isnan(o)) or np.any(np.isinf(o))):
                    bad = np.where(np.isnan(o) | np.isinf(o))[0]
                    print(f"[NaN GUARD] obs for {aid}: bad indices={bad}")
                    obs_d[aid] = np.nan_to_num(o)

            # Inject custom_metrics so RLlib aggregates them in env_runners
            cm = self._pop_custom_metrics()
            if cm:
                for fid in list(info_d.keys()):
                    if fid != '__all__':
                        info_d.setdefault(fid, {})["custom_metrics"] = cm

            self.step_durations.append(time.time() - step_start)
            self._last_step_return_time = time.time()
            return obs_d, rew_d, term_d, trunc_d, info_d

        except Exception as e:
            import traceback
            print(f"[STEP ERROR] {e}")
            traceback.print_exc()
            return self._terminal_fallback()

    # ── Parsing helpers ───────────────────────────────────────────────────

    def _process_obs(self, obs_list, infos_list) -> Tuple[dict, dict]:
        """Parse obs from send_reset_msg() result (List[Dict] format)."""
        obs_d = {}
        info_d = {}
        for fid in self._agent_ids:
            obs_d[fid], info_d[fid] = self._extract_obs(fid, obs_list, infos_list)
        return obs_d, info_d

    def _parse_obs_and_info(self, result) -> Tuple[dict, dict]:
        """Minimal parse for soft reset — result is 7-tuple from send_action_msg."""
        # result: (obs, rew, term, trunc, infos, initial_obs, initial_info)
        obs_n   = result[0]
        info_n  = result[4]
        obs_d = {}
        info_d = {}
        for fid in self._agent_ids:
            obs_d[fid], info_d[fid] = self._extract_obs(fid, obs_n, info_n)
        # Sync cumulative baseline to avoid phantom delta on next episode's first step
        for fid in self._agent_ids:
            clf = info_d.get(fid, {}).get('CumulativeLifetimeReward')
            if clf is not None:
                try:
                    self._prev_cumulative[fid] = float(clf)
                except (ValueError, TypeError):
                    pass
        return obs_d, info_d

    def _parse_full(self, result) -> Tuple[dict, dict, dict, dict, dict]:
        """Full step result parse from send_action_msg() 7-tuple."""
        # result: (obs, rew, term, trunc, infos, initial_obs, initial_info)
        obs_n, rew_n, term_n, trunc_n, info_n = result[0], result[1], result[2], result[3], result[4]

        obs_d = {}; rew_d = {}; term_d = {}; trunc_d = {}; info_d = {}

        for fid in self._agent_ids:
            ei, ai = self.agent_map[fid]
            obs_d[fid],  info_d[fid] = self._extract_obs(fid, obs_n, info_n)

            # rew_n / term_n / trunc_n are List[Dict[agent_id_str, value]]
            env_rew   = rew_n[ei]   if isinstance(rew_n,   list) else rew_n.get(ei,   {})
            env_term  = term_n[ei]  if isinstance(term_n,  list) else term_n.get(ei,  {})
            env_trunc = trunc_n[ei] if isinstance(trunc_n, list) else trunc_n.get(ei, {})

            rew_d[fid]  = float(env_rew.get(ai, 0.0))
            term_d[fid] = bool(env_term.get(ai, False))
            trunc_d[fid]= bool(env_trunc.get(ai, False))

        # Detect per-sub-env terminations from Schola.
        # If ALL agents in a sub-env are terminated/truncated simultaneously,
        # that signals a match-level end (not individual agent death).
        # Individual agent deaths (subset terminated) are suppressed.
        self._schola_env_done: Dict[int, str] = {}  # env_idx → "term"/"trunc"
        for ei in range(self.num_envs):
            agents = self._agents_for_env(ei)
            if not agents:
                continue
            n_term  = sum(1 for a in agents if term_d.get(a, False))
            n_trunc = sum(1 for a in agents if trunc_d.get(a, False))
            n_total = len(agents)
            if n_term == n_total:
                self._schola_env_done[ei] = "term"
            elif n_trunc == n_total:
                self._schola_env_done[ei] = "trunc"
            elif n_term + n_trunc > 0:
                # Partial termination (individual agent deaths) — log but suppress
                if self._total_step_count <= 10 or self._total_step_count % 200 == 0:
                    print(f"[SCHOLA] Partial term/trunc in env={ei}: "
                          f"term={n_term}/{n_total} trunc={n_trunc}/{n_total} "
                          f"(suppressed — individual agent death)")

        # Log when Schola signals full sub-env termination
        if self._schola_env_done:
            print(f"[SCHOLA] Sub-env terminations detected: {self._schola_env_done} "
                  f"at step {self._total_step_count}")

        # Now suppress all — step() will re-apply based on _schola_env_done / timeout
        for fid in list(term_d.keys()):
            if fid != '__all__':
                term_d[fid]  = False
                trunc_d[fid] = False
        term_d['__all__']  = False
        trunc_d['__all__'] = False

        # Prefer CumulativeLifetimeReward delta over Schola step reward
        info_used = False
        for fid in list(rew_d.keys()):
            clf = info_d.get(fid, {}).get('CumulativeLifetimeReward')
            if clf is None:
                continue
            try:
                curr  = float(clf)
                prev  = self._prev_cumulative.get(fid)
                if prev is None:
                    self._prev_cumulative[fid] = curr
                    continue
                delta = curr - prev
                self._prev_cumulative[fid] = curr
                if abs(delta) > 1e-4:
                    rew_d[fid] = delta
                    info_used = True
            except (ValueError, TypeError):
                pass

        # Per-strategy reward accumulation
        # Also update AGENT_STRATEGY_REGISTRY from obs one-hot every step so that
        # the NEXT episode's policy_mapping_fn uses the correct per-role routing.
        # (policy_mapping_fn is called once per agent per episode at episode start,
        #  so the registry populated during episode N drives routing in episode N+1.)
        for fid in list(rew_d.keys()):
            if fid == '__all__':
                continue
            info = info_d.get(fid, {})

            # 1. Prefer explicit StrategyType from info channel
            raw_strat = info.get('StrategyType')
            if raw_strat is not None:
                try:
                    strat_from_info = int(raw_strat)
                    self._agent_strategy[fid] = strat_from_info
                    AGENT_STRATEGY_REGISTRY[fid] = strat_from_info
                except (ValueError, TypeError):
                    pass

            # 2. Fallback: decode from obs one-hot (obs[223:226])
            #    Only update if the one-hot is non-zero (Squad Commander has run)
            obs = obs_d.get(fid)
            if obs is not None:
                one_hot = obs[STRATEGY_ONEHOT_START:STRATEGY_ONEHOT_START + 3]
                if one_hot.sum() > 0.5:
                    strat_from_obs = int(np.argmax(one_hot))
                    prev = self._agent_strategy.get(fid, -1)
                    if strat_from_obs != prev:
                        self._agent_strategy[fid] = strat_from_obs
                        AGENT_STRATEGY_REGISTRY[fid] = strat_from_obs
                        if self._total_step_count <= 20:
                            print(f"[STRATEGY UPDATE step={self._total_step_count}] "
                                  f"{fid}: {self._strategy_names.get(prev,'?')} → "
                                  f"{self._strategy_names.get(strat_from_obs,'?')}")

            strat = self._agent_strategy.get(fid)
            if strat is not None and strat in self._step_rewards_by_strategy:
                self._step_rewards_by_strategy[strat].append(rew_d[fid])

            # Per reward-component accumulation from UE5 info
            for comp in self._reward_components:
                raw_val = info.get(comp)
                if raw_val is not None:
                    try:
                        self._component_ep_sums[comp].append(float(raw_val))
                    except (ValueError, TypeError):
                        pass

        # Debug first 3 steps
        self._reward_debug_count += 1
        if self._reward_debug_count <= 3:
            sample = next(iter(rew_d.keys()))
            si = info_d.get(sample, {})
            print(f"[REWARD DEBUG #{self._reward_debug_count}]  "
                  f"schola_rew={rew_n[0] if isinstance(rew_n, list) and rew_n else '?'}  "
                  f"delta={rew_d[sample]:.4f}  "
                  f"cumul={si.get('CumulativeLifetimeReward','?')}")
        if info_used and self._reward_debug_count == 1:
            print("[REWARD] Using CumulativeLifetimeReward delta channel")

        # Scale and clip raw UE5 rewards before returning to RLlib.
        # process_reward() in train.py defines these constants but is never
        # called from env_wrapper — apply the scaling here so PPO sees
        # rewards in a stable range instead of raw C++ magnitudes.
        REWARD_SCALE = 0.01
        REWARD_CLIP  = 5.0
        for fid in list(rew_d.keys()):
            if fid != '__all__':
                rew_d[fid] = float(np.clip(rew_d[fid] * REWARD_SCALE, -REWARD_CLIP, REWARD_CLIP))

        return obs_d, rew_d, term_d, trunc_d, info_d

    # ── Custom metrics ────────────────────────────────────────────────────

    def _pop_custom_metrics(self) -> dict:
        """
        Drain accumulated per-strategy and per-component reward buffers
        and return a flat dict suitable for RLlib custom_metrics.
        Buffers are cleared after each call.
        """
        # NOTE: RLlib appends its own "_mean"/"_min"/"_max" suffixes during
        # aggregation.  Do NOT include "_mean" in the key here — the aggregated
        # result key will be  reward_strategy_strike_mean  (RLlib-added suffix).
        metrics = {}
        for strat_id, name in self._strategy_names.items():
            buf = self._step_rewards_by_strategy[strat_id]
            if buf:
                metrics[f"reward_strategy_{name}"]       = float(np.mean(buf))
                metrics[f"reward_strategy_{name}_sum"]   = float(np.sum(buf))
                metrics[f"reward_strategy_{name}_count"] = float(len(buf))
                self._step_rewards_by_strategy[strat_id] = []
        for comp in self._reward_components:
            buf = self._component_ep_sums[comp]
            if buf:
                metrics[f"reward_component_{comp}"] = float(np.mean(buf))
                self._component_ep_sums[comp] = []

        # Win rate (only non-timeout episodes count)
        if self._rl_episodes_total > 0:
            metrics["rl_win_rate"] = self._rl_episode_wins / self._rl_episodes_total
        self._rl_episode_wins   = 0
        self._rl_episodes_total = 0

        # Debug: log strategy buffer state periodically to diagnose routing issues
        if self._total_step_count <= 5 or self._total_step_count % 200 == 0:
            dist = {self._strategy_names[k]: len(v)
                    for k, v in self._step_rewards_by_strategy.items()}
            known = {name: self._strategy_names.get(idx, '?')
                     for name, idx in list(self._agent_strategy.items())[:5]}
            print(f"[CUSTOM_METRICS step={self._total_step_count}] "
                  f"strategy_buf_sizes(after_drain)={dist}  "
                  f"returned_keys={list(metrics.keys())}  "
                  f"agent_strategy_sample={known}")

        return metrics

    # ── Episode management helpers ────────────────────────────────────────

    def _sync_max_steps(self, info_d: dict):
        for fid, info in info_d.items():
            if isinstance(info, dict) and 'MaxSteps' in info:
                try:
                    v = int(info['MaxSteps'])
                    if v > 0:
                        self._max_episode_steps        = v
                        self._max_episode_steps_synced = True
                        print(f"[DEEntityCentricEnv] max_episode_steps={v} (from UE5)")
                        return
                except (ValueError, TypeError):
                    pass
        self._max_episode_steps_synced = True
        print(f"[DEEntityCentricEnv] max_episode_steps={self._max_episode_steps} (default)")

    def _log_episode(self, ei, agents, term_d, trunc_d):
        dur     = time.time() - (self._env_episode_start.get(ei) or time.time())
        total_r = sum(self._agent_ep_rewards.get(a, 0.0) for a in agents)
        truncated = any(trunc_d.get(a) for a in agents)
        end_t   = "TRUNCATED" if truncated else "TERMINATED"

        # Win rate tracking: early termination (TERMINATED, not TRUNCATED) with
        # positive mean episode reward indicates RL team captured all points (win).
        # Timeout episodes (TRUNCATED) do not count toward win rate.
        if not truncated:
            self._rl_episodes_total += 1
            mean_r = total_r / max(1, len(agents))
            if mean_r > 0.0:
                self._rl_episode_wins += 1

        print("=" * 70)
        print(f"[EP END] env={ei}  ep={self._env_episodes_done[ei]}  {end_t}")
        print(f"  steps={self._env_episode_steps[ei]}  dur={dur:.1f}s  total_r={total_r:.2f}")
        for a in sorted(agents):
            print(f"    {a}: {self._agent_ep_rewards.get(a, 0.0):.2f}")
        print("=" * 70)

    def _log_progress(self):
        elapsed = time.time() - (self._training_start_time or time.time())
        print(f"[PROGRESS] elapsed={elapsed:.0f}s")
        for ei in range(self.num_envs):
            agents  = self._agents_for_env(ei)
            ep_dur  = time.time() - (self._env_episode_start.get(ei) or time.time())
            ep_r    = sum(self._agent_ep_rewards.get(a, 0.0) for a in agents)
            flag    = "DONE" if ei in self._completed_envs else "ACTIVE"
            print(f"  env={ei}  {flag}  "
                  f"ep={self._env_episodes_done[ei]}  "
                  f"steps={self._env_episode_steps[ei]}  "
                  f"ep_dur={ep_dur:.1f}s  ep_r={ep_r:.2f}")
        ap = np.mean(self.poll_durations) * 1000 if self.poll_durations else 0
        as_ = np.mean(self.step_durations) * 1000 if self.step_durations else 0
        print(f"  poll={ap:.1f}ms  step={as_:.1f}ms")

    def _zero_step_result(self):
        obs_d  = {fid: np.zeros(OBS_DIM, dtype=np.float32) for fid in self._agent_ids}
        rew_d  = {fid: 0.0 for fid in self._agent_ids}
        term_d = {fid: False for fid in self._agent_ids}
        term_d['__all__'] = False
        trunc_d = {fid: False for fid in self._agent_ids}
        trunc_d['__all__'] = False
        info_d = {fid: {} for fid in self._agent_ids}
        return obs_d, rew_d, term_d, trunc_d, info_d

    def _terminal_fallback(self):
        obs_d  = {fid: np.zeros(OBS_DIM, dtype=np.float32) for fid in self._agent_ids}
        rew_d  = {fid: 0.0 for fid in self._agent_ids}
        term_d = {fid: True for fid in self._agent_ids}
        term_d['__all__'] = True
        trunc_d = {fid: False for fid in self._agent_ids}
        trunc_d['__all__'] = False
        info_d = {fid: {} for fid in self._agent_ids}
        return obs_d, rew_d, term_d, trunc_d, info_d

    # ── Cleanup ───────────────────────────────────────────────────────────

    def render(self):
        return None

    def close(self):
        print("[DEEntityCentricEnv] Closing...")
        if hasattr(self, '_protocol'):
            self._protocol.close()
        if hasattr(self, '_simulator'):
            self._simulator.stop()
