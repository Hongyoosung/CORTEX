# Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.
"""
This module contains the settings dataclasses for the RLlib script
"""
from typing import Annotated, Any, Dict, List, Optional, Type, Union
from pathlib import Path
from dataclasses import dataclass, field

from schola.scripts.common import (
    ActivationFunctionEnum,
    CheckpointArgs,
    EnvironmentArgs,
    RLLibLauncherExtension,
)

from cyclopts import App, Parameter, validators


class RLLibAlgorithmSpecificSettings:
    """
    Base Class for RLLib algorithm specific settings. This class is intended to be inherited by specific algorithm settings classes (e.g., PPOSettings, IMPALASettings, etc.).
    """

    def get_settings_dict(self) -> Dict[str, Any]:
        """
        Get the settings as a dictionary keyed by the correct parameter name in Ray
        """
        ...

    @classmethod
    def get_parser(cls):
        """
        Add the settings to the parser or subparser
        """
        ...


@dataclass
class PPOSettings(RLLibAlgorithmSpecificSettings):
    """
    Dataclass for PPO (Proximal Policy Optimization) algorithm specific settings. This class defines the parameters used in the PPO algorithm, including GAE lambda, clip parameter, and whether to use GAE.
    """

    gae_lambda: Annotated[float, Parameter(validator=validators.Number(gte=0.0, lte=1.0))] = 0.95
    "The lambda parameter for Generalized Advantage Estimation (GAE). This controls the trade-off between bias and variance in the advantage estimation."

    clip_param: Annotated[float, Parameter(validator=validators.Number(gte=0.0))] = 0.2
    "The clip parameter for the PPO algorithm. This is the epsilon value used in the clipped surrogate objective function. It helps to limit the policy update step size to prevent large changes that could lead to performance collapse."

    use_gae: bool = True
    "Whether to use Generalized Advantage Estimation (GAE) for advantage calculation. GAE is a method to reduce the variance of the advantage estimates while keeping bias low. If set to False, the standard advantage calculation will be used instead."

    @property
    def rllib_config(self) -> Type["PPOConfig"]: # type: ignore
        from ray.rllib.algorithms.ppo.ppo import PPOConfig
        return PPOConfig

    @property
    def name(self) -> str:
        return "PPO"

    def get_settings_dict(self):
        return {
            "lambda_": self.gae_lambda,
            "use_gae": self.use_gae,
            "clip_param": self.clip_param,
        }


@dataclass
class SACSettings(RLLibAlgorithmSpecificSettings):
    """
    Dataclass for SAC (Soft Actor-Critic) algorithm specific settings. This class defines the parameters used in the SAC algorithm, including soft target network updates and entropy regularization.
    """

    tau: Annotated[float, Parameter(validator=validators.Number(gte=0.0, lte=1.0))] = 0.005
    "Soft update coefficient for target networks. Controls how quickly target networks track the main networks. Lower values (e.g., 0.005) mean slower updates, which can improve stability."

    target_entropy: str = "auto"
    "Target entropy for automatic temperature tuning. Set to 'auto' to automatically calculate based on action space dimensionality, or provide a float value for manual control."

    initial_alpha: Annotated[float, Parameter(validator=validators.Number(gt=0.0))] = 1.0
    "Initial temperature/alpha value for entropy regularization. Higher values encourage more exploration."

    n_step: Annotated[int, Parameter(validator=validators.Number(gte=1))] = 1
    "Number of steps for n-step returns. Using n>1 can help with credit assignment in sparse reward environments."

    twin_q: bool = True
    "Whether to use twin Q networks (double Q-learning). This helps reduce overestimation bias in Q-value estimates."

    @property
    def rllib_config(self) -> Type["SACConfig"]: # type: ignore
        from ray.rllib.algorithms.sac.sac import SACConfig
        return SACConfig

    @property
    def name(self) -> str:
        return "SAC"

    def get_settings_dict(self):
        return {
            "tau": self.tau,
            "target_entropy": self.target_entropy,
            "initial_alpha": self.initial_alpha,
            "n_step": self.n_step,
            "twin_q": self.twin_q,
        }


