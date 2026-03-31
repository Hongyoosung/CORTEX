# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
"""
Utilities for working with gym environments
"""
from gymnasium.spaces import Dict
from gymnasium import Env, Wrapper


class PopActionWrapper(Wrapper):
    """
    A wrapper that pops the action from the environment's action space.
    This is useful for environments where the action space is a dictionary
    and we want to use only one of the actions.
    """

    def __init__(self, env: Env):
        super().__init__(env)
        assert isinstance(
            env.action_space, Dict
        ), "Action space must be a Dictionary Space."
        # Pop the first action from the action space
        self.key, self.action_space = list(env.action_space.spaces.items())[0]

    def step(self, action):
        """
        Step the environment with the given action.
        """
        return self.env.step({self.key: action})
