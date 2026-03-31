# Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

"""
Script to train an rllib model using Schola.
"""
import logging

from pathlib import Path
from typing import Annotated, Literal, Union
import traceback


from schola.scripts.common import (
    UnrealEditorSimulatorArgs,
    UnrealExecutableCommandLinePlaceholder,
    UnrealExecutableSimulatorArgs,
    get_activation_function
)

from schola.scripts.rllib.settings import (
    RLlibScriptArgs,
    PPOSettings,
    SACSettings,
    IMPALASettings,
)

from cyclopts import App, Parameter, Group

# Logging setup
if not logging.getLogger().handlers:
    logging.basicConfig(
        level=logging.INFO,
        format="%(levelname)s %(name)s: %(message)s",
    )
logger = logging.getLogger(__name__)


app = App(name="train", help="Train a Model using ray")

@app.command(help="Train a model using the Soft Actor Critic Algorithm")
def sac(args: Annotated[RLlibScriptArgs, Parameter(name="*")] = RLlibScriptArgs(),
        simulator_type: Annotated[Literal["editor", "executable"], Parameter(group="Simulator (Choose One)")] = "editor",
        executable: Annotated[UnrealExecutableSimulatorArgs, Parameter(name="*", group="Executable Parameters")] = UnrealExecutableCommandLinePlaceholder(),
        sac: Annotated[SACSettings, Parameter(name="*", group="SAC Parameters")] = SACSettings(),
        editor: Annotated[UnrealEditorSimulatorArgs, Parameter(show=False)] = UnrealEditorSimulatorArgs()):
    args.environment_settings.simulator = editor if simulator_type == "editor" else executable
    args.algorithm_settings = sac
    logger.info("Arguments: %s", args)
    main(args)

@app.command(help="Train a model using Proximal Policy Optimization")
def ppo(args: Annotated[RLlibScriptArgs, Parameter(name="*")] = RLlibScriptArgs(),
        simulator_type: Annotated[Literal["editor", "executable"], Parameter(group="Simulator (Choose One)")] = "editor",
        executable: Annotated[UnrealExecutableSimulatorArgs, Parameter(name="*", group="Executable Parameters")] = UnrealExecutableCommandLinePlaceholder(),
        ppo: Annotated[PPOSettings, Parameter(name="*", group="PPO Parameters")] = PPOSettings(),
        editor: Annotated[UnrealEditorSimulatorArgs, Parameter(show=False)] = UnrealEditorSimulatorArgs()):
    args.environment_settings.simulator = editor if simulator_type == "editor" else executable
    args.algorithm_settings = ppo
    logger.info("Arguments: %s", args)
    main(args)

@app.command(help="Train a model using the Impala algorithm")
def impala(args: Annotated[RLlibScriptArgs, Parameter(name="*")] = RLlibScriptArgs(),
            simulator_type: Annotated[Literal["editor", "executable"], Parameter(group="Simulator (Choose One)")] = "editor",
            executable: Annotated[UnrealExecutableSimulatorArgs, Parameter(name="*", group="Executable Parameters")] = UnrealExecutableCommandLinePlaceholder(),
            impala: Annotated[IMPALASettings, Parameter(name="*", group="IMPALA Parameters")] = IMPALASettings(),
            editor: Annotated[UnrealEditorSimulatorArgs, Parameter(show=False)] = UnrealEditorSimulatorArgs()):
    args.environment_settings.simulator = editor if simulator_type == "editor" else executable
    args.algorithm_settings = impala
    logger.info("Arguments: %s", args)
    main(args)

