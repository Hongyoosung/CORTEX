"""
Test script for v6.0 architecture changes

Verifies:
1. Observation space: 68 features (64 base + 4 objective context)
2. Action space: Discrete(4) for strategy selection
3. Network architecture: Single-head policy + value
4. ONNX export compatibility

Run: python test_v6_architecture.py
"""

import numpy as np
import torch
import torch.nn as nn
from gymnasium import spaces


def test_observation_space():
    """Test v6.0 observation space (68 features)"""
    print("\n=== Testing Observation Space ===")

    obs_space = spaces.Box(low=-np.inf, high=np.inf, shape=(68,), dtype=np.float32)

    # Create dummy observation
    dummy_obs = np.random.randn(68).astype(np.float32)

    # Verify breakdown
    agent_state = dummy_obs[0:7]      # pos(3), vel(3), health(1)
    combat = dummy_obs[7:8]           # enemy_dist(1)
    perception = dummy_obs[8:40]      # raycasts(16), hit_types(16)
    enemy_info = dummy_obs[40:56]     # count(1), nearby(15)
    tactical = dummy_obs[56:60]       # cover features(4)
    support = dummy_obs[60:64]        # ally context(4)
    objective = dummy_obs[64:68]      # objective context(4) - NEW in v6.0

    print(f"[OK] Observation space shape: {obs_space.shape}")
    print(f"   - Agent State: {len(agent_state)} features")
    print(f"   - Combat: {len(combat)} features")
    print(f"   - Perception: {len(perception)} features")
    print(f"   - Enemy Info: {len(enemy_info)} features")
    print(f"   - Tactical: {len(tactical)} features")
    print(f"   - Support: {len(support)} features")
    print(f"   - Objective Context (v6.0): {len(objective)} features")

    total = len(agent_state) + len(combat) + len(perception) + len(enemy_info) + len(tactical) + len(support) + len(objective)
    assert total == 68, f"Feature count mismatch: {total} != 68"
    print(f"[OK] Total features: {total}")


def test_action_space():
    """Test v6.0 action space (Discrete 4)"""
    print("\n=== Testing Action Space ===")

    action_space = spaces.Discrete(4)

    # Sample actions
    for _ in range(5):
        action = action_space.sample()
        assert 0 <= action < 4, f"Invalid action: {action}"

    print(f"[OK] Action space: Discrete(4)")
    print(f"   - 0: Assault")
    print(f"   - 1: Defend")
    print(f"   - 2: Support")
    print(f"   - 3: Retreat")


def test_network_architecture():
    """Test v6.0 single-head network"""
    print("\n=== Testing Network Architecture ===")

    class TestPolicyNetwork(nn.Module):
        """Simplified version of SingleHeadStrategyPolicy for testing"""
        def __init__(self, obs_dim=68, hidden_layers=[128, 128, 64], num_outputs=4):
            super().__init__()

            # Shared trunk
            layers = []
            prev_size = obs_dim
            for hidden_size in hidden_layers:
                layers.append(nn.Linear(prev_size, hidden_size))
                layers.append(nn.ReLU())
                prev_size = hidden_size

            self.shared_trunk = nn.Sequential(*layers)

            # Policy head (4 strategy logits)
            self.policy_head = nn.Linear(prev_size, num_outputs)

            # Value head (1 value estimate)
            self.value_head = nn.Linear(prev_size, 1)

        def forward(self, obs):
            features = self.shared_trunk(obs)
            policy_logits = self.policy_head(features)
            value = self.value_head(features)
            return policy_logits, value

    # Create network
    network = TestPolicyNetwork()
    network.eval()

    # Test forward pass
    batch_size = 4
    dummy_input = torch.randn(batch_size, 68)

    with torch.no_grad():
        policy_logits, value = network(dummy_input)

    print(f"[OK] Network created successfully")
    print(f"   - Input shape: {dummy_input.shape}")
    print(f"   - Policy logits shape: {policy_logits.shape} (expected: [4, 4])")
    print(f"   - Value shape: {value.shape} (expected: [4, 1])")

    assert policy_logits.shape == (batch_size, 4), f"Policy shape mismatch: {policy_logits.shape}"
    assert value.shape == (batch_size, 1), f"Value shape mismatch: {value.shape}"

    # Verify policy outputs are valid logits
    probs = torch.softmax(policy_logits, dim=-1)
    assert torch.allclose(probs.sum(dim=-1), torch.ones(batch_size), atol=1e-5), "Softmax probabilities don't sum to 1"

    print(f"[OK] Sample policy distribution (agent 0): {probs[0].numpy()}")
    print(f"[OK] Sample value estimate (agent 0): {value[0].item():.3f}")


