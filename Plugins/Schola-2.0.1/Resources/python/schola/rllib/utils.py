""" Utilities for Working With Ray (e.g. Exporting, and Image Handling) """
# Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

import pathlib
from ray.rllib.algorithms.algorithm import Algorithm
from ray.rllib.connectors.env_to_module import FlattenObservations
from ray.rllib.core.rl_module.rl_module import RLModule
import torch.nn as nn
from gymnasium.spaces import Box, flatdim
from functools import singledispatch
from ray.rllib.policy import Policy

import torch as th
from ray.rllib.policy.sample_batch import SampleBatch
import os
import numpy as np
from schola.core.model import ScholaModel
import gymnasium as gym
import gymnasium as gym
from gymnasium import spaces
import numpy as np
import copy
from schola.rllib.env import RayVecEnv
from typing import Any, List, Optional, Tuple, Dict, Union
import logging

@singledispatch
def export_onnx_from_policy(arg, path: pathlib.Path):
    """
    Export an RLlib policy to ONNX format.

    This is a generic function that uses singledispatch to handle different
    input types (Policy objects, dictionaries of policies, or checkpoint paths).

    Parameters
    ----------
    arg : Policy or dict or str
        The policy to export. Can be:
        - A Policy object
        - A dictionary mapping policy names to Policy objects
        - A string path to a policy checkpoint
        - A pathlib.Path object to a policy checkpoint
    path : str
        The directory path where the ONNX model will be saved.
    policy_name : str, optional
        The name to use for the exported policy file. If None, uses "Policy" as default.

    Raises
    ------
    TypeError
        If the argument type is not supported.

    Notes
    -----
    This function has multiple registered implementations for different
    argument types. See the individual @export_onnx_from_policy.register
    implementations for specific type handling.
    """
    raise TypeError(
        f"Cannot export ONNX from Policy/Checkpoint stored as {type(arg)}. Pass a Policy, Dictionary of Policies, or a path to a Policy Checkpoint"
    )


@export_onnx_from_policy.register
def _(arg: Policy, path: pathlib.Path):
    if path.is_dir():
        path = path / "default_policy.onnx"
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
    schola_model = RLLibScholaModel(arg)
    schola_model.save_as_onnx(path)

@export_onnx_from_policy.register
def _(arg: RLModule, path: pathlib.Path):
    if path.is_dir():
        path = path / "default_policy.onnx"
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
    schola_model = ScholaRLModule(arg)
    schola_model.save_as_onnx(path)

@export_onnx_from_policy.register
def _(arg: dict, path: pathlib.Path):
    if path.is_file():
        path = path.parent
        logging.warning(f"Path is a file but a dictionary of policies was passed, using parent directory: {path}")
    # policy name is ignored, as the dictionary has them already
    for policy_name, policy in arg.items():
        export_onnx_from_policy(policy, path / f"{policy_name}.onnx")

@export_onnx_from_policy.register
def _(arg: str, path: pathlib.Path):
    # dir and other stuff is handled later
    policy = Policy.from_checkpoint(arg)
    export_onnx_from_policy(policy, path)

@export_onnx_from_policy.register
def _(arg: pathlib.Path, path: pathlib.Path):
    # dir and other stuff is handled later
    policy = Policy.from_checkpoint(str(arg))
    export_onnx_from_policy(policy, path)

@export_onnx_from_policy.register
def _(arg: Algorithm, path: pathlib.Path):
    # arg.spaces["__env_single__"] is a tuple (observation_space, action_space)
    obs_space, act_space = arg.spaces["__env_single__"]
    schola_model = ScholaRLModule(arg.get_module(), obs_space, act_space)
    schola_model.save_as_onnx(path)

