"""
DE Entity-Centric Environment Wrapper.

Wraps Schola/UE5 connection for the entity-centric refactor (v10.2+).

Key differences from the old v10.2 wrapper:
  - Observation: 167-dim flat (from C++ FDEObservationV2::ToFlatArray)
  - Action:       7-dim EQS weights in [-1, 1]  (7th = AssignedBaseProximity)
  - No strategy routing: single EntityCentricPolicy handles all agents
  - No uniform strategy assignment: strategies come from UE5 Squad Commander
    and are encoded implicitly in the base tokens (is_assigned_target field)

Reward source:
  - Primary: CumulativeLifetimeReward delta from info channel
    (captures cooperative base rewards that Schola may drop on SAME_STEP resets)
  - Fallback: Schola step reward
"""

from gymnasium import spaces
import numpy as np
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
    from schola.core.env import ScholaEnv, AutoResetType
    from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
    SCHOLA_AVAILABLE = True
except ImportError:
    SCHOLA_AVAILABLE = False
    print("Warning: schola not installed. Run: pip install schola")


# ── Layout constants (must match DEObservationTypes.h) ───────────────────────
OBS_DIM    = 167
ACTION_DIM = 7


if SCHOLA_AVAILABLE:

    class DEEntityCentricEnv(MultiAgentEnv):
        """
        DE Entity-Centric Multi-Agent Environment.

        Connects to UE5 via Schola gRPC. All agents share a single
        EntityCentricPolicy (no per-strategy routing).
        """

        def __init__(self, **kwargs):
            super().__init__()

            host           = kwargs.get("host", "localhost")
            port           = self._resolve_port(kwargs)
            self.num_envs  = kwargs.get("num_envs", 4)

            print(f"[DEEntityCentricEnv] Connecting to {host}:{port}  "
                  f"num_envs={self.num_envs}")
            print(f"[DEEntityCentricEnv] Obs={OBS_DIM}-dim, Action={ACTION_DIM}-dim EQS")

            try:
                conn = UnrealEditorConnection(url=host, port=port)
                self.schola_env = ScholaEnv(
                    unreal_connection=conn,
                    verbosity=1,
                    auto_reset_type=AutoResetType.SAME_STEP,
                )
                print("[DEEntityCentricEnv] Connected.")

                actual = len(self.schola_env.ids)
                if actual != self.num_envs:
                    print(f"[DEEntityCentricEnv] Auto-adjust: expected {self.num_envs}, "
                          f"got {actual} from UE5")
                    self.num_envs = actual

                total = sum(len(a) for a in self.schola_env.ids)
                print(f"[DEEntityCentricEnv] {self.num_envs} envs, {total} agents total")

            except Exception as e:
                print(f"[DEEntityCentricEnv] Connection failed: {e}")
                raise

            # Agent ID mapping: flat string ↔ (env_idx, schola_agent_idx)
            self.agent_map:    Dict[str, Tuple[int, int]] = {}
            self.reverse_map:  Dict[Tuple[int, int], str] = {}
            self._agent_ids:   set = set()
            if hasattr(self.schola_env, 'ids'):
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

            self._max_episode_steps        = 100
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
            for env_idx, agent_list in enumerate(self.schola_env.ids):
                for schola_idx in agent_list:
                    fid = f"agent_{env_idx}_{schola_idx}"
                    self.agent_map[fid]                    = (env_idx, schola_idx)
                    self.reverse_map[(env_idx, schola_idx)] = fid
                    self._agent_ids.add(fid)
            print(f"[DEEntityCentricEnv] Agent map: {len(self._agent_ids)} agents")

        def _agents_for_env(self, env_idx: int):
            prefix = f"agent_{env_idx}_"
            return [a for a in self._agent_ids if a.startswith(prefix)]

        # ── Observation extraction ────────────────────────────────────────────

        def _extract_obs(
            self,
            flat_id:    str,
            obs_nested: dict,
            info_nested: dict,
        ) -> Tuple[np.ndarray, dict]:
            """
            Extract a single agent's 167-dim flat observation and info dict
            from nested Schola data.

            Returns:
                obs  : (167,) float32
                info : dict
            """
            env_idx, agent_idx = self.agent_map[flat_id]

            raw = obs_nested.get(env_idx, {}).get(agent_idx, None)
            if raw is not None:
                if isinstance(raw, dict):
                    raw = list(raw.values())[0]
                obs = np.asarray(raw, dtype=np.float32).flatten()
            else:
                obs = np.zeros(OBS_DIM, dtype=np.float32)

            # Pad or truncate to exactly OBS_DIM
            if len(obs) < OBS_DIM:
                obs = np.pad(obs, (0, OBS_DIM - len(obs)))
            else:
                obs = obs[:OBS_DIM]

            raw_info = info_nested.get(env_idx, {}).get(agent_idx, {})
            info = {"env_id": env_idx, **(raw_info if isinstance(raw_info, dict) else {})}

            return obs, info

        def _get_action_keys(self) -> dict:
            all_keys = {}
            action_defns = getattr(self.schola_env, 'action_defns', {})
            for fid in self._agent_ids:
                ei, ai = self.agent_map[fid]
                defn = action_defns.get(ei, {}).get(ai, None)
                if defn is None:
                    continue
                keys = list(defn.spaces.keys()) if hasattr(defn, 'spaces') else (
                       list(defn.keys())         if isinstance(defn, dict) else [])
                if keys:
                    all_keys[(ei, ai)] = keys
            return all_keys

        # ── Core interface ────────────────────────────────────────────────────

        def reset(self, *, seed=None, options=None):
            t0 = time.time()
            print(f"[RESET] time={time.strftime('%H:%M:%S')}  "
                  f"first={not self._first_reset_done}")

            now = time.time()
            if self._training_start_time is None:
                self._training_start_time = now

            if not self._first_reset_done:
                for ei in range(self.num_envs):
                    self._env_episode_steps[ei] = 0
                    self._env_episode_start[ei]  = now
                    self._env_done_flags[ei]     = False
                for fid in self._agent_ids:
                    self._agent_ep_rewards[fid] = 0.0

                raw = self.schola_env.hard_reset()
                self._first_reset_done = True
                self._update_agent_map()
                for fid in self._agent_ids:
                    self._agent_ep_rewards[fid] = 0.0

                obs_d, info_d = self._process_obs(raw)
                print(f"[RESET] Complete ({time.time()-t0:.2f}s, agents={len(obs_d)})")
                return obs_d, info_d

            else:
                # Soft reset: send dummy actions to advance Schola state
                keys = self._get_action_keys()
                dummy = {}
                for fid in self._agent_ids:
                    ei, ai = self.agent_map[fid]
                    if ei not in dummy:
                        dummy[ei] = {}
                    ks = keys.get((ei, ai), [])
                    if ks:
                        dummy[ei][ai] = {k: np.zeros(ACTION_DIM, dtype=np.float32) for k in ks}

                self.schola_env.send_actions(dummy)
                result = self.schola_env.poll()
                obs_d, info_d = self._parse_obs_and_info(result)
                print(f"[RESET] Soft complete ({time.time()-t0:.2f}s)")
                return obs_d, info_d

        def step(self, actiondict: dict):
            t0 = self._step_call_count
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
                keys = self._get_action_keys()
                fmt  = {}
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
                    ks = keys.get((ei, ai), [])
                    if ks:
                        fmt[ei][ai] = {k: a for k in ks}

                self.schola_env.send_actions(fmt)

                self._total_step_count += 1
                steps_s0 = self._env_episode_steps.get(0, 0)
                if self._total_step_count <= 5 or self._total_step_count % 500 == 0 or steps_s0 <= 2:
                    print(f"[STEP {self._total_step_count}] poll()  "
                          f"env_steps={dict(self._env_episode_steps)}")

                poll_t = time.time()
                result = self.schola_env.poll()
                self.poll_durations.append(time.time() - poll_t)

                obs_d, rew_d, term_d, trunc_d, info_d = self._parse_full(result)

                if not self._max_episode_steps_synced:
                    self._sync_max_steps(info_d)

                for ei in range(self.num_envs):
                    agents = self._agents_for_env(ei)
                    if not agents:
                        continue

                    if self._env_done_flags.get(ei, False):
                        self._env_done_flags[ei] = False

                    self._env_episode_steps[ei] += 1
                    for aid in agents:
                        self._agent_ep_rewards[aid] = (
                            self._agent_ep_rewards.get(aid, 0.0) + rew_d.get(aid, 0.0)
                        )

                    ue5_end = any(
                        info_d.get(aid, {}).get('MatchEnded', 'false') == 'true'
                        for aid in agents
                    )
                    timed_out = (self._force_timeout and
                                 self._env_episode_steps[ei] >= self._max_episode_steps)

                    if ue5_end or timed_out:
                        reason = "UE5 match end" if ue5_end else f"timeout({self._max_episode_steps})"
                        print(f"[STEP] Episode end: env={ei} — {reason}")
                        for aid in agents:
                            if ue5_end:
                                term_d[aid] = True
                            else:
                                trunc_d[aid] = True
                        self._log_episode(ei, agents, term_d, trunc_d)
                        self._env_done_flags[ei]   = True
                        self._env_episodes_done[ei] += 1
                        self._env_episode_steps[ei] = 0
                        self._env_episode_start[ei] = time.time()
                        for aid in agents:
                            self._agent_ep_rewards[aid] = 0.0

                all_done = all(self._env_done_flags.get(i, False)
                               for i in range(self.num_envs))
                if all_done:
                    term_d['__all__']  = True
                    trunc_d['__all__'] = True

                # Periodic progress log
                if any(self._env_episode_steps[i] % 100 == 0 and
                       self._env_episode_steps[i] > 0
                       for i in range(self.num_envs)):
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

                self.step_durations.append(time.time() - step_start)
                self._last_step_return_time = time.time()
                return obs_d, rew_d, term_d, trunc_d, info_d

            except Exception as e:
                import traceback
                print(f"[STEP ERROR] {e}")
                traceback.print_exc()
                return self._terminal_fallback()

        # ── Parsing helpers ───────────────────────────────────────────────────

        def _process_obs(self, raw) -> Tuple[dict, dict]:
            """Parse obs from hard_reset() result."""
            obs_nested  = raw if not isinstance(raw, tuple) else raw[0]
            info_nested = (raw[4] if isinstance(raw, tuple) and len(raw) >= 5 else
                           raw[3] if isinstance(raw, tuple) and len(raw) >= 4 else {})
            obs_d = {}
            info_d = {}
            for fid in self._agent_ids:
                obs_d[fid], info_d[fid] = self._extract_obs(fid, obs_nested, info_nested)
            return obs_d, info_d

        def _parse_obs_and_info(self, result) -> Tuple[dict, dict]:
            """Minimal parse for soft reset (obs + info only)."""
            if len(result) == 5:
                obs_n, _, _, _, info_n = result
            elif len(result) == 4:
                obs_n, _, _, info_n = result
            else:
                obs_n, info_n = result[0], {}
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
            """Full step result parse: obs, rewards, terminated, truncated, info."""
            if len(result) == 5:
                obs_n, rew_n, term_n, trunc_n, info_n = result
            elif len(result) == 4:
                obs_n, rew_n, term_n, info_n = result
                trunc_n = term_n
            else:
                return self._zero_step_result()

            obs_d = {}; rew_d = {}; term_d = {}; trunc_d = {}; info_d = {}

            for fid in self._agent_ids:
                ei, ai = self.agent_map[fid]
                obs_d[fid],  info_d[fid] = self._extract_obs(fid, obs_n, info_n)
                rew_d[fid]  = float(rew_n.get(ei, {}).get(ai, 0.0))
                term_d[fid] = bool(term_n.get(ei, {}).get(ai, False))
                trunc_d[fid]= bool((trunc_n.get(ei, {}).get(ai, False)
                                    if isinstance(trunc_n, dict) else False))

            # Suppress individual agent terminations (allow only force-timeout or UE5 match-end)
            ue5_match_end = any(
                info_d.get(fid, {}).get('MatchEnded', 'false') == 'true'
                for fid in info_d if fid != '__all__'
            )
            for fid in list(term_d.keys()):
                if fid != '__all__':
                    term_d[fid]  = False
                    trunc_d[fid] = False
            term_d['__all__']  = ue5_match_end
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

            # Debug first 3 steps
            self._reward_debug_count += 1
            if self._reward_debug_count <= 3:
                sample = next(iter(rew_d.keys()))
                si = info_d.get(sample, {})
                print(f"[REWARD DEBUG #{self._reward_debug_count}]  "
                      f"schola={rew_n.get(0, {})}  "
                      f"delta={rew_d[sample]:.4f}  "
                      f"cumul={si.get('CumulativeLifetimeReward','?')}")
            if info_used and self._reward_debug_count == 1:
                print("[REWARD] Using CumulativeLifetimeReward delta channel")

            return obs_d, rew_d, term_d, trunc_d, info_d

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
            end_t   = "TRUNCATED" if any(trunc_d.get(a) for a in agents) else "TERMINATED"
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
                flag    = "DONE" if self._env_done_flags.get(ei) else "ACTIVE"
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
            if hasattr(self.schola_env, 'close'):
                self.schola_env.close()
