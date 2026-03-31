# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

import pytest
import numpy as np
from schola.core.simulators.unreal.editor import UnrealEditor
from schola.core.protocols.protobuf.gRPC import gRPCProtocol
from schola.rllib.env import RayEnv
from ray.rllib.env.multi_agent_env import MultiAgentEnv

# Check if PettingZoo is available
try:
    import pettingzoo
    PETTINGZOO_AVAILABLE = True
except ImportError:
    PETTINGZOO_AVAILABLE = False

# Skip all tests in this file if PettingZoo is not available
pytestmark = pytest.mark.skipif(not PETTINGZOO_AVAILABLE, reason="PettingZoo not installed")


def test_rayenv_pettingzoo_create(make_rllib_pettingzoo_env):
    """Test creating RayEnv with a PettingZoo environment."""
    env = make_rllib_pettingzoo_env("pistonball_v6")
    
    assert env.num_envs == 1
    assert env.single_observation_space is not None, f"Single observation space should not be None"
    assert env.single_action_space is not None, f"Single action space should not be None"
    assert env.single_observation_spaces is not None, f"Single observation spaces should not be None"
    assert env.single_action_spaces is not None, f"Single action spaces should not be None"
    
    # PettingZoo pistonball_v6 should have 20 agents (piston_0 through piston_19)
    assert len(env.possible_agents) == 20, "PettingZoo pistonball_v6 should have 20 agents"
    
    # Verify observation spaces match pistonball_v6 spec
    # Observation Shape: (457, 120, 3), Values: (0, 255)
    for agent_id in env.possible_agents:
        obs_space = env.single_observation_spaces[agent_id]
        assert obs_space.shape == (457, 120, 3), f"Agent {agent_id} observation shape should be (457, 120, 3), got {obs_space.shape}"
        assert obs_space.dtype == np.uint8, f"Agent {agent_id} observation dtype should be uint8, got {obs_space.dtype}"
        assert np.all(obs_space.low == 0), f"Agent {agent_id} observation low should be 0"
        assert np.all(obs_space.high == 255), f"Agent {agent_id} observation high should be 255"
    
    # Verify action spaces match pistonball_v6 spec (continuous mode)
    # Action Shape: (1,), Values: [-1, 1]
    for agent_id in env.possible_agents:
        action_space = env.single_action_spaces[agent_id]
        assert action_space.shape == (1,), f"Agent {agent_id} action shape should be (1,), got {action_space.shape}"
        assert np.all(action_space.low == -1.0), f"Agent {agent_id} action low should be -1.0, got {action_space.low}"
        assert np.all(action_space.high == 1.0), f"Agent {agent_id} action high should be 1.0, got {action_space.high}"
    
    env.close()

def test_rayenv_pettingzoo_reset(make_rllib_pettingzoo_env):
    """Test RayEnv reset with PettingZoo environment."""
    env = make_rllib_pettingzoo_env("pistonball_v6")
    
    observations, infos = env.reset()
    
    # Should return dict format (not list)
    assert isinstance(observations, dict)
    assert isinstance(infos, dict)
    
    # Should have multiple agents
    assert len(observations) == 20, "Pistonball_v6 environment should have 20 agents"
    
    # All agents should be in possible_agents
    for agent_id in observations.keys():
        assert agent_id in env.possible_agents
    
    # Check observations are in observation space
    for agent_id, obs in observations.items():
        assert obs in env.single_observation_spaces[agent_id], f"Observation {obs} should be in agent {agent_id} observation space {env.single_observation_spaces[agent_id]}"
    
    env.close()