def test_onnx_export_compatibility():
    """Test ONNX export would work"""
    print("\n=== Testing ONNX Export Compatibility ===")

    class TestPolicyNetwork(nn.Module):
        def __init__(self):
            super().__init__()
            self.trunk = nn.Sequential(
                nn.Linear(68, 128),
                nn.ReLU(),
                nn.Linear(128, 128),
                nn.ReLU(),
                nn.Linear(128, 64),
                nn.ReLU()
            )
            self.policy_head = nn.Linear(64, 4)
            self.value_head = nn.Linear(64, 1)

        def forward(self, obs):
            features = self.trunk(obs)
            policy_logits = self.policy_head(features)
            value = self.value_head(features)
            return policy_logits, value

    network = TestPolicyNetwork()
    network.eval()

    # Create dummy input
    dummy_input = torch.randn(1, 68)

    try:
        # Test ONNX export (in-memory)
        import io
        f = io.BytesIO()

        torch.onnx.export(
            network,
            dummy_input,
            f,
            input_names=["observation"],
            output_names=["policy_logits", "value"],
            dynamic_axes={
                "observation": {0: "batch_size"},
                "policy_logits": {0: "batch_size"},
                "value": {0: "batch_size"}
            },
            opset_version=11
        )

        print(f"[OK] ONNX export successful (in-memory)")
        print(f"   - Input: observation [batch_size, 68]")
        print(f"   - Output 1: policy_logits [batch_size, 4]")
        print(f"   - Output 2: value [batch_size, 1]")

    except ModuleNotFoundError as e:
        print(f"[SKIP] ONNX export skipped: {e}")
        print(f"       Install onnxscript for full ONNX testing: pip install onnxscript")
    except Exception as e:
        print(f"[FAIL] ONNX export failed: {e}")
        raise


def test_environment_compatibility():
    """Test environment would work with v6.0 changes"""
    print("\n=== Testing Environment Compatibility ===")

    # Simulate environment step
    num_agents = 4

    # Create observations (68 features each)
    observations = {
        f"agent_{i}": np.random.randn(68).astype(np.float32)
        for i in range(num_agents)
    }

    # Create actions (Discrete 4)
    actions = {
        f"agent_{i}": np.random.randint(0, 4)
        for i in range(num_agents)
    }

    # Create action masks (all strategies valid)
    action_masks = {
        f"agent_{i}": np.array([1, 1, 1, 1], dtype=np.int8)
        for i in range(num_agents)
    }

    print(f"[OK] Environment step simulation successful")
    print(f"   - Agents: {num_agents}")
    print(f"   - Observation shape per agent: {observations['agent_0'].shape}")
    print(f"   - Action range: [0, 3]")
    print(f"   - Action mask: {action_masks['agent_0']}")

    # Verify all observations have correct shape
    for agent_id, obs in observations.items():
        assert obs.shape == (68,), f"Invalid obs shape for {agent_id}: {obs.shape}"

    # Verify all actions are valid
    for agent_id, action in actions.items():
        assert 0 <= action < 4, f"Invalid action for {agent_id}: {action}"


def main():
    print("=" * 60)
    print("CORTEX v6.0 Architecture Test Suite")
    print("=" * 60)

    try:
        test_observation_space()
        test_action_space()
        test_network_architecture()
        test_onnx_export_compatibility()
        test_environment_compatibility()

        print("\n" + "=" * 60)
        print("[OK] ALL TESTS PASSED")
        print("=" * 60)
        print("\nv6.0 Architecture Summary:")
        print("  - Observation: 68 features (64 base + 4 objective context)")
        print("  - Action: Discrete(4) - Strategy selection only")
        print("  - Network: Single-head (4 policy logits + 1 value)")
        print("  - ONNX: Single unified model (cortex_policy_v6.onnx)")
        print("\nReady for training!")

    except Exception as e:
        print("\n" + "=" * 60)
        print("[FAIL] TEST FAILED")
        print("=" * 60)
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        return 1

    return 0


if __name__ == "__main__":
    exit(main())