@dataclass
class IMPALASettings(RLLibAlgorithmSpecificSettings):
    """
    Dataclass for IMPALA (Importance Weighted Actor-Learner Architecture) algorithm specific settings. This class defines the parameters used in the IMPALA algorithm, including V-trace settings for off-policy correction.
    """

    vtrace: bool = True
    "Whether to use the V-trace algorithm for off-policy correction in the IMPALA algorithm. V-trace is a method to correct the bias introduced by using off-policy data for training. It helps to ensure that the value estimates are more accurate and stable."

    vtrace_clip_rho_threshold: Annotated[float, Parameter(validator=validators.Number(gte=0.0))] = 1.0
    "The clip threshold for V-trace rho values."

    vtrace_clip_pg_rho_threshold: Annotated[float, Parameter(validator=validators.Number(gte=0.0))] = 1.0
    "The clip threshold for V-trace rho values in the policy gradient."

    @property
    def rllib_config(self) -> Type["IMPALAConfig"]: # type: ignore
        from ray.rllib.algorithms.impala.impala import IMPALAConfig
        return IMPALAConfig

    @property
    def name(self) -> str:
        return "IMPALA"

    def get_settings_dict(self):
        return {
            "vtrace": self.vtrace,
            "vtrace_clip_rho_threshold": self.vtrace_clip_rho_threshold,
            "vtrace_clip_pg_rho_threshold": self.vtrace_clip_pg_rho_threshold,
        }


@dataclass
class APPOSettings(IMPALASettings, PPOSettings):
    """
    Dataclass for APPO (Asynchronous Proximal Policy Optimization) algorithm specific settings. This class inherits from both IMPALASettings and PPOSettings to combine the settings for both algorithms. This allows for the use of both V-trace for off-policy correction and PPO for policy optimization in a single algorithm.
    """

    @property
    def rllib_config(self) -> Type["APPOConfig"]: # type: ignore
        from ray.rllib.algorithms.appo.appo import APPOConfig
        return APPOConfig

    @property
    def name(self) -> str:
        return "APPO"

    def get_settings_dict(self):
        return {**IMPALASettings.get_settings_dict(self), **PPOSettings.get_settings_dict(self)}

@dataclass
class TrainingSettings:
    """
    Dataclass for generic training settings used in the RLlib training process. This class defines the parameters for training, including the number of timesteps, learning rate, minibatch size, and other hyperparameters that control the training process. These settings are applicable to any RLlib algorithm and can be customized based on the specific requirements of the training job.
    """

    timesteps: Annotated[int, Parameter(validator=validators.Number(gte=1))] = 3000
    "The number of timesteps to train for. This is the total number of timesteps to run during training."

    learning_rate: Annotated[float, Parameter(validator=validators.Number(gt=0))] = 0.0003
    "The learning rate for any chosen algorithm. This controls how much to adjust the model weights in response to the estimated error each time the model weights are updated. A smaller value means slower learning, while a larger value means faster learning."

    minibatch_size: Annotated[int, Parameter(validator=validators.Number(gte=1))] = 128
    "The size of the minibatch for training. This is the number of samples used in each iteration of training to update the model weights. A larger batch size can lead to more stable estimates of the gradient, but requires more memory and can slow down training if too large."

    train_batch_size_per_learner: Annotated[int, Parameter(validator=validators.Number(gte=1))] = 256
    "The number of samples given to each learner during training. Must be divisible by minibatch_size."

    num_sgd_iter: Annotated[int, Parameter(validator=validators.Number(gte=1))] = 5
    "The number of stochastic gradient descent (SGD) iterations for each batch. This is the number of times to update the model weights using the samples in the minibatch. More iterations can lead to better convergence, but also increases the training time."

    gamma: Annotated[float, Parameter(validator=validators.Number(gte=0.0, lte=1.0))] = 0.99
    "The discount factor for the reinforcement learning algorithm. This is used to calculate the present value of future rewards. A value of 0.99 means that future rewards are discounted by 1% for each time step into the future. This helps to balance the importance of immediate versus future rewards in the training process. A value closer to 1.0 will prioritize future rewards more heavily, while a value closer to 0 will prioritize immediate rewards."

    @property
    def name(self) -> str:
        return "Training Settings"

    def __post_init__(self):
        if self.minibatch_size > self.train_batch_size_per_learner:
            raise ValueError(
                f"minibatch_size ({self.minibatch_size}) cannot exceed train_batch_size_per_learner ({self.train_batch_size_per_learner})."
            )
        if self.train_batch_size_per_learner % self.minibatch_size != 0:
            raise ValueError(
                f"train_batch_size_per_learner ({self.train_batch_size_per_learner}) must be a multiple of minibatch_size ({self.minibatch_size})."
            )


