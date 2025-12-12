"""
Debug script to inspect Schola id_manager and action keys
"""

from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
from schola.gym.env import GymVectorEnv
from gymnasium import spaces

# Import SafeUnrealVectorEnv from sbdapm_env
import sys
sys.path.insert(0, r'C:\Users\PC\Documents\GitHub\SBDAPM\Source\GameAI_Project\Scripts')
from sbdapm_env import SafeUnrealVectorEnv

print("="*80)
print("Connecting to Schola...")
print("="*80)

connection = UnrealEditorConnection(url="localhost", port=50051)
env = SafeUnrealVectorEnv(unreal_connection=connection, verbosity=1)

#Wait for connection
print("\nWaiting for environment to initialize...")
print("(Make sure UE5 editor is running with PIE started)")

# Reset to get initial state
obs, info = env.reset()

print("\n" + "="*80)
print("INSPECTION COMPLETE")
print("="*80)
print(f"\nFinal Results:")
print(f"  - action_space keys: {list(env.action_space.keys())}")
print(f"  - observation keys: {list(obs.keys()) if isinstance(obs, dict) else 'N/A'}")
print(f"  - id_manager entries: {len(env.id_manager.id_list)}")
print(f"  - valid_agent_keys: {env._valid_agent_keys}")
print(f"  - cdo_keys: {env._cdo_keys}")

env.close()