class ScholaRLModule(th.nn.Module):
    def __init__(self, rl_module, observation_space = None, action_space = None):
        super().__init__()
        self.rl_module = rl_module.to("cpu")
        self.rl_module.eval()
        self.observation_space = observation_space if observation_space is not None else rl_module.observation_space
        self.action_space = action_space if action_space is not None else rl_module.action_space
       
    def forward(self, *args):
        state = args[-1]
        x = args[:-1]
       
        if isinstance(self.observation_space, gym.spaces.Dict):
            # Concatenate all values in the dict in a consistent order
            x = th.cat(x, dim=-1)
        else:
            x = x[0] # unpack x from a tuple
            
        model_out = self.rl_module.forward_inference({"obs": x, "state_in": state})

        if isinstance(self.action_space, gym.spaces.Dict):
            outputs, _ = self.make_composite_output(self.action_space, model_out["action_dist_inputs"], 0)
        else:
            outputs, _ = self.make_fundamental_output(self.action_space, model_out["action_dist_inputs"], 0)

        return (*outputs, model_out.get("state_out", state))


    def save_as_onnx(self, export_path: str, onnx_opset: int = 17) -> None:
        seq_len = 1
        state_dim = 1
        # Use the unwrapped action space here so that we output in a nice dictionary format

        # make directories if they don't exist
        dir_path = pathlib.Path(export_path).parent
        dir_path.mkdir(parents=True, exist_ok=True)

        input_names = []
        inputs = []

        if not isinstance(self.action_space, gym.spaces.Dict):
            output_names = ["action"]
        else:
            output_names = list(self.action_space.spaces.keys())

        # Output is flattened so we only ever output 2 items
        output_names += ["state_out"]
        if isinstance(self.observation_space, gym.spaces.Dict):
            for obs_space_name, obs_space in self.observation_space.spaces.items():
                input_names.append(obs_space_name)
                # Just flatten discrete and boolean spaces
                if not isinstance(obs_space, gym.spaces.Box):
                    obs_space = gym.spaces.utils.flatten_space(obs_space)
                inputs.append(th.rand(1, *obs_space.shape))
        else:
            input_names.append("obs")
            obs_space = gym.spaces.utils.flatten_space(self.observation_space)
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

    def make_fundamental_output(self, space, logits, space_start):
        outputs = []
        space_size = flatdim(space)
        # remove the extra dimensions containing variance etc from the outputs
        if isinstance(space, Box):
            outputs.append(logits[:, space_start : space_start + space_size])
            space_end = 2 * space_size
        else:
            outputs.append(logits[:, space_start : space_start + space_size])
            space_end = space_size
        return outputs, space_end

    def make_composite_output(self, space, logits, space_start):
        outputs = []
        outputs = []
        curr_dim = 0
        for space_name, space in self.rl_module.action_space.items():
            outputs, space_end = self.make_fundamental_output(space, logits, curr_dim)
            curr_dim += space_end
        return outputs, curr_dim

# The below code is adapted from https://github.com/ray-project/ray/blob/master/rllib/policy/torch_policy_v2.py
"""
Copyright 2023 Ray Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
"""
# Modifications Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

