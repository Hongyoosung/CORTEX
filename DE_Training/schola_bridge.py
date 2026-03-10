"""
Schola Bridge - Python-UE5 Communication Layer for DE Training

Provides interface for:
- Environment reset/step
- State observation collection
- Action execution
- Reward retrieval
- Episode management

Communication: Socket-based (TCP) with JSON serialization
Alternative: gRPC for production (higher performance)

Usage:
    from schola_bridge import DEEnvironment

    env = DEEnvironment(host='localhost', port=8888)
    obs = env.reset()

    for step in range(1000):
        action = policy.predict(obs)
        obs, reward, done, info = env.step(action)
        if done:
            obs = env.reset()
"""

import socket
import json
import numpy as np
from typing import Dict, Tuple, List, Optional, Any


class DEEnvironment:
    """
    Python interface to DE Arena UE5 environment.

    State Space: 52-dim observation (positions, health, visibility)
    Action Space: (OptionType, TargetPos, Duration, EQSWeights)
    Reward: Strategy-conditioned rewards (see DERewardCalculator)
    """

    def __init__(
        self,
        host: str = 'localhost',
        port: int = 8888,
        timeout: float = 30.0,
        max_retries: int = 3
    ):
        """
        Initialize connection to UE5 environment.

        Args:
            host: UE5 server host
            port: UE5 server port
            timeout: Socket timeout in seconds
            max_retries: Connection retry attempts
        """
        self.host = host
        self.port = port
        self.timeout = timeout
        self.max_retries = max_retries

        self.socket: Optional[socket.socket] = None
        self.episode_count = 0
        self.step_count = 0

        self._connect()

    def _connect(self):
        """Establish socket connection to UE5."""
        for attempt in range(self.max_retries):
            try:
                self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.socket.settimeout(self.timeout)
                self.socket.connect((self.host, self.port))
                print(f"✅ Connected to UE5 at {self.host}:{self.port}")
                return
            except Exception as e:
                print(f"⚠️ Connection attempt {attempt + 1}/{self.max_retries} failed: {e}")
                if attempt == self.max_retries - 1:
                    raise ConnectionError(f"Failed to connect to UE5 after {self.max_retries} attempts")

    def reset(self, team_id: int = 0, agent_id: int = 0) -> np.ndarray:
        """
        Reset environment and return initial observation.

        Args:
            team_id: Team ID (0 = Red, 1 = Blue)
            agent_id: Agent ID (0-4)

        Returns:
            Initial observation (52-dim numpy array)
        """
        request = {
            'command': 'reset',
            'team_id': team_id,
            'agent_id': agent_id
        }

        response = self._send_receive(request)

        if response['status'] != 'success':
            raise RuntimeError(f"Reset failed: {response.get('error', 'Unknown error')}")

        self.episode_count += 1
        self.step_count = 0

        return np.array(response['observation'], dtype=np.float32)

    def step(
        self,
        option_type: int,
        target_position: List[float],
        option_duration: float,
        eqs_weights: List[float]
    ) -> Tuple[np.ndarray, float, bool, Dict[str, Any]]:
        """
        Execute action and return (obs, reward, done, info).

        Args:
            option_type: Strategy type (0=Assault, 1=Defend, 2=Support, 3=Scout, 4=Retreat)
            target_position: [x, y, z] target coordinates
            option_duration: Expected strategy duration (seconds)
            eqs_weights: 7-dim EQS weight parameters

        Returns:
            observation: Next state (52-dim)
            reward: Strategy-conditioned reward
            done: Episode terminated
            info: Additional metadata
        """
        request = {
            'command': 'step',
            'action': {
                'option_type': option_type,
                'target_position': target_position,
                'option_duration': option_duration,
                'eqs_weights': eqs_weights
            }
        }

        response = self._send_receive(request)

        if response['status'] != 'success':
            raise RuntimeError(f"Step failed: {response.get('error', 'Unknown error')}")

        self.step_count += 1

        obs = np.array(response['observation'], dtype=np.float32)
        reward = float(response['reward'])
        done = bool(response['done'])
        info = response.get('info', {})

        return obs, reward, done, info

    def get_team_state(self) -> Dict[str, Any]:
        """
        Get current team state (for multi-agent coordination).

        Returns:
            Dictionary containing:
            - ally_positions: List of ally positions
            - ally_strategies: List of active strategies
            - ally_health: List of health ratios
            - strategy_distribution: Distribution of strategies
        """
        request = {'command': 'get_team_state'}
        response = self._send_receive(request)

        if response['status'] != 'success':
            raise RuntimeError(f"Get team state failed: {response.get('error', 'Unknown error')}")

        return response['team_state']

    def get_capture_points_status(self) -> List[Dict[str, Any]]:
        """
        Get status of all 5 capture points.

        Returns:
            List of dictionaries with:
            - position: [x, y, z]
            - owner_team: -1 (neutral), 0 (red), 1 (blue)
            - capture_progress: 0.0 to 1.0
            - contested: bool
        """
        request = {'command': 'get_capture_points'}
        response = self._send_receive(request)

        if response['status'] != 'success':
            raise RuntimeError(f"Get capture points failed: {response.get('error', 'Unknown error')}")

        return response['capture_points']

    def close(self):
        """Close connection to UE5."""
        if self.socket:
            try:
                request = {'command': 'disconnect'}
                self._send_receive(request, expect_response=False)
            except:
                pass
            finally:
                self.socket.close()
                self.socket = None
                print("🔌 Disconnected from UE5")

    def _send_receive(
        self,
        request: Dict[str, Any],
        expect_response: bool = True
    ) -> Optional[Dict[str, Any]]:
        """
        Send request and receive response via socket.

        Args:
            request: Request dictionary
            expect_response: Wait for response

        Returns:
            Response dictionary or None
        """
        if not self.socket:
            raise ConnectionError("Not connected to UE5")

        # Serialize request
        request_json = json.dumps(request)
        request_bytes = request_json.encode('utf-8')

        # Send with length prefix (4 bytes)
        message_length = len(request_bytes)
        self.socket.sendall(message_length.to_bytes(4, byteorder='little'))
        self.socket.sendall(request_bytes)

        if not expect_response:
            return None

        # Receive response length
        length_bytes = self._recv_exact(4)
        response_length = int.from_bytes(length_bytes, byteorder='little')

        # Receive response data
        response_bytes = self._recv_exact(response_length)
        response_json = response_bytes.decode('utf-8')

        return json.loads(response_json)

    def _recv_exact(self, num_bytes: int) -> bytes:
        """Receive exact number of bytes from socket."""
        data = b''
        while len(data) < num_bytes:
            chunk = self.socket.recv(num_bytes - len(data))
            if not chunk:
                raise ConnectionError("Socket connection broken")
            data += chunk
        return data

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()