@dataclass
class ResourceSettings:
    """
    Dataclass for resource settings used in the RLlib training process. This class defines the parameters for allocating computational resources, including the number of GPUs and CPUs to use for the training job. These settings help to control how resources are allocated for the training process, which can impact performance and training times. This is especially important when running on a cluster or distributed environment.
    """

    num_gpus: Annotated[int, Parameter(validator=validators.Number(gte=0))] = 0
    "The number of GPUs to use for the training process. This specifies how many GPUs are available for the RLlib training job. If set to 0, it will default to CPU training. This can be used to leverage GPU acceleration for faster training times if available."

    num_cpus: Annotated[int, Parameter(validator=validators.Number(gte=1))] = 1
    "The total number of CPUs to use for the training process. This specifies how many CPU cores are available for the RLlib training job. This can be used to parallelize the training process across multiple CPU cores, which can help to speed up training times."

    num_learners: Annotated[int, Parameter(validator=validators.Number(gte=0))] = 0
    "The number of learner processes to use for the training job. This specifies how many parallel learner processes will be used to train the model. Each learner will process a portion of the training data and update the model weights independently. This can help to speed up training times by leveraging multiple CPU cores or GPUs."

    num_cpus_for_main_process: Annotated[int, Parameter(validator=validators.Number(gte=1))] = 1
    "The number of CPUs to allocate for the main process. This is the number of CPU cores that will be allocated to the main process that manages the training job. This can be used to ensure that the main process has enough resources to handle the workload and manage the learner processes effectively."

    num_cpus_per_learner: Annotated[int, Parameter(validator=validators.Number(gte=1))] = 1
    "The number of CPUs to allocate for each learner process. This specifies how many CPU cores will be allocated to each individual learner process that is used for training. This can be used to ensure that each learner has enough resources to handle its workload and process the training data efficiently."

    num_gpus_per_learner: Annotated[int, Parameter(validator=validators.Number(gte=0))] = 0
    "The number of GPUs to allocate for each learner process. This specifies how many GPUs will be allocated to each individual learner process that is used for training."

    using_cluster: bool = False
    "Whether Ray is running on a predefined cluster, or if one should be created as part of the launch script."

    @property
    def name(self) -> str:
        return "Resource Settings"


@dataclass
class LoggingSettings:
    """
    Dataclass for logging settings used in the RLlib training process. This class defines the verbosity levels for logging in both the Schola environment and RLlib. These settings help to control the amount of logging information generated during the training process, which can be useful for debugging and understanding the training process. Adjusting these settings can help to balance the amount of information logged against performance and readability of the logs.
    """

    schola_verbosity: Annotated[int, Parameter(validator=validators.Number(gte=0, lte=2))] = 0 # Errors out < zero, but warns on > 3. Discuss if this behaviour is correct!
    "Verbosity level for the Schola environment. This controls the level of detail in the logs generated by the Schola environment. A higher value will produce more detailed logs, which can be useful for debugging and understanding the training process. Default is 0 (no additional logging)."

    rllib_verbosity: Annotated[int, Parameter(validator=validators.Number(gte=0, lte=3))] = 1
    "Verbosity level for RLlib. This controls the level of detail in the logs generated by RLlib. A higher value will produce more detailed logs, which can be useful for debugging and understanding the training process. Default is 1 (standard logging)."

    @property
    def name(self) -> str:
        return "Logging Settings"


@dataclass
class ResumeSettings:
    """
    Dataclass for resume settings used in the RLlib training process. This class defines the parameters for resuming training from a saved checkpoint. This allows you to continue training from a previously saved model checkpoint instead of starting from scratch. This is useful for long training jobs or if you want to experiment with different hyperparameters without losing progress.
    """

    resume_from: Annotated[Optional[Path], Parameter(validator=validators.Path(exists=True, file_okay=True, dir_okay=False))] = None
    "Path to a checkpoint to resume training from. This allows you to continue training from a previously saved model checkpoint instead of starting from scratch. This is useful for long training jobs or if you want to experiment with different hyperparameters without losing progress. If set to None, training will start from scratch."

    @property
    def name(self) -> str:
        return "Resume Settings"