class RLLibScholaModel(ScholaModel):
    """
    Wrapper for RLlib policies to enable ONNX export for use with Schola.

    This class adapts RLlib policies to the ScholaModel interface, allowing
    them to be exported to ONNX format for inference in Unreal Engine.

    Parameters
    ----------
    policy : ray.rllib.policy.Policy
        The RLlib policy to wrap for ONNX export.

    Attributes
    ----------
    _policy : ray.rllib.policy.Policy
        The wrapped RLlib policy.
    _model : torch.nn.Module
        The underlying PyTorch model from the policy.
    """

    def __init__(self, policy):
        super().__init__()
        self._policy = policy
        self._model = policy.model.to("cpu")

    def forward(self, *args):
        """
        Forward pass through the model. Removes variance outputs, to make compatible with Unreal.
        """
        seq_len = [1]
        state = args[-1]
        inputs = args[:-1]

        self._policy._get_dummy_batch_from_view_requirements(1)
        self._policy._lazy_tensor_dict(self._policy._dummy_batch)

        dummy_inputs = {
            k: self._policy._dummy_batch[k]
            for k in self._policy._dummy_batch.keys()
            if k != "is_training"
        }

        dummy_inputs["state_in_0"] = state
        if isinstance(self._policy.observation_space, gym.spaces.Dict):
            dummy_inputs["obs"] = {
                k: v
                for k, v in zip(
                    self._policy.observation_space.spaces.keys(), inputs
                )
            }
        else:
            dummy_inputs["obs"] = inputs[0]
            
        dummy_inputs["obs_flat"] = th.cat(
            [th.flatten(input_tensor, start_dim=1) for input_tensor in inputs], dim=1
        )

        model_out = self._model.forward(dummy_inputs, [state], seq_len)
        # model_out[0] is the logits, model_out[1] is the state
        # check if state is 3D meaning a rnn model, if not, view it as 1x1x1
        # Logits output other miscelanous outputs so we need to mask them out
        return self.make_outputs(model_out[0], model_out[1])

    def make_outputs(self, logits, state):
        """
        Process model outputs by extracting action logits and state.

        This method processes the raw model outputs to extract the appropriate
        action logits for each action space component and formats the state tensor.

        Parameters
        ----------
        logits : torch.Tensor
            The raw logits output from the model.
        state : list of torch.Tensor
            The model's internal state (e.g., RNN hidden states).

        Returns
        -------
        tuple
            A tuple containing:
            - Action logits for each action space component
            - The formatted state tensor
        """
        if state[0].shape != 3:
            state = [state[0].view(1, 1, -1)]

        if isinstance(self._model.action_space, gym.spaces.Dict):
            outputs, _ = self.make_composite_output(self._model.action_space, logits, 0)
        else:
            outputs, _ = self.make_fundamental_output(self._model.action_space, logits, 0)

        outputs.append(state)
        return tuple(outputs)

    def make_fundamental_output(self, space, logits, space_start):
        outputs = []
        space_size = flatdim(space)
        # remove the extra dimensions containing variance etc from the outputs
        if isinstance(space, Box):
            outputs.append(logits[:, space_start : space_start + space_size])
            space_end = 2 * space_size
        else:
            outputs.append(logits[:, space_start : space_start + space_size])
            space_end = space_size
        return outputs, space_end

    def make_composite_output(self, space, logits, space_start):
        outputs = []
        outputs = []
        curr_dim = 0
        for space_name, space in self._model.action_space.items():
            outputs, space_end = self.make_fundamental_output(space, logits, curr_dim)
            curr_dim += space_end
        return outputs, curr_dim

    def save_as_onnx(self, export_path: pathlib.Path, onnx_opset: int = 17) -> None:
        policy = self._policy
        export_path.parent.mkdir(parents=True, exist_ok=True)

        # Replace dummy batch with a batch of size 1 for inference.
        # Disable the preprocessor API to get the unflattened observations in the _dummy_batch
        policy._dummy_batch = policy._get_dummy_batch_from_view_requirements(1)

        # Due to different view requirements for the different columns,
        # columns in the resulting batch may not all have the same batch size.
        policy._lazy_tensor_dict(policy._dummy_batch)

        # Provide dummy state inputs if not an RNN (torch cannot jit with
        # returned empty internal states list).
        if "state_in_0" not in policy._dummy_batch:
            policy._dummy_batch["state_in_0"] = policy._dummy_batch[
                SampleBatch.SEQ_LENS
            ] = np.array([1.0]).reshape(1, 1, -1)

        # only allowed one state for now
        state_in = policy._dummy_batch["state_in_0"].to("cpu")

        input_names = []
        output_names = []
        inputs = []
        if isinstance(policy.observation_space, gym.spaces.Dict):
            for obs_space_name, obs_space in policy.observation_space.spaces.items():
                input_names.append(obs_space_name)
                # Just flatten discrete and boolean spaces
                if not isinstance(obs_space, gym.spaces.Box):
                    obs_space = gym.spaces.utils.flatten_space(obs_space)
                inputs.append(th.rand(1, *obs_space.shape))
        else:
            obs_space = policy.observation_space
            if not isinstance(obs_space, gym.spaces.Box):
                obs_space = gym.spaces.utils.flatten_space(obs_space)
            input_names.append("obs")
            inputs.append(th.rand(1, *obs_space.shape))

        # Handle both Dict and non-Dict action spaces
        if isinstance(policy.action_space, gym.spaces.Dict):
            for action_space_name, action_space in policy.action_space.items():
                output_names.append(action_space_name)
        else:
            # For non-Dict action spaces, use a default name
            output_names.append("action")

        inputs.append(state_in)
        input_names.append("state_in")
        output_names.append("state_out")
        # Note that the seq_lens gets dropped from the exported model
        
        # Pass the path as a string (not a file object) for compatibility with newer PyTorch ONNX exporters
        # Use dynamo=False for legacy exporter which properly handles input_names parameter
        th.onnx.export(
            self,
            tuple(inputs),
            str(export_path),
            export_params=True,
            opset_version=onnx_opset,
            input_names=input_names,
            output_names=output_names,
            dynamic_axes={k: {0: "batch_size"} for k in input_names},
            dynamo=False,
        )

# end of adapted code

