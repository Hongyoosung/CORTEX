# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
from minari import DataCollector
from schola.core.utils.id_manager import IdManager
from schola.core.protocols.base import BaseImitationProtocol
from schola.core.simulators.base import BaseSimulator
import gymnasium as gym

class _FakeGymEnv(gym.Env):
    
    def __init__(self, observation_space: gym.Space, action_space: gym.Space):
        self.observation_space = observation_space
        self.action_space = action_space
        # These should all be set from the DataCollector
        self.observations = None
        self.rewards = None
        self.terminations = None
        self.truncations = None
        self.infos = None
        self.initial_obs = None
        self.initial_infos = None
    
    def reset(self,*args,**kwargs):
        return self.initial_obs, self.initial_infos

    def step(self, action):
        return self.observations, self.rewards, self.terminations, self.truncations, self.infos

class ScholaDataCollector(DataCollector):
    
    def __init__(self, protocol: BaseImitationProtocol, simulator: BaseSimulator, seed: int = None, options: dict = None):
        self.protocol = protocol
        self.simulator = simulator

        self.protocol.start()
        self.simulator.start(self.protocol.properties)

        ids, _, observation_spaces, action_spaces = self.protocol.get_definition()
        id_manager = IdManager(ids)
        self._env_id, self._agent_id = id_manager[0]

        assert id_manager.num_ids == 1 , "Minari Integration only supports one environment and one agent"

        self.protocol.send_startup_msg(seeds=[seed], options=[options])
        
        # Extract the actual gym spaces for the single environment and agent
        observation_space = observation_spaces[self._env_id][self._agent_id]
        action_space = action_spaces[self._env_id][self._agent_id]
        
        super().__init__(_FakeGymEnv(observation_space, action_space))
        
        # Flag to track if we need to initialize the buffer with initial observations
        self._needs_initial_reset = True

    def reset(self,*args,**kwargs):
        raise NotImplementedError("Reset is not implemented for Minari DataCollection with Schola. The Environment will reset itself.")

    def step(self,*args,**kwargs):
        observations, rewards, terminations, truncations, infos, initial_obs, initial_infos, actions = self.protocol.get_data()
        
        # On first step, initialize the buffer with initial observations
        if self._needs_initial_reset and self._env_id in initial_obs and self._agent_id in initial_obs[self._env_id]:
            self.env.initial_obs = initial_obs[self._env_id][self._agent_id]
            self.env.initial_infos = initial_infos[self._env_id][self._agent_id]
            super().reset()
            self._needs_initial_reset = False
        
        #inject all of the data into the fake gym env and then step it
        self.env.observations = observations[self._env_id][self._agent_id]
        self.env.rewards = rewards[self._env_id][self._agent_id]
        self.env.terminations = terminations[self._env_id][self._agent_id]
        self.env.truncations = truncations[self._env_id][self._agent_id]
        self.env.infos = infos[self._env_id][self._agent_id]
        
        # Extract the actual action value (not the dictionary)
        action = actions[self._env_id][self._agent_id]
        step_output = super().step(action)

        if(self.env.terminations or self.env.truncations):
            self.env.initial_obs = initial_obs[self._env_id][self._agent_id]
            self.env.initial_infos = initial_infos[self._env_id][self._agent_id]
            super().reset()


    def close(self):
        self.protocol.close()
        self.simulator.stop()


