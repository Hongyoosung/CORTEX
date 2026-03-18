# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
import gymnasium as gym
import shutil
import os
import numpy as np

# Minari is required for these tests - fail immediately if not available
import minari
from minari import DataCollector
from minari.utils import get_dataset_path
from schola.minari.datacollector import ScholaDataCollector

from schola.core.protocols.protobuf.offlinegRPC import gRPCImitationProtocol
from schola.core.simulators.unreal.editor import UnrealEditor

def wrap(env, wrappers):
    if not isinstance(env, gym.Env):
        env = env()
    if wrappers:
        for wrapper in wrappers:
            env = wrapper(env)
    return env



def test_minari_integration(make_imitation_server, simple_policy, imitation_id_and_wrappers):
    env_id, wrappers = imitation_id_and_wrappers
    
    # Clean up any existing datasets from previous test runs
    for dataset_id in [f"{env_id}-base-v0", f"{env_id}-schola-v0"]:
        dataset_path = get_dataset_path(dataset_id)
        if os.path.exists(dataset_path):
            shutil.rmtree(dataset_path)
    
    port = make_imitation_server(env_id, simple_policy, wrappers)
    protocol = gRPCImitationProtocol(url="localhost", port=port)
    simulator = UnrealEditor()
    collector = ScholaDataCollector(protocol, simulator, seed = 123)
    wrapped_env = wrap(gym.make(env_id), wrappers)
    policy_env = gym.make(env_id)
    policy = simple_policy(policy_env)
    
    for i in range(10):
        collector.step()
    
    minari_collector = DataCollector(wrapped_env)
    obs, info = minari_collector.reset(seed=123)
    for i in range(10):
        action = policy(obs)
        obs, reward, terminated, truncated, info = minari_collector.step(action)
        if terminated or truncated:
            obs, info = minari_collector.reset()
   

    minari_dataset = minari_collector.create_dataset(f"{env_id}-base-v0")
    schola_dataset = collector.create_dataset(f"{env_id}-schola-v0")

    assert minari_dataset.total_steps == schola_dataset.total_steps, f"Total steps do not match: {minari_dataset.total_steps} != {schola_dataset.total_steps}"
    
    # MinariDataset uses total_episodes, not num_episodes
    assert minari_dataset.total_episodes == schola_dataset.total_episodes, f"Number of episodes do not match: {minari_dataset.total_episodes} != {schola_dataset.total_episodes}"

    for episode,schola_episode in zip(  minari_dataset.iterate_episodes(), schola_dataset.iterate_episodes()):
        assert np.all(episode.actions == schola_episode.actions), f"Actions do not match:"
        assert np.all(episode.observations == schola_episode.observations), f"Observations do not match"
        assert np.all(episode.rewards == schola_episode.rewards), f"Rewards do not match"
        assert np.all(episode.terminations == schola_episode.terminations), f"Terminations do not match"
        assert np.all(episode.truncations == schola_episode.truncations), f"Truncations do not match"
        assert episode.infos == schola_episode.infos, f"Infos do not match"


    collector.close()
    minari_collector.close()

    