# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
"""
RLlib Environment Runner for Schola/Unreal Engine. Use this class to use the RayVecEnv
"""

import logging
from functools import partial
import gymnasium as gym
from ray.rllib.callbacks.utils import make_callback
from ray.rllib.env.env_context import EnvContext
from ray.rllib.env.env_runner import EnvRunner
from ray.rllib.utils.annotations import override
from ray.rllib.env.multi_agent_env_runner import MultiAgentEnvRunner
from ray.tune.registry import ENV_CREATOR, _global_registry

from schola.core.protocols.base import BaseRLProtocol
from schola.core.simulators.base import BaseSimulator
from schola.rllib.env import RayVecEnv

logger = logging.getLogger("ray.rllib")


class ScholaEnvRunner(MultiAgentEnvRunner):

    @override(EnvRunner)
    def make_env(self):
        # If an env already exists, try closing it first (to allow it to properly
        # cleanup).
        if self.env is not None:
            try:
                self.env.close()
            except Exception as e:
                logger.warning(
                    "Tried closing the existing env (multi-agent), but failed with "
                    f"error: {e.args[0]}"
                )
            del self.env

        env_ctx = self.config.env_config
        if not isinstance(env_ctx, EnvContext):
            env_ctx = EnvContext(
                env_ctx,
                worker_index=self.worker_index,
                num_workers=self.config.num_env_runners,
                remote=self.config.remote_worker_envs,
            )

        assert "protocol" in env_ctx, "Protocol must be provided in the env_config"
        assert "simulator" in env_ctx, "Simulator must be provided in the env_config"
        assert issubclass(env_ctx["protocol"], BaseRLProtocol), "Protocol must be a BaseRLProtocol"
        assert issubclass(env_ctx["simulator"], BaseSimulator), "Simulator must be a BaseSimulator"
        
        # Create the environment
        self.env = RayVecEnv(env_ctx["protocol"](**env_ctx["protocol_args"]), env_ctx["simulator"](**env_ctx["simulator_args"]))
        
        self.num_envs: int = self.env.num_envs
        if self.num_envs != self.config.num_envs_per_env_runner:
            logger.warning(f"Ignoring 'num_envs_per_env_runner' setting because the number of environments ({self.num_envs}) does not match the number of environments per env runner ({self.config.num_envs_per_env_runner})")
            self.config.num_envs_per_env_runner = self.num_envs

        if not self.config.disable_env_checking:
            logger.warning("Environment checking setting is ignored when using the ScholaEnvRunner")

        # Set the flag to reset all envs upon the next `sample()` call.
        self._needs_initial_reset = True

        # Call the `on_environment_created` callback.
        make_callback(
            "on_environment_created",
            callbacks_objects=self._callbacks,
            callbacks_functions=self.config.callbacks_on_environment_created,
            kwargs=dict(
                env_runner=self,
                metrics_logger=self.metrics,
                env=self.env.unwrapped,
                env_context=env_ctx,
            ),
        )