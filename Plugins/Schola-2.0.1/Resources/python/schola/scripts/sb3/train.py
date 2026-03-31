# Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

"""
Script to train a Stable Baselines3 model using Schola.
"""
from dataclasses import asdict
import os
import logging
from typing import Literal, Optional, Tuple
from typing_extensions import Annotated

from schola.scripts.common import (
        UnrealEditorSimulatorArgs,
        UnrealExecutableSimulatorArgs,
        UnrealExecutableCommandLinePlaceholder,
        get_activation_function
    )
from schola.scripts.sb3.settings import PPOSettings, SACSettings, SB3ScriptArgs
from cyclopts import App, Parameter, validators, Group
from schola.scripts.panel import print_error
 

# Logging setup (idempotent)
if not logging.getLogger().handlers:
    logging.basicConfig(
        level=logging.INFO,
        format="%(levelname)s %(name)s: %(message)s",
    )

logger = logging.getLogger(__name__)


def warn_if_small_image_observation(observation_space, threshold: int = 64):
    """Issue a panel warning if any Box observation that looks image-like has
    a spatial dimension smaller than `threshold`.

    Heuristic: Treat Box spaces with shape len 2 (H, W) or len 3 (C,H,W) / (H,W,C)
    as image-like. For 3D, pick the two largest dims as spatial. If min spatial < threshold
    we warn once.
    """
    import gymnasium as gym

    def _iter_box_spaces(space):
        if isinstance(space, gym.spaces.Box):
            yield space
        elif isinstance(space, gym.spaces.Dict):
            for s in space.spaces.values():
                yield from _iter_box_spaces(s)
        elif isinstance(space, gym.spaces.Tuple):
            for s in space.spaces:
                yield from _iter_box_spaces(s)

    for box in _iter_box_spaces(observation_space):
        shape = box.shape
        if len(shape) == 2:
            h, w = shape
        elif len(shape) == 3:
            # Two largest dims are spatial (robust to (C,H,W) vs (H,W,C)).
            h, w = sorted(shape, reverse=True)[:2]
        else:
            continue

        if min(h, w) < threshold:
            print_error(
                f"Image observation detected with shape {shape}; min dimension < {threshold}. "
                "The default SB3 CNN may fail or produce poor features. Consider resizing "
                "or providing a custom features_extractor."
            )
            break