#forward declare here for type hinting with no load
def main(args: RLlibScriptArgs) -> 'ray.tune.ExperimentAnalysis':
    """
    Main function for launching training with ray.

    Parameters
    ----------
    args : RLlibArgs
        The arguments for the script as a dataclass

    Returns
    -------
    tune.ExperimentAnalysis
        The results of the training
    """
    # Import ray and rllib dependencies lazily when the command is actually executed
    import ray
    from ray import air, tune
    from ray.rllib.algorithms.algorithm import Algorithm
    from ray.rllib.policy.policy import PolicySpec
    from ray.rllib.algorithms.ppo import PPOConfig
    from ray.rllib.algorithms.sac.sac import SACConfig
    from ray.rllib.algorithms.appo.appo import APPOConfig
    from ray.rllib.algorithms.impala.impala import IMPALAConfig
    from ray.tune.registry import register_env
    from schola.rllib.utils import export_onnx_from_policy
    from ray.rllib.policy.policy import Policy
    from ray.rllib.core.rl_module.rl_module import RLModuleSpec
    from ray.rllib.connectors.env_to_module import FlattenObservations
    from schola.rllib.env import RayVecEnv
    from schola.rllib.env_runner import ScholaEnvRunner
    from schola.core.protocols.protobuf.gRPC import gRPCProtocol
    from schola.core.simulators.unreal.editor import UnrealEditor
    # Determine num_env_runners: when num_cpus=1 -> num_env_runners=0 (local runner)
    num_env_runners = max(0, args.resource_settings.num_cpus - 1)
    
    # Collect agent display names using a temporary environment instance
    tmp_env = RayVecEnv(
        args.environment_settings.protocol.make(), args.environment_settings.simulator.make(), verbosity=args.logging_settings.schola_verbosity
    )
    # Note: We no longer wrap with DictToBoxWrapper - flattening is handled by ConnectorV2
    try:
        # Get agent IDs from the first environment
        agent_names = {agent_id: agent_id for agent_id in tmp_env.possible_agents}
    finally:
        tmp_env.close_extras()

    # Clusters configure resources automatically
    if args.resource_settings.using_cluster:
        ray.init()
    else:
        ray.init(
            num_cpus=args.resource_settings.num_cpus,
            num_gpus=args.resource_settings.num_gpus,
        )

    # Get the number of sub-environments from the temporary env we created earlier
    num_sub_envs = tmp_env.num_envs
    
    # Get activation function for model config
    activation_fn = get_activation_function(args.network_architecture_settings.activation)

    # Use NEW API stack with RayEnv/RayVecEnv (new stack interface)
    # Auto-assignment: RayEnv for local runner (num_env_runners=0), RayVecEnv for remote runners
    config: Union[PPOConfig, SACConfig, APPOConfig, IMPALAConfig] = (
        args.algorithm_settings.rllib_config()
        .api_stack(
            enable_rl_module_and_learner=True,  # Enable new stack
            enable_env_runner_and_connector_v2=True,  # Enable EnvRunner
        )
        .environment(
            env_config={
                "protocol": gRPCProtocol,
                "protocol_args": {"url": args.environment_settings.protocol.url, "port": args.environment_settings.protocol.port},
                "simulator": UnrealEditor,
                "simulator_args": {},
                },
        )
        .framework("torch")
        .env_runners(
            env_runner_cls=ScholaEnvRunner,
            num_env_runners=0,
            # Use ConnectorV2 to flatten Dict observations to Box for neural network processing
            # Signature: def connector_fn(env, spaces=None, device=None) -> ConnectorV2
            env_to_module_connector=lambda env: FlattenObservations(
                input_observation_space=env.single_observation_space,
                input_action_space=env.single_action_space,
                multi_agent=True,
            ),
        )
        .multi_agent(
            policies={agent_id: PolicySpec() for agent_id in agent_names.keys()},
            policy_mapping_fn=lambda agent_id, *args, **kwargs: agent_id,
        )
        .resources(
            num_gpus=args.resource_settings.num_gpus,
        )
        .learners(
            # When num_cpus=1, use 0 learners (local learning on main process)
            # This avoids resource conflicts with env_runner
            num_learners=args.resource_settings.num_learners if args.resource_settings.num_learners > 0 or args.resource_settings.num_cpus > 1 else 0,
            num_gpus_per_learner=args.resource_settings.num_gpus_per_learner,
            num_cpus_per_learner=args.resource_settings.num_cpus_per_learner,
        )
        .rl_module(
            rl_module_spec=RLModuleSpec(
                # Using default RLlib catalog - ConnectorV2 handles Dict->Box flattening
                model_config={
                    "fcnet_hiddens": args.network_architecture_settings.fcnet_hiddens,
                    "fcnet_activation": activation_fn,
                    "use_attention": args.network_architecture_settings.use_attention,
                    "attention_dim": args.network_architecture_settings.attention_dim,
                },
            ),
        )
        .training(
            lr=args.training_settings.learning_rate,
            gamma=args.training_settings.gamma,
            num_sgd_iter=args.training_settings.num_sgd_iter,
            train_batch_size=args.training_settings.train_batch_size_per_learner,
            minibatch_size=args.training_settings.minibatch_size,
            **args.algorithm_settings.get_settings_dict()
        )
    )

    # Use the new API stack metric name for stopping criterion
    # Old stack used "timesteps_total", new stack uses "num_env_steps_sampled_lifetime"
    stop = {
        "num_env_steps_sampled_lifetime": args.training_settings.timesteps,
    }

    callbacks = []

    logger.info("Starting training")
    try:
        results = tune.run(
            args.algorithm_settings.name,
            config=config,
            stop=stop,
            checkpoint_config=air.CheckpointConfig(
                checkpoint_frequency=args.checkpoint_settings.save_freq if args.checkpoint_settings.enable_checkpoints else 0,
                checkpoint_at_end=args.checkpoint_settings.save_final_policy,
            ),
            restore=args.resume_settings.resume_from,
            verbose=args.logging_settings.rllib_verbosity,
            storage_path=str(args.checkpoint_settings.checkpoint_dir.resolve()),
            callbacks=callbacks,
        )
        last_checkpoint = results.get_last_checkpoint()
        logger.info("Training complete")
    finally:
        # Always shutdown ray and release the environment from training even if there is an error
        # will reraise the error unless a control flow statement is added
        ray.shutdown()

    if args.checkpoint_settings.export_onnx:
        export_onnx_from_policy(
            Algorithm.from_checkpoint(last_checkpoint), Path(results.trials[-1].path)
        )
        logger.info("Models exported to ONNX at %s", results.trials[-1].path)
    return results


def debug_main_from_cli() -> None:
    """
    Main function for launching training with ray from the command line, that catches any errors and waits for user input to close.

    See Also
    --------
    main_from_cli : The main function for launching training with ray from the command line
    main : The main function for launching training with ray
    """
    try:
        main()
    except Exception as e:
        traceback.print_exc()
    finally:
        input("Press any key to close:")

if __name__ == "__main__":
    app()
