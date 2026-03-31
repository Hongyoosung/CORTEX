# Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.

"""
Utility functions for working with stable baselines 3
"""

from math import sqrt
from pathlib import Path
from typing import Dict, List, Tuple
import torch as th
from stable_baselines3 import PPO
from stable_baselines3.common.base_class import BaseAlgorithm
import os
import gymnasium as gym
import numpy as np
from stable_baselines3.common.vec_env.base_vec_env import (
    VecEnvObs,
    VecEnv,
    VecEnvWrapper,
)
from schola.core.model import ScholaModel
from gymnasium.spaces import Box, flatdim
import stable_baselines3 as sb3

# The below code is adapted from https://github.com/DLR-RM/stable-baselines3/blob/v2.2.1/docs/guide/export.rst

# The MIT License
#
# Copyright (c) 2019 Antonin Raffin
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

# Modifications Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.


class SB3ScholaModel(ScholaModel):
    """
    A PyTorch Module that wraps a stable baselines policy and extracts the necessary components to export to ONNX.

    Parameters
    ----------
    policy : stable_baselines3.common.policies.BasePolicy
        The policy to wrap.
    action_space : gym.spaces.Dict
        The action space to wrap.

    Attributes
    ----------
    extractor : _CombinedExtractorWrapper
        The wrapped feature extractor.

    action_net : th.nn.Module
        The wrapped action network.
    """

    def __init__(self, policy, action_space):
        super().__init__()
        self.action_space = action_space
        self.policy = policy
        self.policy.set_training_mode(False)

    def get_logits(self, x) -> th.Tensor: ...

    def forward(self, *args) -> Tuple[th.Tensor, th.Tensor]:
        state = args[-1]
        x = args[:-1]
       
        if isinstance(self.policy.observation_space, gym.spaces.Dict):
            x = {k: v for k, v in zip(self.policy.observation_space.spaces.keys(), x)}
        else:
            x = x[0] # unpack x from a tuple, 
        logits = self.get_logits(x)

        return logits, state

    def save_as_onnx(self, export_path: str, onnx_opset: int = 17) -> None:
        # For non-RNN policies, the state is not used values are set to arbitrary dummy values
        seq_len = 1
        state_dim = 1
        # Use the unwrapped action space here so that we output in a nice dictionary format

        # make directories if they don't exist
        dir_path = Path(export_path).parent
        dir_path.mkdir(parents=True, exist_ok=True)

        input_names = []
        inputs = []

        if not isinstance(self.policy.action_space, gym.spaces.Dict):
            output_names = ["action"]
        else:
            output_names = list(self.action_space.spaces.keys())

        # Output is flattened so we only ever output 2 items
        output_names += ["state_out"]
        if isinstance(self.policy.observation_space, gym.spaces.Dict):
            for obs_space_name, obs_space in self.policy.observation_space.spaces.items():
                input_names.append(obs_space_name)
                # Just flatten discrete and boolean spaces
                if not isinstance(obs_space, gym.spaces.Box):
                    obs_space = gym.spaces.utils.flatten_space(obs_space)
                inputs.append(th.rand(1, *obs_space.shape))
        else:
            input_names.append("obs")
            obs_space = gym.spaces.utils.flatten_space(self.policy.observation_space)
            inputs.append(th.rand(1, *obs_space.shape))

        # add the state input
        input_names.append("state_in")
        inputs.append(th.rand((1, seq_len, state_dim)))
        # Get the input dim from the model
        # input_dim = gym.spaces.utils.flatten_space(model.observation_space).shape
        # Export the model to ONNX
        # Pass the path as a string (not a file object) for compatibility with newer PyTorch ONNX exporters
        # Use dynamo=False for legacy exporter which properly handles input_names parameter
        th.onnx.export(
            self,
            tuple(inputs),
            str(export_path),
            opset_version=onnx_opset,
            input_names=input_names,
            output_names=output_names,
            dynamic_axes={k: {0: "batch_size"} for k in input_names},
            dynamo=False,
        )
        print("Model exported to ONNX")


class SB3PPOModel(SB3ScholaModel):

    def get_logits(self, x):
        x = self.policy.features_extractor(x)
        x, _ = self.policy.mlp_extractor(x)
        x = self.policy.action_net(x)
        return x


# A2C and PPO share the same policies
class SB3A2CModel(SB3PPOModel): ...


# All possible policies for BC share the same policies
class SB3BCModel(SB3PPOModel): ...


