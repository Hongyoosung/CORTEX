"""
Test Script for UE5 Action Execution Fixes

Validates:
1. Movement - Smooth pathfinding without jitter
2. Crouch - Proper state toggling
3. Fire - Masking when no enemies visible
4. Lookup - Rotation changes (already working)

Usage:
    1. Start UE5 with Schola server (port 50051)
    2. python test_action_fixes.py
    3. Check UE5 logs for expected behavior
"""

import numpy as np
import time
import sys
from sbdapm_env import SBDAPMMultiAgentEnv


class ActionTester:
    """Automated test suite for action execution."""

    def __init__(self, host="localhost", port=50051):
        print("=" * 70)
        print("UE5 ACTION EXECUTION TEST SUITE")
        print("=" * 70)
        print(f"\nConnecting to {host}:{port}...")

        try:
            self.env = SBDAPMMultiAgentEnv(host=host, port=port)
            obs_dict, _ = self.env.reset()
            self.agent_ids = list(obs_dict.keys())
            print(f"✓ Connected! Found {len(self.agent_ids)} agents: {self.agent_ids}\n")
        except Exception as e:
            print(f"✗ Connection failed: {e}")
            print("\nMake sure UE5 is running with Schola server on port 50051")
            sys.exit(1)

    def create_action(self, move_x=0.0, move_y=0.0, speed=0.0,
                     look_x=0.0, look_y=0.0, fire=0.0, crouch=0.0):
        """Create a 7-dimensional action vector."""
        return np.array([move_x, move_y, speed, look_x, look_y, fire, crouch],
                       dtype=np.float32)

    def send_actions(self, action, duration=2.0, description=""):
        """Send same action to all agents for specified duration."""
        print(f"\n{'─' * 70}")
        print(f"TEST: {description}")
        print(f"Action: Move=({action[0]:.2f},{action[1]:.2f}) Speed={action[2]:.2f} "
              f"Look=({action[3]:.2f},{action[4]:.2f}) Fire={action[5]:.2f} Crouch={action[6]:.2f}")
        print(f"Duration: {duration}s")
        print(f"{'─' * 70}")

        steps = int(duration * 10)  # 10 FPS for visibility
        for step in range(steps):
            actions = {agent_id: action.copy() for agent_id in self.agent_ids}
            obs_dict, rewards, dones, truncs, infos = self.env.step(actions)
            time.sleep(0.1)

            # Print progress
            if step % 5 == 0:
                print(f"  Step {step}/{steps} - Check UE5 logs...")

    def test_movement_smooth(self):
        """Test 1: Movement should be smooth without jitter."""
        print("\n" + "=" * 70)
        print("TEST 1: MOVEMENT - Smooth Pathfinding")
        print("=" * 70)
        print("\n📋 Expected UE5 Logs:")
        print("  [MOVE EXEC AI] 'AgentName': NEW target (...), Speed=...")
        print("  (Should appear 1-3 times, NOT 60 times/second)")
        print("\n📋 Expected Behavior:")
        print("  - Agents move SMOOTHLY toward destination")
        print("  - No vibrating or jittering in place")
        print("  - Agents cover distance before getting new target")

        # Test 1a: Forward movement
        self.send_actions(
            self.create_action(move_x=1.0, move_y=0.0, speed=0.8),
            duration=3.0,
            description="Forward Movement (should be smooth)"
        )

        # Test 1b: Diagonal movement
        self.send_actions(
            self.create_action(move_x=0.7, move_y=0.7, speed=0.6),
            duration=3.0,
            description="Diagonal Movement (should be smooth)"
        )

        # Test 1c: Backward movement
        self.send_actions(
            self.create_action(move_x=-1.0, move_y=0.0, speed=0.5),
            duration=3.0,
            description="Backward Movement (should be smooth)"
        )

        print("\n✓ Movement test complete. Verify in UE5:")
        print("  ✓ [MOVE EXEC AI] logs appear ~1-2 times per test (not spam)")
        print("  ✓ Agents moved smoothly without vibrating")
        input("\nPress Enter when verified...")

    def test_crouch(self):
        """Test 2: Crouch should toggle and log state changes."""
        print("\n" + "=" * 70)
        print("TEST 2: CROUCH - State Toggling")
        print("=" * 70)
        print("\n📋 Expected UE5 Logs:")
        print("  [CROUCH] 'AgentName': Crouching")
        print("  [CROUCH] 'AgentName': Standing")
        print("\n📋 OR If Broken:")
        print("  [CROUCH FAILED] 'AgentName': No CharacterMovementComponent found!")
        print("\n📋 Expected Behavior:")
        print("  - Agent capsule height visibly decreases when crouching")
        print("  - Agent capsule height returns to normal when standing")

        # Test 2a: Crouch
        self.send_actions(
            self.create_action(crouch=1.0),
            duration=2.0,
            description="Crouch Down"
        )

        # Test 2b: Stand
        self.send_actions(
            self.create_action(crouch=0.0),
            duration=2.0,
            description="Stand Up"
        )

        # Test 2c: Crouch while moving
        self.send_actions(
            self.create_action(move_x=0.5, move_y=0.0, speed=0.4, crouch=1.0),
            duration=2.0,
            description="Crouch + Move (should crouch-walk)"
        )

        print("\n✓ Crouch test complete. Verify in UE5:")
        print("  ✓ [CROUCH] logs appeared")
        print("  ✓ Agent capsule height changed visibly")
        print("  ✗ OR: [CROUCH FAILED] error (component missing)")
        input("\nPress Enter when verified...")

    def test_fire_masking(self):
        """Test 3: Fire should be masked when no enemies visible."""
        print("\n" + "=" * 70)
        print("TEST 3: FIRE MASKING - Enemy Detection")
        print("=" * 70)
        print("\n📋 Expected UE5 Logs:")
        print("  [FIRE MASKED] 'AgentName': No targets detected (VisibleEnemyCount=0)")
        print("  OR (if enemies spawned):")
        print("  [FIRE ALLOWED] 'AgentName': Targets detected (VisibleEnemyCount=N)")
        print("\n📋 Expected Behavior:")
        print("  - If no enemies: Fire action blocked, no projectiles spawn")
        print("  - If enemies: Fire action allowed, weapon fires")

        # Test 3a: Attempt fire with no enemies
        self.send_actions(
            self.create_action(fire=1.0),
            duration=2.0,
            description="Fire (should be MASKED if no enemies)"
        )

        # Test 3b: Fire while looking around (more realistic)
        self.send_actions(
            self.create_action(look_x=0.3, look_y=0.1, fire=1.0),
            duration=2.0,
            description="Fire + Look (should be MASKED if no enemies)"
        )

        print("\n✓ Fire masking test complete. Verify in UE5:")
        print("  ✓ [FIRE MASKED] logs appeared (if no enemies)")
        print("  ✓ OR [FIRE ALLOWED] logs appeared (if enemies present)")
        print("  ✓ No projectiles spawned when masked")
        input("\nPress Enter when verified...")

    def test_lookup(self):
        """Test 4: Lookup/rotation should change smoothly."""
        print("\n" + "=" * 70)
        print("TEST 4: LOOKUP - Rotation Changes")
        print("=" * 70)
        print("\n📋 Expected Behavior:")
        print("  - Agent rotation changes SMOOTHLY")
        print("  - No logs expected (this already worked)")
        print("  - Agent should look left, right, up, down")

        # Test 4a: Look left
        self.send_actions(
            self.create_action(look_x=-0.5, look_y=0.0),
            duration=1.5,
            description="Look Left"
        )

        # Test 4b: Look right
        self.send_actions(
            self.create_action(look_x=0.5, look_y=0.0),
            duration=1.5,
            description="Look Right"
        )

        # Test 4c: Look up
        self.send_actions(
            self.create_action(look_x=0.0, look_y=0.5),
            duration=1.5,
            description="Look Up"
        )

        # Test 4d: Look down
        self.send_actions(
            self.create_action(look_x=0.0, look_y=-0.5),
            duration=1.5,
            description="Look Down"
        )

        print("\n✓ Lookup test complete. Verify in UE5:")
        print("  ✓ Agent rotation changed in all directions")
        print("  ✓ Rotation was smooth (not snapping)")
        input("\nPress Enter when verified...")

    def test_combined_actions(self):
        """Test 5: Combined actions (move + look + crouch + fire)."""
        print("\n" + "=" * 70)
        print("TEST 5: COMBINED ACTIONS - Realistic Combat Scenario")
        print("=" * 70)
        print("\n📋 Expected Behavior:")
        print("  - Agent should perform ALL actions simultaneously:")
        print("    - Move forward while crouched")
        print("    - Look around while moving")
        print("    - Attempt to fire (masked if no enemies)")

        # Combined action: Move forward + crouch + look + fire
        self.send_actions(
            self.create_action(
                move_x=0.8, move_y=0.2, speed=0.6,
                look_x=0.3, look_y=-0.1,
                fire=1.0, crouch=1.0
            ),
            duration=3.0,
            description="Combined: Move + Crouch + Look + Fire"
        )

        print("\n✓ Combined actions test complete. Verify in UE5:")
        print("  ✓ Agent moved smoothly while crouched")
        print("  ✓ Agent rotation changed during movement")
        print("  ✓ Fire was attempted (masked or allowed)")
        input("\nPress Enter when verified...")

    def run_all_tests(self):
        """Run complete test suite."""
        print("\n" + "=" * 70)
        print("RUNNING ALL TESTS")
        print("=" * 70)
        print("\nThis will test all action types in sequence.")
        print("Watch the UE5 viewport and Output Log for expected behavior.")
        print("\nPress Ctrl+C at any time to abort.\n")

        try:
            # Run tests in order
            self.test_movement_smooth()
            self.test_crouch()
            self.test_fire_masking()
            self.test_lookup()
            self.test_combined_actions()

            # Final report
            print("\n" + "=" * 70)
            print("TEST SUITE COMPLETE")
            print("=" * 70)
            print("\n✓ All tests executed successfully!")
            print("\nFinal Checklist:")
            print("  [ ] Movement: Smooth pathfinding (no jitter)")
            print("  [ ] Crouch: State changes logged and visible")
            print("  [ ] Fire: Masking logged correctly")
            print("  [ ] Lookup: Rotation changes smoothly")
            print("  [ ] Combined: All actions work together")
            print("\nIf all tests passed, the fixes are working correctly!")

        except KeyboardInterrupt:
            print("\n\n⚠ Tests interrupted by user")
        except Exception as e:
            print(f"\n\n✗ Test failed with error: {e}")
            import traceback
            traceback.print_exc()
        finally:
            print("\nClosing environment...")
            self.env.close()


def main():
    """Main entry point."""
    import argparse

    parser = argparse.ArgumentParser(description="Test UE5 action execution fixes")
    parser.add_argument("--host", type=str, default="localhost",
                       help="Schola server host")
    parser.add_argument("--port", type=int, default=50051,
                       help="Schola server port")
    parser.add_argument("--test", type=str, choices=["all", "movement", "crouch", "fire", "lookup", "combined"],
                       default="all", help="Which test to run")

    args = parser.parse_args()

    # Create tester
    tester = ActionTester(host=args.host, port=args.port)

    # Run requested test
    if args.test == "all":
        tester.run_all_tests()
    elif args.test == "movement":
        tester.test_movement_smooth()
    elif args.test == "crouch":
        tester.test_crouch()
    elif args.test == "fire":
        tester.test_fire_masking()
    elif args.test == "lookup":
        tester.test_lookup()
    elif args.test == "combined":
        tester.test_combined_actions()


if __name__ == "__main__":
    main()