def main(args: SB3ScriptArgs) -> Optional[Tuple[float, float]]:
    """
    Main function for training a Stable Baselines3 model using Schola.

    Parameters
    ----------
    args : SB3ScriptArgs
        The arguments for the script.

    Returns
    -------
    Optional[Tuple[float,float]]
        The mean and standard deviation of the rewards if evaluation is enabled, otherwise None.
    """

    if args.pbar:
        try:
            import tqdm
        except Exception:
            logger.warning("tqdm not installed. disabling PBar")
            args.pbar = False

    if args.pbar:
        try:
            import rich
        except Exception:
            logger.warning("rich not installed. disabling PBar")
            args.pbar = False

    if args.logging_settings.enable_tensorboard:
        try:
            import tensorboard
        except Exception:
            logger.warning("tensorboard not installed. Disabling tensorboard logging")
            args.logging_settings.enable_tensorboard = False
    
    from schola.sb3.utils import convert_ckpt_to_onnx_for_unreal
    import gymnasium as gym
    from stable_baselines3.common.evaluation import evaluate_policy
    from stable_baselines3.common.vec_env import VecNormalize

    from schola.scripts.sb3.utils import RewardCallback, CustomProgressBarCallback
    from schola.sb3.env import VecEnv
    from schola.sb3.utils import VecMergeDictActionWrapper
    from schola.core.error_manager import ScholaErrorContextManager
    from stable_baselines3.common.callbacks import CheckpointCallback
    from stable_baselines3.common.vec_env.vec_monitor import VecMonitor
    
    from stable_baselines3.common import utils
    
    
    # initialize so we can force closure at the end
    env = None
    try:
        # This context manager redirects GRPC errors into custom error types to help debug
        with ScholaErrorContextManager() as err_ctxt:

            # make a gym environment
            env = VecEnv(args.environment_settings.simulator.make(),args.environment_settings.protocol.make(), verbosity=args.logging_settings.schola_verbosity)

            
            if isinstance(env.action_space, gym.spaces.Dict):
                logging.warning("SB3 doesn't support dictionary action spaces. Attempting to merge into a single non-composite action space (e.g. Box, MultiDiscrete). This will cause issues with the ONNX Export.")
                env = VecMergeDictActionWrapper(env)

            model_loaded = False
            if args.resume_settings.resume_from:
                try:
                    model = args.algorithm_settings.constructor.load(
                        args.resume_settings.resume_from, env=env
                    )
                    model_loaded = True
                except Exception as e:
                    logger.warning("Error loading model '%s': %s. Training from scratch", args.resume_from, e)

            if not model_loaded:
                policy_kwargs = None
                if (
                    args.network_architecture_settings.activation
                    or args.network_architecture_settings.critic_parameters
                    or args.network_architecture_settings.policy_parameters
                ):
                    if isinstance(env.observation_space, gym.spaces.Dict):
                        policy_kwargs = dict(
                            features_extractor_kwargs={"normalized_image": True}
                        )
                    else:
                        policy_kwargs = dict()

                    if args.network_architecture_settings.activation:
                        policy_kwargs["activation_fn"] = get_activation_function(args.network_architecture_settings.activation)

                    if args.network_architecture_settings.critic_parameters or args.network_architecture_settings.policy_parameters:
                        # default to nothing
                        policy_kwargs["net_arch"] = dict(vf=[], pi=[], qf=[])

                    if args.network_architecture_settings.critic_parameters:
                        policy_kwargs["net_arch"][
                            args.algorithm_settings.critic_type
                        ] = args.network_architecture_settings.critic_parameters

                    if args.network_architecture_settings.policy_parameters:
                        policy_kwargs["net_arch"]["pi"] = args.network_architecture_settings.policy_parameters

                model = args.algorithm_settings.constructor(
                    policy=(
                        "MultiInputPolicy"
                        if isinstance(env.observation_space, gym.spaces.Dict)
                        else "MlpPolicy"
                    ),
                    env=env,
                    verbose=args.logging_settings.sb3_verbosity,
                    policy_kwargs=policy_kwargs,
                    **asdict(args.algorithm_settings),
                )

            if args.resume_settings.load_vecnormalize:
                if model.get_vec_normalize_env() is None:
                    try:
                        VecNormalize.load(args.resume_settings.load_vecnormalize, env)
                    except Exception:
                        logger.warning("Error loading saved VecNormalize parameters. Skipping.")
                else:
                    logger.warning("resume_settings.load_vecnormalize was true but no VecNormalize wrapper exists to load to. Skipping.")

            if args.resume_settings.load_replay_buffer:
                if hasattr(model, "replay_buffer"):
                    try:
                        model.load_replay_buffer(args.resume_settings.load_replay_buffer)
                    except Exception:
                        logger.warning("Error loading saved Replay Buffer. Skipping.")
                else:
                    logger.warning("resume_settings.load_replay_buffer was true but Model does not have a Replay Buffer to load to. Skipping.")

            callbacks = []

            # grab all loggers that we can find installed in the pc,
            output_formats = []

            # This is a bit of a hack, since output_formats doesn't have a getter/setter but it this is totally safe otherwise
            logger = utils.configure_logger(
                args.logging_settings.sb3_verbosity,
                args.logging_settings.log_dir if args.logging_settings.enable_tensorboard else None,
                args.algorithm_settings.name,
                args.resume_settings.reset_timestep,
            )
            logger.output_formats += output_formats
            model.set_logger(logger)

            if args.logging_settings.enable_tensorboard:
                reward_callback = RewardCallback(
                    verbose=args.logging_settings.callback_verbosity,
                    frequency=args.logging_settings.log_freq,
                    num_envs=env.num_envs,
                )
                callbacks.append(reward_callback)

            if args.checkpoint_settings.enable_checkpoints:
                ckpt_callback = CheckpointCallback(
                    save_freq=args.checkpoint_settings.save_freq,
                    save_path=args.checkpoint_settings.checkpoint_dir,
                    name_prefix=args.name_prefix,
                    save_replay_buffer=args.checkpoint_settings.save_replay_buffer,
                    save_vecnormalize=args.checkpoint_settings.save_vecnormalize,
                )
                callbacks.append(ckpt_callback)

            if args.pbar:
                pbar_callback = CustomProgressBarCallback()
                callbacks.append(pbar_callback)

            model.learn(
                total_timesteps=args.timesteps,
                callback=callbacks,
                reset_num_timesteps=args.resume_settings.reset_timestep,
                log_interval=args.logging_settings.log_freq,
            )

            if args.checkpoint_settings.save_final_policy:
                logging.info("... Saving final policy checkpoint")
                model.save(
                    os.path.join(args.checkpoint_settings.checkpoint_dir, f"{args.name_prefix}_final.zip")
                )

                if args.checkpoint_settings.save_vecnormalize and model.get_vec_normalize_env() is not None:
                    model.get_vec_normalize_env().save(
                        os.path.join(
                            args.checkpoint_settings.checkpoint_dir,
                            f"{args.name_prefix}_vec_normalize_final.zip",
                        )
                    )

                if args.checkpoint_settings.export_onnx:
                    logging.info("... Exporting final policy to ONNX")
                    convert_ckpt_to_onnx_for_unreal(
                        model,
                        f"{args.checkpoint_settings.checkpoint_dir}/{args.name_prefix}_final.zip",
                        f"{args.checkpoint_settings.checkpoint_dir}/{args.name_prefix}_final.onnx",
                    )

            if not args.disable_eval:
                logging.info("... Evaluating the model")
                env_with_monitor = VecMonitor(env)
                mean_reward, std_reward = evaluate_policy(
                    model, env_with_monitor, n_eval_episodes=10, deterministic=True
                )
                
                logging.info("Evaluation complete: mean_reward={:.2f} +/- {:.2f}".format(mean_reward, std_reward))
                env.close()
                return mean_reward, std_reward
            else:
                logging.info("Evaluation disabled. Skipping.")
                env.close()
    except Exception as e:
        if env:
            env.close()
        raise e