class SB3SACModel(SB3ScholaModel):

    def get_logits(self, x):
        x = self.policy.actor.features_extractor(x)
        x = self.policy.actor.latent_pi(x)
        x = self.policy.actor.mu(x)
        return x


class SB3TD3Model(SB3ScholaModel):

    def get_logits(self, x):
        x = self.policy.actor.features_extractor(x)
        x = self.policy.actor.mu(x)
        return x


# DDPG and TD3 share the same policies
class SB3DDPGModel(SB3TD3Model): ...


class SB3DQNModel(SB3ScholaModel):

    def get_logits(self, x):
        x = self.policy.q_net(x)
        return x


def get_scholasb3_model(model: BaseAlgorithm) -> ScholaModel:
    """
    Get the ScholaModel for a stable baselines algorithm.

    Parameters
    ----------
    model : stable_baselines3.common.base_class.BaseAlgorithm
        The model to get the ScholaModel for.

    Returns
    -------
    ScholaModel
        The ScholaModel for the given model.
    """
    if isinstance(model, sb3.PPO):
        return SB3PPOModel(model.policy, model.policy.action_space)
    elif isinstance(model, sb3.A2C):
        return SB3A2CModel(model.policy, model.policy.action_space)
    elif isinstance(model, sb3.SAC):
        return SB3SACModel(model.policy, model.policy.action_space)
    elif isinstance(model, sb3.TD3):
        return SB3TD3Model(model.policy, model.policy.action_space)
    elif isinstance(model, sb3.DDPG):
        return SB3DDPGModel(model.policy, model.policy.action_space)
    elif isinstance(model, sb3.DQN):
        return SB3DQNModel(model.policy, model.policy.action_space)
    else:
        raise ValueError(f"Unsupported model type: {type(model)}")


# end of adapted code
def save_model_as_onnx(model: BaseAlgorithm, export_path: str) -> None:
    """
    Save a stable baselines model as ONNX.

    Parameters
    ----------
    model : stable_baselines3.common.base_class.BaseAlgorithm
        The model to save as ONNX.
    export_path : str
        The path to save the model to.
    """
    model = get_scholasb3_model(model)
    model.save_as_onnx(export_path)


def convert_ckpt_to_onnx_for_unreal(
    trainer=PPO,
    model_path="./ckpt/ppo_final.zip",
    export_path="./ckpt/OnnxFiles/Model.onnx",
) -> None:
    """
    Convert a stable baselines model to ONNX for use in Unreal.

    Parameters
    ----------
    trainer : stable_baselines3.common.base_class.BaseAlgorithm
        The trainer to load the model from.
    model_path : str
        The path to the model to convert.
    export_path : str
        The path to save the converted model to.
    """
    model = trainer.load(model_path, device="cpu")
    model = get_scholasb3_model(model)
    model.save_as_onnx(export_path)


class VecMergeDictActionWrapper(VecEnvWrapper):
    """
    A vectorized wrapper for merging a dictionary of actions into 1 single action.
    All actions in the dictionary must be of compatible types.

    Parameters
    ----------
    venv : VecEnv
        The vectorized environment being wrapped.
    """

    def __init__(self, venv: VecEnv):
        all_action_spaces = list(venv.action_space.spaces.values())
        assert len(all_action_spaces) > 0, "No Action Spaces to merge."
        action_space = all_action_spaces[0].merge(*all_action_spaces)
        super().__init__(venv=venv, action_space=action_space)

    def reset(self) -> VecEnvObs:
        return self.venv.reset()

    def step(
        self, action: np.ndarray
    ) -> Tuple[VecEnvObs, np.ndarray, np.ndarray, List[Dict]]:
        return self.venv.step(action)

    def step_async(self, actions: np.ndarray) -> None:
        self.venv.step_async(actions)

    def step_wait(self) -> Tuple[VecEnvObs, np.ndarray, np.ndarray, List[Dict]]:
        return self.venv.step_wait()


try:
    from matplotlib import pyplot as plt
except ImportError:
    # matplot lib is not installed, raise a lazy error if someone tries to use the RenderImagesWrapper
    plt = None


