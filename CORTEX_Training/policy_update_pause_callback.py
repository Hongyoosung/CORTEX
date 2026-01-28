"""
Policy Update Pause Callback for CORTEX v9.0.1 (RLlib 2.x+ API)

Pauses environments during policy updates to prevent episode desync.

Key Insight:
    - RLlib alternates between sample collection (env.step()) and policy updates (gradient descent)
    - During policy updates, the main thread is busy with training, but async gRPC threads
      continue polling UE5, causing episodes to advance
    - This creates partial episodes and training data corruption

Solution:
    - Before policy update: pause() all environments → gRPC threads stop sending actions
    - After policy update: resume() all environments → gRPC threads resume action sending
    - Result: Clean episode boundaries, no desync, stable training

Usage:
    from policy_update_pause_callback import PolicyUpdatePauseCallback

    config = PPOConfig()
        .environment(...)
        .callbacks(PolicyUpdatePauseCallback)
        .build()

API Reference:
    - Uses RLlib 2.x+ API: EnvRunner, env_runner_group
    - Compatible with: Ray 2.0+, RLlib 2.0+
"""

from ray.rllib.algorithms.callbacks import DefaultCallbacks
from ray.rllib.algorithms.algorithm import Algorithm
from typing import Dict, Optional


class PolicyUpdatePauseCallback(DefaultCallbacks):
    """
    Pauses environments during policy updates to prevent episode desync.

    Hooks into RLlib's training lifecycle:
        1. on_sample_end() - Called AFTER sample collection → pause()
        2. (RLlib performs gradient descent - envs are paused)
        3. on_train_result() - Called AFTER training → resume()
    """

    def on_sample_end(self, *, env_runner, **kwargs):
        if hasattr(env_runner, 'env'):
            env_runner.env.pause()  
    
    def on_train_result(self, *, algorithm, **kwargs):
        algorithm.env_runner_group.foreach_worker(
            lambda w: w.env.resume() if hasattr(w, 'env') else None,
            local_env_runner=True
        )

    @staticmethod
    def _safe_pause(env):
        """
        Safely pause environment if it supports pause().

        Args:
            env: Gym environment instance
        """
        try:
            if hasattr(env, 'pause') and callable(env.pause):
                print(f"[PolicyUpdatePauseCallback] Calling env.pause() on {type(env).__name__}")
                env.pause()
                print(f"[PolicyUpdatePauseCallback] env.pause() completed successfully")
            else:
                print(f"[PolicyUpdatePauseCallback] ⚠️  env.pause() not available on {type(env).__name__}")
                print(f"[PolicyUpdatePauseCallback]    Available methods: {[m for m in dir(env) if not m.startswith('_') and callable(getattr(env, m))][:10]}")
        except Exception as e:
            print(f"[PolicyUpdatePauseCallback] ❌ Error calling env.pause(): {e}")
            import traceback
            traceback.print_exc()

    @staticmethod
    def _safe_resume(env):
        """
        Safely resume environment if it supports resume().

        Args:
            env: Gym environment instance
        """
        try:
            if hasattr(env, 'resume') and callable(env.resume):
                print(f"[PolicyUpdatePauseCallback] Calling env.resume() on {type(env).__name__}")
                env.resume()
                print(f"[PolicyUpdatePauseCallback] env.resume() completed successfully")
            else:
                print(f"[PolicyUpdatePauseCallback] ⚠️  env.resume() not available on {type(env).__name__}")
        except Exception as e:
            print(f"[PolicyUpdatePauseCallback] ❌ Error calling env.resume(): {e}")
            import traceback
            traceback.print_exc()
