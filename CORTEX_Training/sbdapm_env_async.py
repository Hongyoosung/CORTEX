"""
SBDAPM Async Environment with Message Queuing (v8.7)

This version implements ASYNC architecture to prevent poll() blocking during policy updates:

Architecture:
    - Main Thread: Policy updates (training)
    - gRPC Thread: Dedicated communication with UE5
    - Message Queues: Thread-safe buffers between layers

Key Features:
    1. Non-blocking gRPC: poll() runs in dedicated thread
    2. Action Queue: Actions are queued and sent asynchronously
    3. Observation Buffer: Latest observations always available
    4. No Deadlocks: UE5 communication never blocks training
    5. v8.7: Episode completion queuing to fix reward logging race condition

v8.7 Changes:
    - Added episode_completion_queue to capture episode end state BEFORE auto-reset
    - Step() now returns terminal state when episode completes, ensuring RLlib's
      on_episode_end callback is triggered with correct rewards
    - Fixed issue where iteration updates showed 0.00 reward despite episodes completing

Usage:
    In train_rllib.py, replace:
        from sbdapm_env import SBDAPMMultiAgentEnv
    with:
        from sbdapm_env_async import SBDAPMAsyncMultiAgentEnv as SBDAPMMultiAgentEnv
"""

from gymnasium import spaces
import numpy as np
import time
import threading
import queue
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
    print("Warning: schola not installed")

try:
    from training_env.config import RLConfig
except ImportError:
    class RLConfig:
        OBSERVATION_SIZE = 50
        NUM_TOTAL_OUTPUTS = 5