app = App(name="train", help="Train a model using StableBaselines3")

@app.command(help="Train a model using the Soft Actor Critic Algorithm")
def sac(args: Annotated[SB3ScriptArgs, Parameter(name="*")] = SB3ScriptArgs(),
         simulator_type: Annotated[Literal["editor", "executable"], Parameter(group="Simulator (Choose One)")] = "editor",
         executable: Annotated[UnrealExecutableSimulatorArgs, Parameter(name="*", group="Executable Parameters")] = UnrealExecutableCommandLinePlaceholder(),
         sac: Annotated[SACSettings, Parameter(name="*", group="SAC Parameters")] = SACSettings(),
         editor: Annotated[UnrealEditorSimulatorArgs, Parameter(show=False)] = UnrealEditorSimulatorArgs()):
    args.environment_settings.simulator = editor if simulator_type == "editor" else executable
    args.algorithm_settings = sac
    logger.info("%s", args)
    main(args)

@app.command(help="Train a model using Proximal Policy Optimization")
def ppo( args: Annotated[SB3ScriptArgs, Parameter(name="*")] = SB3ScriptArgs(),
         simulator_type: Annotated[Literal["editor", "executable"], Parameter(group="Simulator (Choose One)")] = "editor",
         executable: Annotated[UnrealExecutableSimulatorArgs, Parameter(name="*", group="Executable Arguments")] = UnrealExecutableCommandLinePlaceholder(),
         ppo: Annotated[PPOSettings, Parameter(name="*", group="PPO Parameters")] = PPOSettings(),
        editor: Annotated[UnrealEditorSimulatorArgs, Parameter(show=False)] = UnrealEditorSimulatorArgs()):
    args.environment_settings.simulator = editor if simulator_type == "editor" else executable
    args.algorithm_settings = ppo
    logger.info("Arguments: %s", args)
    main(args)

