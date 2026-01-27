"""
Policy Update Pause Callback for CORTEX v9.0

Pauses environments during policy updates to prevent episode desync.

Key Insight:
    - RLlib alternates between sample collection (env.step()) and policy updates (gradient descent)
    - During policy updates, the main thread is busy with training, but async gRPC threads
      continue polling UE5, causing episodes to advance
    - This creates partial episodes and training data corruption

Solution:
    - Before policy update: pause() all environments → gRPC threads stop polling
    - After policy update: resume() all environments → gRPC threads resume
    - Result: Clean episode boundaries, no desync, stable training

Usage:
    from policy_update_pause_callback import PolicyUpdatePauseCallback

    config = PPOConfig()
        .environment(...)
        .callbacks(PolicyUpdatePauseCallback)
        .build()
"""

from ray.rllib.algorithms.callbacks import DefaultCallbacks
from ray.rllib.algorithms.algorithm import Algorithm
from typing import Dict
import time


class PolicyUpdatePauseCallback(DefaultCallbacks):
    """
    Pauses environments during policy updates to prevent episode desync.

    Hooks into RLlib's training lifecycle:
        1. on_sample_end() - Called AFTER sample collection → pause()
        2. (RLlib performs gradient descent - envs are paused)
        3. on_train_result() - Called AFTER training → resume()
    """

    def on_sample_end(self, *, worker, samples, **kwargs):
        """
        Called after sample collection completes, BEFORE policy update.
        Pause environments now to prevent advancement during training.
        """
        try:
            # Pause all environments on this worker
            worker.foreach_env(self._safe_pause)
        except Exception as e:
            print(f"[PolicyUpdatePauseCallback] Warning: Failed to pause: {e}")

    def on_train_result(self, *, algorithm: Algorithm, result: Dict, **kwargs):
        """
        Called AFTER training iteration completes.
        Resume all environments to continue episode collection.
        """
        try:
            workers = algorithm.workers

            # Resume local worker envs
            if workers.local_worker():
                workers.local_worker().foreach_env(self._safe_resume)

            # Resume remote worker envs
            workers.foreach_worker(
                lambda w: w.foreach_env(self._safe_resume),
                local_worker=False
            )

        except Exception as e:
            print(f"[PolicyUpdatePauseCallback] Warning: Failed to resume: {e}")

    def _safe_pause(self, env):
        """Safely pause environment if it supports pause()."""
        if hasattr(env, 'pause') and callable(env.pause):
            env.pause()
        return env

    def _safe_resume(self, env):
        """Safely resume environment if it supports resume()."""
        if hasattr(env, 'resume') and callable(env.resume):
            env.resume()
        return env