def test_rayenv_pettingzoo_step(make_rllib_pettingzoo_env, make_pettingzoo_env):
    """Test RayEnv step with PettingZoo environment by comparing with local environment."""
    # Create remote environment (via Schola)
    remote_env = make_rllib_pettingzoo_env("pistonball_v6")
    
    # Create local PettingZoo environment for comparison
    local_env = make_pettingzoo_env("pistonball_v6")
    
    # Set seed for reproducibility
    seed = 42
    
    # Reset both environments with the same seed
    remote_obs, remote_infos = remote_env.reset(seed=seed)
    local_obs, local_infos = local_env.reset(seed=seed)
    
    # Verify observations match
    assert set(remote_obs.keys()) == set(local_obs.keys()), "Agent keys should match between remote and local"
    for agent_id in remote_obs.keys():
        np.testing.assert_array_almost_equal(
            remote_obs[agent_id], 
            local_obs[agent_id],
            err_msg=f"Initial observations for agent {agent_id} should match"
        )
    
    # Create the same actions for both environments
    actions = {}
    for agent_id in remote_obs.keys():
        # Use a seeded random generator for reproducible actions
        rng = np.random.RandomState(seed + hash(agent_id) % 2**31)
        action_space = remote_env.single_action_spaces[agent_id]
        # Sample action manually for reproducibility
        actions[agent_id] = rng.uniform(
            action_space.low, 
            action_space.high, 
            action_space.shape
        ).astype(action_space.dtype)
    
    # Step both environments with the same actions
    remote_obs, remote_rewards, remote_terminateds, remote_truncateds, remote_infos = remote_env.step(actions)
    local_obs, local_rewards, local_terminateds, local_truncateds, local_infos = local_env.step(actions)
    
    # Check return types for remote environment
    assert isinstance(remote_obs, dict)
    assert isinstance(remote_rewards, dict)
    assert isinstance(remote_terminateds, dict)
    assert isinstance(remote_truncateds, dict)
    assert isinstance(remote_infos, dict)
    
    # Check for __all__ key in remote environment (RLlib convention)
    assert "__all__" in remote_terminateds
    assert "__all__" in remote_truncateds
    
    # Compare observations
    assert set(remote_obs.keys()) == set(local_obs.keys()), "Agent keys should match after step"
    for agent_id in remote_obs.keys():
        np.testing.assert_array_almost_equal(
            remote_obs[agent_id],
            local_obs[agent_id],
            err_msg=f"Observations for agent {agent_id} should match"
        )
    
    # Compare rewards
    for agent_id in remote_rewards.keys():
        if agent_id != "__all__":  # Skip RLlib's __all__ key
            assert agent_id in local_rewards, f"Agent {agent_id} should have reward in local env"
            assert remote_rewards[agent_id] == pytest.approx(local_rewards[agent_id], rel=1e-6), \
                f"Rewards for agent {agent_id} should match: remote={remote_rewards[agent_id]}, local={local_rewards[agent_id]}"
    

    # Verify __all__ flag is correctly set based on individual agent flags
    assert remote_terminateds["__all__"] == all(local_terminateds.values()), \
        "__all__ terminated flag should be True if all agents are terminated"
    assert remote_truncateds["__all__"] == all(local_truncateds.values()), \
        "__all__ truncated flag should be True if all agents are truncated"
    
    remote_env.close()
    local_env.close()


def test_rayenv_pettingzoo_multiple_agents(make_rllib_pettingzoo_env):
    """Test that PettingZoo multi-agent functionality works correctly."""
    env = make_rllib_pettingzoo_env("pistonball_v6")
    
    observations, infos = env.reset()
    
    # pistonball_v6 has 20 agents by default
    assert len(observations) == 20, f"Expected 20 agents, got {len(observations)}"
    
    # Step through environment
    for _ in range(10):
        actions = {agent_id: env.single_action_spaces[agent_id].sample() 
                   for agent_id in observations.keys()}
        observations, rewards, terminateds, truncateds, infos = env.step(actions)
        
        # Verify multi-agent consistency
        assert len(rewards) == 20, "Should have rewards for all agents"
        
        # All active agents should have observations
        for agent_id in observations.keys():
            if agent_id != "__all__":
                assert agent_id in env.possible_agents
    
    env.close()


def test_rayenv_pettingzoo_inheritance(make_rllib_pettingzoo_env):
    """Test that RayEnv with PettingZoo inherits from correct base classes."""
    env = make_rllib_pettingzoo_env("simple_spread_v3")
    
    # Check inheritance
    assert isinstance(env, MultiAgentEnv), "Should inherit from MultiAgentEnv"
    
    # Check that required properties exist
    assert hasattr(env, 'possible_agents')
    assert hasattr(env, 'num_agents')
    assert hasattr(env, 'observation_space')
    assert hasattr(env, 'action_space')
    
    env.close()

from ray.rllib.algorithms.ppo import PPOConfig
from ray.rllib.core.rl_module.multi_rl_module import MultiRLModuleSpec
from ray.rllib.core.rl_module.rl_module import RLModuleSpec
from ray.rllib.policy.policy import PolicySpec
from ray.rllib.connectors.env_to_module import FlattenObservations
#import ray registry
from ray.tune.registry import register_env

def test_multiagent_training(make_pettingzoo_env_server, make_pettingzoo_env):
    env_server_port = make_pettingzoo_env_server(make_pettingzoo_env("simple_spread_v3"))
    
    def make(*args,**kwargs):
        simulator = UnrealEditor()
        protocol = gRPCProtocol(url="localhost", port=env_server_port)
        return RayEnv(protocol, simulator)

    register_env("simple_spread_v3", make)
    
    config = (
        PPOConfig()
        .api_stack(
            enable_rl_module_and_learner=True,
            enable_env_runner_and_connector_v2=True,
        )
        .environment(env="simple_spread_v3")
        .framework("torch")
        .env_runners(
            num_env_runners=0,  # Use local runner only (no remote workers)
            env_to_module_connector=lambda env: FlattenObservations(
                input_observation_space=env.single_observation_space,
                input_action_space=env.single_action_space,
                multi_agent=True,
            ),
        )
        .multi_agent(
            policies={"shared_policy": PolicySpec()},
            policy_mapping_fn=lambda agent_id, *args, **kwargs: "shared_policy",
        )
    )
    algo = config.build_algo()
    # Just run a few iterations to verify training works
    result = algo.train()
    assert result is not None
    algo.stop()