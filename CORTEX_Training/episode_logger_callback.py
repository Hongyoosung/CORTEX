"""
Episode Logging Callback for CORTEX v8.0

Logs detailed per-agent and per-environment episode metrics.
Addresses the issue where episode-level rewards are not being tracked.
"""

from ray.rllib.algorithms.callbacks import DefaultCallbacks
from ray.rllib.env import BaseEnv
from ray.rllib.evaluation import Episode
from ray.rllib.policy import Policy
from typing import Dict, Optional
import numpy as np


class EpisodeLoggerCallback(DefaultCallbacks):
    """
    Custom callback to log detailed episode metrics.

    Tracks:
    - Per-agent rewards (individual agent performance)
    - Per-environment rewards (team performance)
    - Episode length and completion statistics
    """

    def on_episode_start(self, *, worker, base_env: BaseEnv, policies: Dict[str, Policy],
                        episode: Episode, **kwargs):
        """
        Called when an episode starts.
        Initialize episode-level tracking.
        """
        # Initialize custom metrics
        episode.user_data["agent_rewards"] = {}
        episode.user_data["agent_steps"] = {}
        episode.user_data["env_rewards"] = {}  # Track per logical environment
        episode.user_data["env_episode_counts"] = {}  # Track episodes per logical env
        episode.user_data["last_step_count"] = 0  # For detecting new episodes

        print(f"[CALLBACK] on_episode_start called for episode {episode.episode_id}")

    def on_episode_step(self, *, worker, base_env: BaseEnv, episode: Episode, **kwargs):
        """
        Called on each episode step.
        Accumulate per-agent rewards.

        Handles both Episode and EpisodeV2 API (RLlib version compatibility)
        EpisodeV2 stores rewards in _agent_reward_history[agent_id] as a list
        """
        # Initialize tracking if missing (happens after auto-reset when on_episode_start not called)
        if "agent_rewards" not in episode.user_data:
            print(f"[CALLBACK] WARNING: on_episode_step called but agent_rewards not initialized!")
            print(f"[CALLBACK] This suggests on_episode_start was not called for episode {episode.episode_id}")
            episode.user_data["agent_rewards"] = {}
            episode.user_data["agent_steps"] = {}
            episode.user_data["env_rewards"] = {}
            episode.user_data["env_episode_counts"] = {}
            episode.user_data["last_step_count"] = 0

        # Get agent rewards from this step
        agent_rewards = episode.user_data.get("agent_rewards", {})
        agent_steps = episode.user_data.get("agent_steps", {})

        # Get last rewards for all agents
        for agent_id in episode.get_agents():
            # Try to get reward using the appropriate API
            try:
                # Try Episode API first (has last_reward_for method)
                if hasattr(episode, 'last_reward_for') and callable(getattr(episode, 'last_reward_for')):
                    reward = episode.last_reward_for(agent_id)
                # EpisodeV2 API: Use _agent_reward_history (line 69 in EpisodeV2)
                elif hasattr(episode, '_agent_reward_history'):
                    reward_history = episode._agent_reward_history.get(agent_id, [])
                    reward = reward_history[-1] if reward_history else 0.0
                else:
                    # Can't get per-step rewards, skip
                    continue
            except (AttributeError, KeyError, TypeError, IndexError):
                continue

            # Accumulate
            if agent_id not in agent_rewards:
                agent_rewards[agent_id] = 0.0
                agent_steps[agent_id] = 0

            agent_rewards[agent_id] += reward
            agent_steps[agent_id] += 1

        episode.user_data["agent_rewards"] = agent_rewards
        episode.user_data["agent_steps"] = agent_steps

        # Periodic logging to confirm callback is working (every 500 steps)
        total_steps = sum(agent_steps.values())
        last_logged = episode.user_data.get("last_step_count", 0)
        if total_steps > 0 and total_steps % 500 == 0 and total_steps != last_logged:
            print(f"[CALLBACK] on_episode_step: Episode {episode.episode_id}, Total steps={total_steps}, "
                  f"Agents tracked={len(agent_rewards)}")
            episode.user_data["last_step_count"] = total_steps

    def on_episode_end(self, *, worker, base_env: BaseEnv, policies: Dict[str, Policy],
                      episode: Episode, **kwargs):
        """
        Called when an episode ends.
        Log accumulated statistics.

        Uses RLlib's built-in episode.agent_rewards if user_data tracking failed.
        """
        agent_rewards = episode.user_data.get("agent_rewards", {})
        agent_steps = episode.user_data.get("agent_steps", {})

        # Fallback: Use RLlib's built-in agent_rewards if our tracking is empty
        if not agent_rewards and hasattr(episode, 'agent_rewards'):
            # EpisodeV2 API: episode.agent_rewards is Dict[(agent_id, policy_id), float]
            # and _agent_reward_history is Dict[agent_id, List[float]]
            if hasattr(episode, '_agent_reward_history'):
                # Use reward history for per-agent breakdown
                for agent_id, rewards_list in episode._agent_reward_history.items():
                    if rewards_list:
                        agent_rewards[agent_id] = sum(rewards_list)
                        agent_steps[agent_id] = len(rewards_list)
            else:
                # Fallback to agent_rewards dict (older API or dict format)
                for key, total_reward in episode.agent_rewards.items():
                    # Handle both (agent_id, policy_id) tuple and plain agent_id
                    agent_id = key[0] if isinstance(key, tuple) else key
                    agent_rewards[agent_id] = total_reward
                    # Can't determine steps from cumulative rewards
                    agent_steps[agent_id] = 1

        if not agent_rewards:
            return

        # Calculate per-environment statistics
        # Use logical_env_id from info if available, otherwise parse from agent_id
        env_rewards = {}
        env_agent_counts = {}

        for agent_id, total_reward in agent_rewards.items():
            try:
                # Try to get logical_env_id from episode info first
                agent_info = episode._agent_to_last_info.get(agent_id, {})
                if isinstance(agent_info, dict) and "logical_env_id" in agent_info:
                    env_idx = agent_info["logical_env_id"]
                else:
                    # Fallback: Parse from agent_id (format: agent_0_0, agent_0_1, etc.)
                    # Note: This will group by physical env, not logical env!
                    parts = agent_id.split('_')
                    if len(parts) >= 3:
                        # For multi-env setup, logical env is determined by agent index
                        agent_idx = int(parts[2])
                        env_idx = agent_idx // 8  # Assuming 8 agents per logical env
                    elif len(parts) >= 2:
                        env_idx = int(parts[1])
                    else:
                        continue

                if env_idx not in env_rewards:
                    env_rewards[env_idx] = 0.0
                    env_agent_counts[env_idx] = 0

                env_rewards[env_idx] += total_reward
                env_agent_counts[env_idx] += 1
            except (ValueError, IndexError, AttributeError):
                pass

        # Log aggregate statistics
        all_rewards = list(agent_rewards.values())
        episode.custom_metrics["agent_reward_mean"] = np.mean(all_rewards) if all_rewards else 0.0
        episode.custom_metrics["agent_reward_min"] = np.min(all_rewards) if all_rewards else 0.0
        episode.custom_metrics["agent_reward_max"] = np.max(all_rewards) if all_rewards else 0.0
        episode.custom_metrics["agent_reward_std"] = np.std(all_rewards) if all_rewards else 0.0

        # Log per-environment statistics
        for env_idx, total_reward in env_rewards.items():
            episode.custom_metrics[f"env_{env_idx}_total_reward"] = total_reward
            episode.custom_metrics[f"env_{env_idx}_avg_reward"] = total_reward / env_agent_counts[env_idx]

        # Log overall environment statistics
        if env_rewards:
            env_total_rewards = list(env_rewards.values())
            episode.custom_metrics["env_reward_mean"] = np.mean(env_total_rewards)
            episode.custom_metrics["env_reward_min"] = np.min(env_total_rewards)
            episode.custom_metrics["env_reward_max"] = np.max(env_total_rewards)

        # Print detailed episode summary
        print("\n" + "="*80)
        print(f"📊 EPISODE COMPLETE - Episode {episode.episode_id}")
        print("="*80)
        print(f"  Episode Length: {episode.length}")
        print(f"  Total Return: {episode.total_reward:.2f}")
        print(f"\n  Per-Agent Statistics ({len(agent_rewards)} agents):")
        print(f"    Mean Reward: {episode.custom_metrics['agent_reward_mean']:.2f}")
        print(f"    Min Reward:  {episode.custom_metrics['agent_reward_min']:.2f}")
        print(f"    Max Reward:  {episode.custom_metrics['agent_reward_max']:.2f}")
        print(f"    Std Dev:     {episode.custom_metrics['agent_reward_std']:.2f}")

        if env_rewards:
            print(f"\n  Per-Environment Statistics ({len(env_rewards)} environments):")
            for env_idx in sorted(env_rewards.keys()):
                print(f"    Env {env_idx}: Total={env_rewards[env_idx]:.2f}, "
                      f"Avg={env_rewards[env_idx]/env_agent_counts[env_idx]:.2f}, "
                      f"Agents={env_agent_counts[env_idx]}")

        # Sample individual agent rewards (first 8 agents to avoid spam)
        print(f"\n  Sample Agent Rewards (first 8):")
        for i, (agent_id, reward) in enumerate(sorted(agent_rewards.items())[:8]):
            steps = agent_steps.get(agent_id, 0)
            avg_per_step = reward / steps if steps > 0 else 0.0
            print(f"    {agent_id}: {reward:.2f} ({steps} steps, {avg_per_step:.4f}/step)")

        if len(agent_rewards) > 8:
            print(f"    ... and {len(agent_rewards) - 8} more agents")

        print("="*80 + "\n")