@dataclass
class NetworkArchitectureSettings:
    """
    Dataclass for network architecture settings used in the RLlib training process. This class defines the parameters for the neural network architecture used for policy and value function approximation. This includes the hidden layer sizes, activation functions, and whether to use an attention mechanism. These settings help to control the complexity and capacity of the neural network model used in the training process.
    """

    fcnet_hiddens: Annotated[List[int], Parameter(consume_multiple=True)] = field(default_factory=lambda: [512, 512])
    "The hidden layer architecture for the fully connected network. This specifies the number of neurons in each hidden layer of the neural network used for the policy and value function approximation. The default is [512, 512], which means two hidden layers with 512 neurons each. This can be adjusted based on the complexity of the problem and the size of the input state space."

    activation: ActivationFunctionEnum = ActivationFunctionEnum.ReLU
    "The activation function to use for the fully connected network. This specifies the non-linear activation function applied to each neuron in the hidden layers of the neural network. The default is ReLU (Rectified Linear Unit), which is a commonly used activation function in deep learning due to its simplicity and effectiveness. Other options may include Tanh, Sigmoid, etc. This can be adjusted based on the specific requirements of the problem and the architecture of the neural network."

    use_attention: bool = False
    "Whether to use an attention mechanism in the model. This specifies whether to include an attention layer in the neural network architecture. Note, this attends does not attend over the inputs but rather the timestep dimension."

    attention_dim: Annotated[int, Parameter(validator=validators.Number(gte=1))] = 64
    "The dimension of the attention layer. This specifies the size of the output from the attention mechanism if `use_attention` is set to True. The attention dimension determines how many features will be used to represent the output of the attention layer. A larger value may allow for more complex representations but will also increase the computational cost. The default is 64, which is a common choice for many applications."

    @property
    def name(self) -> str:
        return "Network Architecture Settings"

    def __post_init__(self):
        if not self.fcnet_hiddens:
            raise ValueError("fcnet_hiddens must contain at least one positive layer size.")
        bad = [h for h in self.fcnet_hiddens if h <= 0]
        if bad:
            raise ValueError(
                f"fcnet_hiddens has non-positive entries {bad}; all layer sizes must be > 0."
            )

@dataclass
class RLlibScriptArgs:
    """
    Top level dataclass for RLlib script arguments. This class aggregates all the settings required for configuring the RLlib training process. It includes settings for training, algorithms, logging, resuming from checkpoints, network architecture, and resource allocation. This allows for a comprehensive configuration of the RLlib training job in a structured manner.
    """

    training_settings: Annotated[
        TrainingSettings, Parameter(group="Training Arguments")
    ] = field(default_factory=TrainingSettings)
    "Settings for configuring the training process."

    
    algorithm_settings: Annotated[
        Union[PPOSettings, SACSettings, APPOSettings, IMPALASettings],
        Parameter(show=False, parse=False),
    ] = field(default_factory=PPOSettings)
    "Settings for configuring the training algorithm to use."

    logging_settings: Annotated[
        LoggingSettings, Parameter(group="Logging Arguments")
    ] = field(default_factory=LoggingSettings)
    "Settings for enabling logging and configuring the logging directory."

    resume_settings: Annotated[ResumeSettings, Parameter(group="Resume Arguments")] = (
        field(default_factory=ResumeSettings)
    )
    "Settings for resuming training from a checkpoint."

    network_architecture_settings: Annotated[
        NetworkArchitectureSettings, Parameter(group="Network Architecture Arguments")
    ] = field(default_factory=NetworkArchitectureSettings)
    "Settings for configuring the neural network architecture used for training."
    
    resource_settings: Annotated[
        ResourceSettings, Parameter(group="Resource Arguments")
    ] = field(default_factory=ResourceSettings)
    "Settings for configuring the resource allocation for the training process."
    
    checkpoint_settings: Annotated[CheckpointArgs, Parameter(group="Checkpoint Arguments")] = field(default_factory=CheckpointArgs)
    "Settings for checkpoints"

    environment_settings: Annotated[EnvironmentArgs, Parameter(group="Environment Arguments",name="*")] = field(default_factory=EnvironmentArgs)
    "Settings for the environment to use during training"