class DEMultiAgentEnvironment:
    """
    Multi-agent wrapper for 5v5 team coordination.

    Manages 5 agents simultaneously with centralized training.
    """

    def __init__(
        self,
        host: str = 'localhost',
        port: int = 8888,
        num_agents: int = 5,
        team_id: int = 0
    ):
        """
        Initialize multi-agent environment.

        Args:
            host: UE5 server host
            port: UE5 server port (incremented for each agent)
            num_agents: Number of agents (default 5)
            team_id: Team ID (0 = Red, 1 = Blue)
        """
        self.num_agents = num_agents
        self.team_id = team_id

        # Create separate connection for each agent
        self.agents = [
            DEEnvironment(host=host, port=port + i)
            for i in range(num_agents)
        ]

    def reset(self) -> List[np.ndarray]:
        """
        Reset all agents.

        Returns:
            List of initial observations (one per agent)
        """
        return [
            agent.reset(team_id=self.team_id, agent_id=i)
            for i, agent in enumerate(self.agents)
        ]

    def step(
        self,
        actions: List[Tuple[int, List[float], float, List[float]]]
    ) -> Tuple[List[np.ndarray], List[float], List[bool], List[Dict]]:
        """
        Execute actions for all agents.

        Args:
            actions: List of (option_type, target, duration, weights) for each agent

        Returns:
            observations: List of next states
            rewards: List of rewards
            dones: List of done flags
            infos: List of info dicts
        """
        results = [
            agent.step(*action)
            for agent, action in zip(self.agents, actions)
        ]

        observations, rewards, dones, infos = zip(*results)
        return list(observations), list(rewards), list(dones), list(infos)

    def close(self):
        """Close all agent connections."""
        for agent in self.agents:
            agent.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()


# Example usage
if __name__ == '__main__':
    # Single agent example
    with DEEnvironment(host='localhost', port=8888) as env:
        obs = env.reset()
        print(f"Initial observation shape: {obs.shape}")

        for step in range(10):
            # Random action (for testing)
            option_type = np.random.randint(0, 5)
            target = [
                np.random.uniform(-7500, 7500),
                np.random.uniform(-7500, 7500),
                0.0
            ]
            duration = np.random.uniform(5.0, 20.0)
            weights = np.random.uniform(-1, 1, size=7).tolist()

            obs, reward, done, info = env.step(option_type, target, duration, weights)
            print(f"Step {step}: Reward={reward:.2f}, Done={done}")

            if done:
                obs = env.reset()

    print("✅ Schola Bridge test completed")