if SCHOLA_AVAILABLE:

    class SBDAPMAsyncMultiAgentEnv(MultiAgentEnv):
        """
        Async Multi-Agent Environment with Message Queuing.

        Threading Model:
            - Main Thread: RLlib training (step/reset calls)
            - gRPC Thread: Dedicated UE5 communication (poll loop)
            - Queues: Thread-safe message passing
        """

        def __init__(self, **kwargs):
            super().__init__()

            host = kwargs.get("host", "localhost")
            port = self._resolve_port(kwargs)
            timeout = kwargs.get("timeout", 60)
            is_docker = kwargs.get("is_docker", False)

            self.num_envs = kwargs.get("num_envs", 4)
            print(f"[ENV v8.7 ASYNC] Connecting to {host}:{port}...")
            print(f"[ENV v8.7 ASYNC] Multi-environment: {self.num_envs} parallel UE5 envs")
            print(f"[ENV v8.7 ASYNC] Architecture: ASYNC with message queuing + episode completion queue")

            # Connect to UE5
            try:
                connection = UnrealEditorConnection(url=host, port=port)
                self.schola_env = ScholaEnv(
                    unreal_connection=connection,
                    verbosity=1,
                    auto_reset_type=AutoResetType.SAME_STEP
                )
                print(f"[ENV v8.7 ASYNC] Connected!")
            except Exception as e:
                print(f"[ERROR] Connection failed: {e}")
                raise

            # Agent mapping
            self.agent_map = {}
            self.reverse_map = {}
            self._agent_ids = set()

            if hasattr(self.schola_env, 'ids'):
                self._update_agent_map()

            # Spaces
            self._obs_space = spaces.Box(
                low=-np.inf, high=np.inf,
                shape=(RLConfig.OBSERVATION_SIZE,),
                dtype=np.float32
            )
            self._action_space = spaces.Box(
                low=-np.inf, high=np.inf,
                shape=(RLConfig.NUM_TOTAL_OUTPUTS,),
                dtype=np.float32
            )

            # === ASYNC ARCHITECTURE ===
            # Message Queues
            self.action_queue = queue.Queue(maxsize=10)  # Actions to send to UE5
            self.obs_buffer = {}  # Latest observations (thread-safe dict access)
            self.reward_buffer = {}
            self.done_buffer = {}
            self.info_buffer = {}
            self.buffer_lock = threading.Lock()

            # Auto-reset detection
            self.reset_event_queue = queue.Queue()  # Signals when UE5 auto-resets an environment
            self.prev_env_done_states = {i: False for i in range(self.num_envs)}

            # Episode completion tracking (v8.7: Fix for episode boundary race condition)
            # When gRPC thread detects episode end, it queues the completion BEFORE auto-reset
            # Main thread then returns __all__=True so RLlib's on_episode_end is triggered
            self.episode_completion_queue = queue.Queue()  # (env_idx, final_rewards_dict, final_steps)
            self.pending_episode_completion = {}  # env_idx -> (final_rewards, final_obs, final_info)

            # gRPC Thread
            self.grpc_thread = None
            self.stop_event = threading.Event()
            self.grpc_ready = threading.Event()

            # Episode tracking
            self._first_reset_done = False
            self._training_start_time = None
            self._env_episode_steps = {i: 0 for i in range(self.num_envs)}
            self._env_episode_start_time = {i: None for i in range(self.num_envs)}
            self._env_episodes_completed = {i: 0 for i in range(self.num_envs)}
            self._env_done_flags = {i: False for i in range(self.num_envs)}
            self._logical_env_map_initialized = False
            self._agent_logical_env = {}

            # Reward tracking per agent (cumulative per episode)
            self._agent_episode_rewards = {}

            # Performance metrics
            self.poll_durations = deque(maxlen=100)
            self.send_durations = deque(maxlen=100)

        def _resolve_port(self, kwargs):
            base_port = kwargs.get("base_port")
            if base_port is not None:
                try:
                    from ray.rllib.evaluation.rollout_worker import get_global_worker
                    worker = get_global_worker()
                    worker_index = worker.worker_index if worker else 0
                    return base_port + max(0, worker_index - 1)
                except:
                    return base_port
            return kwargs.get("port", 50051)

        def _update_agent_map(self):
            self.agent_map.clear()
            self.reverse_map.clear()
            self._agent_ids.clear()

            if hasattr(self.schola_env, 'ids'):
                for physical_env_idx, agent_list in enumerate(self.schola_env.ids):
                    for schola_agent_idx in agent_list:
                        flat_id = f"agent_{physical_env_idx}_{schola_agent_idx}"
                        self.agent_map[flat_id] = (physical_env_idx, schola_agent_idx)
                        self.reverse_map[(physical_env_idx, schola_agent_idx)] = flat_id
                        self._agent_ids.add(flat_id)

            print(f"[ENV v8.7 ASYNC] Agent map: {len(self._agent_ids)} agents")

        def _update_logical_env_map(self, info_nested):
            """Update agent-to-logical-env mapping from UE5 info dict."""
            if self._logical_env_map_initialized:
                return

            self._agent_logical_env = {}
            env_counts = {}

            for flat_id in list(self._agent_ids):
                physical_env_idx, schola_agent_idx = self.agent_map[flat_id]
                agent_info = info_nested.get(physical_env_idx, {}).get(schola_agent_idx, {})

                if isinstance(agent_info, dict):
                    logical_env_str = agent_info.get('logical_env_id', '-1')
                    try:
                        logical_env_id = int(logical_env_str)
                    except (ValueError, TypeError):
                        logical_env_id = -1
                else:
                    logical_env_id = -1

                if logical_env_id < 0:
                    logical_env_id = schola_agent_idx // 8

                self._agent_logical_env[flat_id] = logical_env_id
                env_counts[logical_env_id] = env_counts.get(logical_env_id, 0) + 1

            self._logical_env_map_initialized = True
            print(f"[ENV v8.7 ASYNC] Logical env mapping: {dict(sorted(env_counts.items()))}")

        def _get_agents_for_logical_env(self, logical_env_idx):
            """Get all agent IDs belonging to a logical environment."""
            if not hasattr(self, '_agent_logical_env'):
                return [aid for aid in self._agent_ids if aid.startswith(f"agent_{logical_env_idx}_")]
            return [aid for aid, env_id in self._agent_logical_env.items() if env_id == logical_env_idx]

        def _get_all_action_keys(self):
            all_keys = {}
            action_defns = getattr(self.schola_env, 'action_defns', {})

            for flat_id in self._agent_ids:
                env_idx, agent_idx = self.agent_map[flat_id]
                agent_defn = action_defns.get(env_idx, {}).get(agent_idx, None)

                if agent_defn is None:
                    continue

                keys_list = []
                if hasattr(agent_defn, 'spaces'):
                    keys_list = list(agent_defn.spaces.keys())
                elif isinstance(agent_defn, dict):
                    keys_list = list(agent_defn.keys())

                if keys_list:
                    all_keys[(env_idx, agent_idx)] = keys_list

            return all_keys

        @property
        def observation_space(self):
            return self._obs_space

        @property
        def action_space(self):
            return self._action_space

        def _build_observation(self, base_obs):
            """Build 50-dim observation: pad/truncate to 46 + add strategy one-hot."""
            if len(base_obs) < 46:
                base_obs = np.pad(base_obs, (0, 46 - len(base_obs)), mode='constant')
            elif len(base_obs) > 46:
                base_obs = base_obs[:46]

            strategy_onehot = np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32)
            return np.concatenate([base_obs, strategy_onehot]).astype(np.float32)

        # === ASYNC gRPC THREAD ===

        def _grpc_worker_loop(self):
            """
            Dedicated thread for UE5 communication.
            Continuously polls for observations and sends actions.
            This thread NEVER blocks the main training thread.
            """
            print(f"[gRPC Thread] Started")
            self.grpc_ready.set()

            while not self.stop_event.is_set():
                try:
                    # 1. Get actions from queue (non-blocking with timeout)
                    try:
                        formatted_actions = self.action_queue.get(timeout=0.1)

                        # Send actions to UE5
                        send_start = time.time()
                        self.schola_env.send_actions(formatted_actions)
                        send_duration = time.time() - send_start
                        self.send_durations.append(send_duration)

                        if send_duration > 0.5:
                            print(f"[gRPC Thread] Slow send_actions: {send_duration:.2f}s")

                    except queue.Empty:
                        # No actions to send, skip send_actions this cycle
                        pass

                    # 2. Poll for observations (this is the potentially blocking call)
                    poll_start = time.time()
                    step_result = self.schola_env.poll()
                    poll_duration = time.time() - poll_start
                    self.poll_durations.append(poll_duration)

                    if poll_duration > 2.0:
                        print(f"[gRPC Thread] WARNING: poll() took {poll_duration:.1f}s")

                    # 3. Parse result
                    if len(step_result) == 5:
                        obs_nested, rew_nested, term_nested, trunc_nested, info_nested = step_result
                    elif len(step_result) == 4:
                        obs_nested, rew_nested, term_nested, info_nested = step_result
                        trunc_nested = term_nested
                    else:
                        continue

                    # 4. Update logical env mapping (once)
                    if not self._logical_env_map_initialized:
                        self._update_logical_env_map(info_nested)

                    # 5. Build observation/reward/done/info dictionaries
                    obs_dict = {}
                    reward_dict = {}
                    terminated_dict = {}
                    truncated_dict = {}
                    info_dict = {}

                    for flat_id in self._agent_ids:
                        env_idx, agent_idx = self.agent_map[flat_id]

                        # Observation
                        agent_obs_data = obs_nested.get(env_idx, {}).get(agent_idx, None)
                        if agent_obs_data is not None:
                            if isinstance(agent_obs_data, dict):
                                obs_val = list(agent_obs_data.values())[0]
                            else:
                                obs_val = agent_obs_data
                            base_obs = np.array(obs_val, dtype=np.float32).flatten()
                        else:
                            base_obs = np.zeros(46, dtype=np.float32)
                        obs_dict[flat_id] = self._build_observation(base_obs)

                        # Reward
                        reward_dict[flat_id] = float(rew_nested.get(env_idx, {}).get(agent_idx, 0.0))

                        # Termination
                        is_term = term_nested.get(env_idx, {}).get(agent_idx, False)
                        is_trunc = trunc_nested.get(env_idx, {}).get(agent_idx, False) if isinstance(trunc_nested, dict) else False
                        terminated_dict[flat_id] = is_term
                        truncated_dict[flat_id] = is_trunc

                        # Add logical_env_id to info for callback tracking
                        logical_env_id = self._agent_logical_env.get(flat_id, agent_idx // 8)
                        info_dict[flat_id] = {
                            "env_id": env_idx,
                            "logical_env_id": logical_env_id
                        }

                    # Check if all agents done
                    all_agents_done = all(
                        terminated_dict.get(aid, False) or truncated_dict.get(aid, False)
                        for aid in self._agent_ids
                    )
                    terminated_dict['__all__'] = all_agents_done
                    truncated_dict['__all__'] = all_agents_done

                    # 6. Detect episode completion AND auto-reset events (per logical environment)
                    # v8.7: Queue episode completion BEFORE auto-reset overwrites the data
                    for env_idx in range(self.num_envs):
                        env_agents = self._get_agents_for_logical_env(env_idx)
                        if not env_agents:
                            continue

                        # Check if this environment is currently done
                        current_env_done = all(
                            terminated_dict.get(aid, False) or truncated_dict.get(aid, False)
                            for aid in env_agents
                        )

                        prev_done = self.prev_env_done_states.get(env_idx, False)

                        # Detect transition: was NOT done → now done (episode just completed)
                        # Queue the completion data BEFORE auto-reset can overwrite it
                        if current_env_done and not prev_done:
                            # Episode just completed! Capture final state
                            final_rewards = {aid: reward_dict.get(aid, 0.0) for aid in env_agents}
                            final_obs = {aid: obs_dict.get(aid) for aid in env_agents}
                            final_info = {aid: info_dict.get(aid, {}) for aid in env_agents}
                            try:
                                self.episode_completion_queue.put(
                                    (env_idx, final_rewards, final_obs, final_info),
                                    block=False
                                )
                                print(f"[gRPC Thread] Episode completion queued for Env {env_idx}")
                            except queue.Full:
                                print(f"[gRPC Thread] Warning: Episode completion queue full for Env {env_idx}")

                        # Detect transition: was done → now not done (auto-reset occurred)
                        if prev_done and not current_env_done:
                            # UE5 auto-reset detected! Signal to main thread
                            try:
                                self.reset_event_queue.put(env_idx, block=False)
                                print(f"[gRPC Thread] Auto-reset detected for Env {env_idx}")
                            except queue.Full:
                                print(f"[gRPC Thread] Warning: Reset event queue full for Env {env_idx}")

                        # Update previous state
                        self.prev_env_done_states[env_idx] = current_env_done

                    # 7. Update buffers (thread-safe)
                    with self.buffer_lock:
                        self.obs_buffer = obs_dict
                        self.reward_buffer = reward_dict
                        self.done_buffer = (terminated_dict, truncated_dict)
                        self.info_buffer = info_dict

                except Exception as e:
                    if not self.stop_event.is_set():
                        print(f"[gRPC Thread] Error: {e}")
                        import traceback
                        traceback.print_exc()
                    time.sleep(0.1)

            print(f"[gRPC Thread] Stopped")

        def _start_grpc_thread(self):
            """Start async gRPC communication thread."""
            if self.grpc_thread is None or not self.grpc_thread.is_alive():
                self.stop_event.clear()
                self.grpc_thread = threading.Thread(target=self._grpc_worker_loop, daemon=True)
                self.grpc_thread.start()

                # Wait for thread to be ready
                if not self.grpc_ready.wait(timeout=5.0):
                    raise RuntimeError("gRPC thread failed to start")

                print(f"[ENV v8.7 ASYNC] gRPC thread started successfully")

        def _stop_grpc_thread(self):
            """Stop async gRPC communication thread."""
            if self.grpc_thread and self.grpc_thread.is_alive():
                self.stop_event.set()
                self.grpc_thread.join(timeout=2.0)
                print(f"[ENV v8.7 ASYNC] gRPC thread stopped")

        # === RLlib Interface (Main Thread) ===

        def reset(self, *, seed=None, options=None):
            """
            Reset environment.
            Main thread: Returns immediately after queuing dummy actions.
            gRPC thread: Handles actual communication.
            """
            reset_start = time.time()

            is_first = not self._first_reset_done

            if is_first:
                print("=" * 80)
                print(f"RESET: First reset (ASYNC mode)")

                # Initialize training timer
                if self._training_start_time is None:
                    self._training_start_time = time.time()

                current_time = time.time()
                for env_idx in range(self.num_envs):
                    self._env_episode_steps[env_idx] = 0
                    self._env_episode_start_time[env_idx] = current_time
                    self._env_done_flags[env_idx] = False

                # Initialize reward tracking for all agents
                for flat_id in self._agent_ids:
                    self._agent_episode_rewards[flat_id] = 0.0

                # Hard reset (synchronous for first reset only)
                rawobs = self.schola_env.hard_reset()
                self._first_reset_done = True

                self._update_agent_map()

                # Initialize reward tracking after agent map is built
                for flat_id in self._agent_ids:
                    self._agent_episode_rewards[flat_id] = 0.0

                result = self._process_obs(rawobs)

                # Start async gRPC thread AFTER first reset
                self._start_grpc_thread()

                print(f"RESET: Complete (Duration={time.time()-reset_start:.2f}s, Agents={len(result[0])})")
                print("=" * 80)
                return result

            else:
                # Subsequent resets: Use async pattern
                print(f"RESET: AutoReset mode (async)")

                # Send dummy actions via queue (non-blocking)
                allactionkeys = self._get_all_action_keys()
                dummy_actions = {}
                for flat_id in self._agent_ids:
                    env_idx, agent_idx = self.agent_map[flat_id]
                    if env_idx not in dummy_actions:
                        dummy_actions[env_idx] = {}
                    agent_keys = allactionkeys.get((env_idx, agent_idx), [])
                    if agent_keys:
                        dummy_action = np.array([0.5, 0.5, 0.5, 0.5, 0.0], dtype=np.float32)
                        dummy_actions[env_idx][agent_idx] = {key: dummy_action for key in agent_keys}

                # Queue actions (non-blocking)
                try:
                    self.action_queue.put(dummy_actions, block=False)
                except queue.Full:
                    print("[RESET] Warning: Action queue full, dropping oldest action")
                    try:
                        self.action_queue.get_nowait()
                        self.action_queue.put(dummy_actions, block=False)
                    except:
                        pass

                # Wait briefly for new observations (with timeout)
                max_wait = 2.0
                wait_start = time.time()
                while (time.time() - wait_start) < max_wait:
                    with self.buffer_lock:
                        if self.obs_buffer:
                            obs_dict = self.obs_buffer.copy()
                            info_dict = self.info_buffer.copy()
                            print(f"RESET: Complete (Duration={time.time()-reset_start:.2f}s)")
                            return obs_dict, info_dict
                    time.sleep(0.05)

                # Timeout: Return zero observations
                print(f"RESET: Timeout waiting for observations, returning zeros")
                fallback_obs = {
                    flat_id: self._build_observation(np.zeros(46, dtype=np.float32))
                    for flat_id in self._agent_ids
                }
                return fallback_obs, {flat_id: {} for flat_id in self._agent_ids}

        def step(self, actiondict):
            """
            Execute one step.
            Main thread: Queues actions and reads latest buffers (non-blocking).
            gRPC thread: Handles actual send_actions/poll.

            v8.7: Episode completion is now properly signaled to RLlib BEFORE auto-reset.
            """
            try:
                # 0. Check for pending episode completions (v8.7)
                # If we have pending completions, return terminal state so RLlib sees __all__=True
                while not self.episode_completion_queue.empty():
                    try:
                        env_idx, final_rewards, final_obs, final_info = self.episode_completion_queue.get_nowait()
                        self.pending_episode_completion[env_idx] = (final_rewards, final_obs, final_info)
                        print(f"[STEP] Episode completion pending for Env {env_idx}")
                    except queue.Empty:
                        break

                # If we have pending episode completions, return terminal state
                # This ensures RLlib's on_episode_end callback is triggered
                if self.pending_episode_completion:
                    # Get agents from all pending completions
                    pending_envs = list(self.pending_episode_completion.keys())

                    # Read current buffer to get base data
                    with self.buffer_lock:
                        obs_dict = self.obs_buffer.copy() if self.obs_buffer else {}
                        reward_dict = self.reward_buffer.copy() if self.reward_buffer else {}
                        info_dict = self.info_buffer.copy() if self.info_buffer else {}

                    # Override with terminal state for pending environments
                    terminated_dict = {flat_id: False for flat_id in self._agent_ids}
                    truncated_dict = {flat_id: False for flat_id in self._agent_ids}

                    for env_idx in pending_envs:
                        final_rewards, final_obs, final_info = self.pending_episode_completion[env_idx]
                        env_agents = self._get_agents_for_logical_env(env_idx)

                        for aid in env_agents:
                            # Use final state from queued completion
                            if aid in final_obs and final_obs[aid] is not None:
                                obs_dict[aid] = final_obs[aid]
                            if aid in final_rewards:
                                reward_dict[aid] = final_rewards[aid]
                            if aid in final_info:
                                info_dict[aid] = final_info[aid]
                            terminated_dict[aid] = True
                            truncated_dict[aid] = False

                        # Log episode completion with proper rewards
                        episode_duration = time.time() - self._env_episode_start_time.get(env_idx, time.time())
                        env_total_reward = sum(self._agent_episode_rewards.get(aid, 0.0) + final_rewards.get(aid, 0.0)
                                               for aid in env_agents)

                        print("=" * 80)
                        print(f"🏁 [ENV {env_idx} EPISODE COMPLETE] Episode {self._env_episodes_completed[env_idx]}")
                        print(f"  Duration: {episode_duration:.1f}s, Steps: {self._env_episode_steps[env_idx]}")
                        print(f"  Total Reward: {env_total_reward:.2f}")
                        print(f"  Agent Rewards (final step):")
                        for aid in list(env_agents)[:4]:
                            agent_reward = self._agent_episode_rewards.get(aid, 0.0)
                            print(f"    {aid}: {agent_reward:.2f}")
                        if len(env_agents) > 4:
                            print(f"    ... and {len(env_agents) - 4} more agents")
                        print("=" * 80)

                        # Clear pending completion for this env
                        del self.pending_episode_completion[env_idx]

                    # Mark all as done if ALL environments have pending completions
                    # (In practice, this may be per-env done, but RLlib expects __all__)
                    all_done = all(terminated_dict.get(aid, False) or truncated_dict.get(aid, False)
                                   for aid in self._agent_ids)
                    terminated_dict['__all__'] = all_done
                    truncated_dict['__all__'] = all_done

                    # Fill missing agents with zeros
                    for flat_id in self._agent_ids:
                        if flat_id not in obs_dict:
                            obs_dict[flat_id] = self._build_observation(np.zeros(46, dtype=np.float32))
                        if flat_id not in reward_dict:
                            reward_dict[flat_id] = 0.0
                        if flat_id not in info_dict:
                            info_dict[flat_id] = {}

                    return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict

                # 1. Process auto-reset events from gRPC thread
                while not self.reset_event_queue.empty():
                    try:
                        reset_env_idx = self.reset_event_queue.get_nowait()

                        # Increment episode counter (previous episode completed)
                        self._env_episodes_completed[reset_env_idx] += 1
                        next_episode = self._env_episodes_completed[reset_env_idx]

                        # Reset episode tracking for this environment
                        self._env_episode_steps[reset_env_idx] = 0
                        self._env_episode_start_time[reset_env_idx] = time.time()
                        self._env_done_flags[reset_env_idx] = False

                        # Reset reward tracking for this environment's agents
                        env_agents = self._get_agents_for_logical_env(reset_env_idx)
                        for aid in env_agents:
                            self._agent_episode_rewards[aid] = 0.0

                        print(f"[STEP] Processed auto-reset for Env {reset_env_idx} (Episode {next_episode} starting)")
                    except queue.Empty:
                        break

                # 2. Format actions
                allactionkeys = self._get_all_action_keys()
                formattedactions = {}

                for flatid, action in actiondict.items():
                    if flatid not in self.agent_map:
                        continue
                    envidx, agentidx = self.agent_map[flatid]
                    if envidx not in formattedactions:
                        formattedactions[envidx] = {}

                    if isinstance(action, np.ndarray) and action.shape[0] == 5:
                        actionarray = np.clip(action, 0.0, 1.0).astype(np.float32)
                    else:
                        actionarray = np.array([0.5, 0.5, 0.5, 0.5, 0.0], dtype=np.float32)

                    agentkeys = allactionkeys.get((envidx, agentidx), [])
                    if agentkeys:
                        formattedactions[envidx][agentidx] = {key: actionarray for key in agentkeys}

                # 3. Queue actions (non-blocking)
                try:
                    self.action_queue.put(formattedactions, block=False)
                except queue.Full:
                    # Queue full: drop oldest action and retry
                    try:
                        self.action_queue.get_nowait()
                        self.action_queue.put(formattedactions, block=False)
                    except:
                        print("[STEP] Warning: Failed to queue actions")

                # 4. Read latest observations from buffer (non-blocking)
                with self.buffer_lock:
                    obs_dict = self.obs_buffer.copy() if self.obs_buffer else {}
                    reward_dict = self.reward_buffer.copy() if self.reward_buffer else {}
                    terminated_dict, truncated_dict = self.done_buffer if self.done_buffer else ({}, {})
                    terminated_dict = terminated_dict.copy()
                    truncated_dict = truncated_dict.copy()
                    info_dict = self.info_buffer.copy() if self.info_buffer else {}

                    # Ensure logical_env_id is in info (may be missing if buffer just initialized)
                    for flat_id in info_dict:
                        if "logical_env_id" not in info_dict[flat_id]:
                            env_idx, agent_idx = self.agent_map.get(flat_id, (0, 0))
                            logical_env_id = self._agent_logical_env.get(flat_id, agent_idx // 8)
                            info_dict[flat_id]["logical_env_id"] = logical_env_id

                # 5. Update episode tracking and reward accumulation
                for env_idx in range(self.num_envs):
                    if not self._env_done_flags.get(env_idx, False):
                        self._env_episode_steps[env_idx] += 1

                    # Accumulate rewards for agents in this environment
                    env_agents = self._get_agents_for_logical_env(env_idx)
                    for aid in env_agents:
                        if aid in reward_dict:
                            self._agent_episode_rewards[aid] = self._agent_episode_rewards.get(aid, 0.0) + reward_dict[aid]

                    # Check if environment finished (backup detection, primary is via episode_completion_queue)
                    if env_agents:
                        all_done = all(
                            terminated_dict.get(aid, False) or truncated_dict.get(aid, False)
                            for aid in env_agents
                        )
                        if all_done and not self._env_done_flags.get(env_idx, False):
                            # v8.7: Primary logging now happens in pending_episode_completion handler
                            # This is a backup in case buffer shows done before queue is processed
                            self._env_done_flags[env_idx] = True

                # 6. Periodic logging (detailed per-environment status)
                should_log = any(
                    self._env_episode_steps[i] % 100 == 0 and self._env_episode_steps[i] > 0
                    for i in range(self.num_envs)
                )
                if should_log:
                    first_milestone_env = next(
                        i for i in range(self.num_envs)
                        if self._env_episode_steps[i] % 100 == 0 and self._env_episode_steps[i] > 0
                    )

                    print("=" * 80)
                    print(f"[PROGRESS] Step Milestone={self._env_episode_steps[first_milestone_env]}")

                    # Summary of all environments (using global training time)
                    training_elapsed = time.time() - self._training_start_time
                    for summary_env_idx in range(self.num_envs):
                        episode_time = time.time() - self._env_episode_start_time[summary_env_idx] if self._env_episode_start_time[summary_env_idx] else 0

                        # Calculate current rewards for THIS logical environment
                        env_agents = self._get_agents_for_logical_env(summary_env_idx)
                        env_reward = sum(self._agent_episode_rewards.get(aid, 0.0) for aid in env_agents)

                        status = "⚡" if episode_time < 60 else "🔥"
                        done_flag = "✅ DONE" if self._env_done_flags.get(summary_env_idx, False) else "▶️ ACTIVE"
                        print(f"  {status} Env {summary_env_idx}: {done_flag}, Episode {self._env_episodes_completed[summary_env_idx]}, "
                              f"Steps={self._env_episode_steps[summary_env_idx]}, EpisodeTime={episode_time:.1f}s, "
                              f"CurrentReward={env_reward:.2f}")

                    # Performance metrics
                    avg_poll = np.mean(self.poll_durations) if self.poll_durations else 0
                    avg_send = np.mean(self.send_durations) if self.send_durations else 0
                    print(f"  📊 Total Training Elapsed: {training_elapsed:.1f}s")
                    print(f"  ⏱️ Avg poll={avg_poll*1000:.1f}ms, send={avg_send*1000:.1f}ms")
                    print("=" * 80)

                # 7. Fallback if no observations yet
                if not obs_dict:
                    obs_dict = {
                        flat_id: self._build_observation(np.zeros(46, dtype=np.float32))
                        for flat_id in self._agent_ids
                    }
                    reward_dict = {flat_id: 0.0 for flat_id in self._agent_ids}
                    terminated_dict = {flat_id: False for flat_id in self._agent_ids}
                    terminated_dict['__all__'] = False
                    truncated_dict = {flat_id: False for flat_id in self._agent_ids}
                    truncated_dict['__all__'] = False
                    info_dict = {flat_id: {} for flat_id in self._agent_ids}

                return obs_dict, reward_dict, terminated_dict, truncated_dict, info_dict

            except Exception as e:
                print(f"[STEP ERROR] {e}")
                import traceback
                traceback.print_exc()
                # Return terminal state
                fallback_obs = {
                    flat_id: self._build_observation(np.zeros(46, dtype=np.float32))
                    for flat_id in self._agent_ids
                }
                terminated_fallback = {flat_id: True for flat_id in self._agent_ids}
                terminated_fallback['__all__'] = True
                truncated_fallback = {flat_id: False for flat_id in self._agent_ids}
                truncated_fallback['__all__'] = False
                return (fallback_obs,
                        {flat_id: 0.0 for flat_id in self._agent_ids},
                        terminated_fallback,
                        truncated_fallback,
                        {flat_id: {} for flat_id in self._agent_ids})

        def _process_obs(self, raw_data):
            """Process observation from reset."""
            obs_nested = raw_data
            if isinstance(raw_data, tuple):
                obs_nested = raw_data[0]

            obs_dict = {}
            info_dict = {}

            for flat_id in self._agent_ids:
                env_idx, agent_idx = self.agent_map[flat_id]
                agent_obs_data = obs_nested.get(env_idx, {}).get(agent_idx, None)

                if agent_obs_data is not None:
                    if isinstance(agent_obs_data, dict):
                        obs_val = list(agent_obs_data.values())[0]
                    else:
                        obs_val = agent_obs_data
                    base_obs = np.array(obs_val, dtype=np.float32).flatten()
                else:
                    base_obs = np.zeros(46, dtype=np.float32)

                obs_dict[flat_id] = self._build_observation(base_obs)
                info_dict[flat_id] = {}

            return obs_dict, info_dict

        def render(self):
            return None

        def close(self):
            """Clean shutdown."""
            print("[ENV v8.7 ASYNC] Closing environment...")
            self._stop_grpc_thread()
            if hasattr(self.schola_env, 'close'):
                self.schola_env.close()
            print("[ENV v8.7 ASYNC] Closed")
