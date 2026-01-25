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

    def on_episode_step(self, *, worker, base_env: BaseEnv, episode: Episode, **kwargs):
        """
        Called on each episode step.
        Accumulate per-agent rewards.
        """
        # Get agent rewards from this step
        agent_rewards = episode.user_data.get("agent_rewards", {})
        agent_steps = episode.user_data.get("agent_steps", {})

        # Get last rewards for all agents
        for agent_id in episode.get_agents():
            # Get reward for this agent in this step
            reward = episode.last_reward_for(agent_id)

            # Accumulate
            if agent_id not in agent_rewards:
                agent_rewards[agent_id] = 0.0
                agent_steps[agent_id] = 0

            agent_rewards[agent_id] += reward
            agent_steps[agent_id] += 1

        episode.user_data["agent_rewards"] = agent_rewards
        episode.user_data["agent_steps"] = agent_steps

    def on_episode_end(self, *, worker, base_env: BaseEnv, policies: Dict[str, Policy],
                      episode: Episode, **kwargs):
        """
        Called when an episode ends.
        Log accumulated statistics.
        """
        agent_rewards = episode.user_data.get("agent_rewards", {})
        agent_steps = episode.user_data.get("agent_steps", {})

        if not agent_rewards:
            return

        # Calculate per-environment statistics
        # Assumes agent_id format: "agent_{env_idx}_{agent_idx}"
        env_rewards = {}
        env_agent_counts = {}

        for agent_id, total_reward in agent_rewards.items():
            try:
                # Parse env_idx from agent_id (format: agent_0_0, agent_0_1, etc.)
                parts = agent_id.split('_')
                if len(parts) >= 2:
                    env_idx = int(parts[1])

                    if env_idx not in env_rewards:
                        env_rewards[env_idx] = 0.0
                        env_agent_counts[env_idx] = 0

                    env_rewards[env_idx] += total_reward
                    env_agent_counts[env_idx] += 1
            except (ValueError, IndexError):
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