class VecMergeDictObservationWrapper(VecEnvWrapper):
    """
    A vectorized wrapper for merging a dictionary of actions into 1 single action.
    All actions in the dictionary must be of compatible types.

    Parameters
    ----------
    venv : VecEnv
        The vectorized environment being wrapped.
    """

    def __init__(self, venv: VecEnv):
        all_observation_spaces = list(venv.observation_space.spaces.values())
        assert len(all_observation_spaces) > 0, "No Observation Spaces to merge."
        observation_space = all_observation_spaces[0].merge(*all_observation_spaces)
        super().__init__(venv=venv, observation_space=observation_space)

    def reset(self) -> VecEnvObs:
        return self.venv.reset()

    def step(
        self, action: np.ndarray
    ) -> Tuple[VecEnvObs, np.ndarray, np.ndarray, List[Dict]]:
        return self.venv.step(action)

    def step_async(self, actions: np.ndarray) -> None:
        self.venv.step_async(actions)

    def step_wait(self) -> Tuple[VecEnvObs, np.ndarray, np.ndarray, List[Dict]]:
        return self.venv.step_wait()


class RenderImagesWrapper(VecEnvWrapper):
    """
    Renders image observations to an interactive matplotlib window. It assumes that the observations are square RGB images, and attempts to reshape any box observation to 3xLxL.

    Parameters
    ----------
    venv : VecEnv
        The vectorized environment being wrapped.
    """

    def __init__(self, venv: VecEnv):
        if plt is None:
            raise ImportError(
                "You must install matplotlib in order to use the RenderImagesWrapper."
            )
        self.image_obs = []
        self._num_envs = venv.num_envs
        for obs_space_name, obs_space in venv.observation_space.spaces.items():
            if isinstance(obs_space, gym.spaces.Box):
                self.image_obs.append((obs_space_name, obs_space.shape))
        plt.ion()
        self.axs = []
        self.ims = [[] for _ in range(self._num_envs)]
        for row in range(self._num_envs):
            for col in range(len(self.image_obs)):
                # Note index starts at 1
                index = row * len(self.image_obs) + col + 1
                self.axs.append(plt.subplot(self._num_envs, len(self.image_obs), index))
                # TODO dynamically toggle greyscale
                if obs_space.shape[0] == 1:
                    cmap = "grey"
                else:
                    cmap = None
                default_ndarray = self.convert_to_plt_format(np.zeros(obs_space.shape))
                self.ims[row].append(
                    self.axs[index - 1].imshow(
                        default_ndarray, cmap=cmap, vmin=0.0, vmax=1.0
                    )
                )

        super().__init__(venv=venv)

    def convert_to_plt_format(self, obs: np.ndarray) -> np.ndarray:
        """
        Convert to a format supported by matplotlib. (e.g. (W,H), (W,H,3), and (W,H,4)). No Chanels or Chanels last, from Chanels first.

        Parameters
        ----------
        obs : np.ndarray
            The observation to convert.

        Returns
        -------
        np.ndarray
            The converted observation.
        """
        if obs.shape[0] == 1:
            return obs.squeeze()
        else:
            return obs.transpose(1, 2, 0)

    def close(self):
        super().close()
        plt.ioff()
        plt.show()

    def update_images(self, obs: Dict[str, np.ndarray]) -> Dict[str, np.ndarray]:
        """
        Updates the images in the plt window with the given observations.

        Parameters
        ----------
        obs: Dict[str,np.ndarray]
            Maps the names of the observations to the observation data.

        Returns
        -------
        Dict[str,np.ndarray]
            The original observation.
        """
        for col, (image_obs_name, shape) in enumerate(self.image_obs):
            temp_obs = np.clip(obs[image_obs_name], 0.0, 1.0)
            # yoink out the batch dim at the front of the buffer
            for row in range(temp_obs.shape[0]):
                new_data = self.convert_to_plt_format(temp_obs[row])
                self.ims[row][col].set_data(new_data)
        plt.pause(0.001)

        return obs

    def reset(self) -> VecEnvObs:
        return self.update_images(self.venv.reset())

    def step(
        self, action: np.ndarray
    ) -> Tuple[VecEnvObs, np.ndarray, np.ndarray, List[Dict]]:

        obs, rewards, dones, infos = self.venv.step(action)
        return self.update_images(obs), rewards, dones, infos

    def step_async(self, actions: np.ndarray) -> None:
        self.venv.step_async(actions)

    def step_wait(self) -> Tuple[VecEnvObs, np.ndarray, np.ndarray, List[Dict]]:
        obs, rewards, dones, infos = self.venv.step_wait()
        return self.update_images(obs), rewards, dones, infos
