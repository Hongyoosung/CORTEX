#!/usr/bin/env python3

import sys
import os
import time

def verify_multi_actor_setup():
    """Verify that UE5 is configured with 4 environment actors."""
    print("=" * 80)
    print("MULTI-ACTOR SCHOLA SETUP VERIFICATION")
    print("=" * 80)
    print()
    
    try:
        # Import Schola
        print("[1/5] Importing Schola...")
        from schola.core.env import ScholaEnv, AutoResetType
        from schola.core.unreal_connections import UnrealEditorConnection
        print(" ✓ Schola imported successfully")
        print()
        
        # Connect to UE5
        print("[2/5] Connecting to UE5 (timeout: 30s)...")
        print(" → Make sure UE5 is running with PIE (Play In Editor) active")
        print(" → Looking for gRPC server on localhost:50051...")
        
        start_time = time.time()
        
        # 올바른 파라미터 이름 사용 (언더스코어)
        connection = UnrealEditorConnection(url="localhost", port=50051)
        
        schola_env = ScholaEnv(
            unreal_connection=connection,  # ✓ 언더스코어 사용
            verbosity=1,
            auto_reset_type=AutoResetType.SAME_STEP  # ✓ 언더스코어 사용
        )
        
        elapsed = time.time() - start_time
        print(f" ✓ Connected to UE5 in {elapsed:.2f}s")
        print()
        
        # Check environment structure
        print("[3/5] Analyzing environment structure...")
        num_envs = len(schola_env.ids)
        print(f" → Physical environments detected: {num_envs}")
        print()
        
        # Verify environment count
        print("[4/5] Verifying environment count...")
        if num_envs == 4:
            print(" ✅ CORRECT: 4 physical environments detected")
        elif num_envs == 1:
            print(" ❌ ERROR: Only 1 environment detected")
            print(" → Follow the setup guide to create 4 environment actors")
            print(" → Each actor should manage different teams")
            return False
        else:
            print(f" ⚠️  WARNING: Unexpected number of environments: {num_envs}")
            print(f" → Expected: 4")
            print(f" → Got: {num_envs}")
        print()
        
        # Check agent distribution
        print("[5/5] Verifying agent distribution...")
        all_correct = True
        
        for env_id in range(num_envs):
            if env_id < len(schola_env.ids):
                agent_ids = schola_env.ids[env_id]
                num_agents = len(agent_ids)
                
                print(f" Environment {env_id}:")
                print(f"   - Agent count: {num_agents}")
                print(f"   - Agent IDs: {sorted(agent_ids)}")
                
                if num_agents == 8:
                    print(f"   ✅ Correct agent count")
                else:
                    print(f"   ❌ ERROR: Expected 8 agents, got {num_agents}")
                    all_correct = False
                print()
        
        print("=" * 80)
        print("VERIFICATION SUMMARY")
        print("=" * 80)
        
        if num_envs == 4 and all_correct:
            print("✅ SETUP CORRECT")
            print(" - 4 physical environments detected")
            print(" - Each environment has 8 agents")
            print(" - Ready for vectorized training!")
            print()
            print("Next steps:")
            print(" 1. Test independent episode termination")
            print(" 2. Run: python train_rllib.py")
            return True
        else:
            print("❌ SETUP INCOMPLETE")
            if num_envs != 4:
                print(" - Issue: Wrong number of environments")
                print(" - Expected: 4 environment actors in UE5 level")
                print(f" - Got: {num_envs} environment(s)")
            
            if not all_correct:
                print(" - Issue: Incorrect agent distribution")
                print(" - Expected: 8 agents per environment")
            print()
            print("Solution:")
            print(" → Follow MULTI_ACTOR_SETUP_GUIDE.md")
            print(" → Create 4 BP_ScholaCombatEnvironment actors")
            print(" → Configure TrainingTeamIDs for each actor:")
            print("   - Actor 0: [0, 1]")
            print("   - Actor 1: [2, 3]")
            print("   - Actor 2: [4, 5]")
            print("   - Actor 3: [6, 7]")
            return False
    
    except ImportError as e:
        print(f"❌ ERROR: Failed to import Schola")
        print(f"   {e}")
        print()
        print("Solution:")
        print(" → Install Schola: pip install schola-rl")
        return False
    
    except Exception as e:
        print(f"❌ ERROR: {type(e).__name__}: {e}")
        print()
        print("Common issues:")
        print(" 1. UE5 not running:")
        print("    → Start UE5 and press Play (PIE)")
        print(" 2. gRPC server not started:")
        print("    → Check UE5 Output Log for 'ScholaEnv' messages")
        print(" 3. Wrong port:")
        print("    → Verify ServerPort = 50051 in Actor 0")
        return False
    
    finally:
        # Close connection
        try:
            schola_env.close()
        except:
            pass


if __name__ == "__main__":
    print()
    success = verify_multi_actor_setup()
    print()
    sys.exit(0 if success else 1)
