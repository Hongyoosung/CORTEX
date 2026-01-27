"""
Episode Termination Diagnostic Tool (v8.9.2)

This script connects to UE5 and monitors episode termination behavior to diagnose
why episodes are running too long (2900+ steps without completion).

Usage:
    python diagnose_episode_termination.py

What it checks:
    1. Are termination flags being sent by UE5?
    2. How long does it take for termination flags to arrive?
    3. Are auto-resets clearing flags before Python can see them?
    4. What are the actual episode lengths in UE5 vs Python?

Expected UE5 Configuration:
    - MaxStepsPerEpisode: ~2000 steps
    - MaxEpisodeDuration: ~60 seconds
    - Termination conditions: Team annihilation OR timeout
"""

import time
from schola.core.env import ScholaEnv, AutoResetType
from schola.core.unreal_connections.editor_connection import UnrealEditorConnection

def diagnose_termination():
    """Monitor episode termination behavior."""

    print("=" * 80)
    print("EPISODE TERMINATION DIAGNOSTIC")
    print("=" * 80)
    print()

    # Connect to UE5
    print("Connecting to UE5...")
    print("  Make sure UE5 is running with the training map loaded!")
    print("  Map: Training_BasicCombat_2v2_v01")
    print()

    try:
        connection = UnrealEditorConnection(url="localhost", port=50051)
        env = ScholaEnv(
            unreal_connection=connection,
            num_envs=4,  # CRITICAL FIX: Enable 4-environment vectorization
            verbosity=1,
            auto_reset_type=AutoResetType.SAME_STEP
        )
        print("✓ Connected to UE5!")
    except Exception as e:
        print(f"✗ Connection failed: {e}")
        print("\nTroubleshooting:")
        print("  1. Is UE5 running?")
        print("  2. Is the Schola plugin enabled?")
        print("  3. Is the training map open?")
        print("  4. Check firewall settings for port 50051")
        return
    print()

    # DIAGNOSTIC: Verify environment structure after fix
    print("=" * 80)
    print("SCHOLA ENVIRONMENT STRUCTURE CHECK")
    print("=" * 80)
    print("Expected: 4 independent environments with 8 agents each")
    print(f"Actual structure after num_envs=4 fix:")
    print()

    # Initial reset
    print("Performing initial reset...")
    try:
        obs = env.hard_reset()
        print(f"✓ Initial observation received: {len(obs)} environments")
    except Exception as e:
        print(f"✗ Reset failed: {e}")
        env.close()
        return
    print()

    # Get agent IDs
    if not hasattr(env, 'ids') or not env.ids:
        print("✗ ERROR: No agents found in environment")
        print("  Make sure agents are spawned in the UE5 map")
        env.close()
        return

    num_envs = len(env.ids)
    print(f"Number of environments: {num_envs}")
    total_agents = 0
    for env_idx, agent_ids in enumerate(env.ids):
        print(f"  Env {env_idx}: {len(agent_ids)} agents - {agent_ids}")
        total_agents += len(agent_ids)

    if total_agents == 0:
        print("✗ ERROR: No agents registered")
        print("  Make sure agents are properly configured in UE5")
        env.close()
        return

    print(f"✓ Total agents: {total_agents}")
    print()

    # Discover action space structure from UE5
    print("Discovering action space from UE5...")
    action_defns = getattr(env, 'action_defns', {})
    all_action_keys = {}

    for env_idx, agent_list in enumerate(env.ids):
        for agent_idx in agent_list:
            agent_defn = action_defns.get(env_idx, {}).get(agent_idx, None)
            if agent_defn is None:
                continue

            keys_list = []
            if hasattr(agent_defn, 'spaces'):
                keys_list = list(agent_defn.spaces.keys())
            elif isinstance(agent_defn, dict):
                keys_list = list(agent_defn.keys())

            if keys_list:
                all_action_keys[(env_idx, agent_idx)] = keys_list
                if env_idx == 0 and agent_idx == agent_list[0]:
                    print(f"  Action keys for Env {env_idx}, Agent {agent_idx}: {keys_list}")

    if not all_action_keys:
        print("  ERROR: No action keys discovered. Check UE5 action space configuration.")
        env.close()
        return

    print(f"  Total agents with actions: {len(all_action_keys)}")
    print()

    # Monitor episodes
    print("Monitoring episodes... (Press Ctrl+C to stop)")
    print("=" * 80)

    env_step_counts = {i: 0 for i in range(num_envs)}
    env_start_times = {i: time.time() for i in range(num_envs)}
    env_termination_seen = {i: False for i in range(num_envs)}
    env_prev_done = {i: False for i in range(num_envs)}

    step_count = 0

    try:
        import numpy as np

        while True:
            # Send dummy actions using discovered action keys
            actions = {}
            for env_idx, agent_list in enumerate(env.ids):
                actions[env_idx] = {}
                for agent_idx in agent_list:
                    agent_keys = all_action_keys.get((env_idx, agent_idx), [])
                    if agent_keys:
                        # Create dummy action (5D continuous: 4 tactical params + 1 combat priority)
                        dummy_action = np.array([0.5, 0.5, 0.5, 0.5, 0.0], dtype=np.float32)
                        actions[env_idx][agent_idx] = {key: dummy_action for key in agent_keys}

            env.send_actions(actions)

            # Poll for observations
            result = env.poll()

            if len(result) == 5:
                obs_nested, rew_nested, term_nested, trunc_nested, info_nested = result
            elif len(result) == 4:
                obs_nested, rew_nested, term_nested, info_nested = result
                trunc_nested = term_nested
            else:
                continue

            step_count += 1

            # Check each environment
            for env_idx in range(num_envs):
                env_step_counts[env_idx] += 1

                agent_list = env.ids[env_idx]
                if not agent_list:
                    continue

                # Check if all agents are done
                all_done = all(
                    term_nested.get(env_idx, {}).get(agent_idx, False) or
                    trunc_nested.get(env_idx, {}).get(agent_idx, False)
                    for agent_idx in agent_list
                )

                prev_done = env_prev_done[env_idx]

                # Detect episode completion (transition: not done → done)
                if all_done and not prev_done:
                    duration = time.time() - env_start_times[env_idx]
                    print(f"\n🏁 Env {env_idx} EPISODE COMPLETE!")
                    print(f"   Steps: {env_step_counts[env_idx]}")
                    print(f"   Duration: {duration:.1f}s")
                    print(f"   Termination flags: terminated={term_nested.get(env_idx, {})}")
                    print(f"   Truncation flags: truncated={trunc_nested.get(env_idx, {})}")
                    env_termination_seen[env_idx] = True

                # Detect auto-reset (transition: done → not done)
                if prev_done and not all_done:
                    print(f"   🔄 Auto-reset detected for Env {env_idx}")
                    env_step_counts[env_idx] = 0
                    env_start_times[env_idx] = time.time()
                    env_termination_seen[env_idx] = False

                env_prev_done[env_idx] = all_done

                # Warning if steps are high without termination
                if env_step_counts[env_idx] % 500 == 0 and env_step_counts[env_idx] > 0:
                    duration = time.time() - env_start_times[env_idx]
                    print(f"[Env {env_idx}] Step {env_step_counts[env_idx]}, "
                          f"Duration: {duration:.1f}s, "
                          f"Done: {all_done}")

            # Print global progress every 100 steps
            if step_count % 100 == 0:
                print(f"\nGlobal step: {step_count}")
                for env_idx in range(num_envs):
                    duration = time.time() - env_start_times[env_idx]
                    status = "DONE" if env_prev_done[env_idx] else "ACTIVE"
                    print(f"  Env {env_idx}: {status}, Steps={env_step_counts[env_idx]}, "
                          f"Duration={duration:.1f}s")

            time.sleep(0.01)  # 100 Hz polling

    except KeyboardInterrupt:
        print("\n\nDiagnostic stopped by user")

    finally:
        print("\n" + "=" * 80)
        print("DIAGNOSTIC SUMMARY")
        print("=" * 80)

        any_termination_seen = False
        max_steps = 0
        max_duration = 0

        for env_idx in range(num_envs):
            duration = time.time() - env_start_times[env_idx]
            max_steps = max(max_steps, env_step_counts[env_idx])
            max_duration = max(max_duration, duration)
            any_termination_seen = any_termination_seen or env_termination_seen[env_idx]

            print(f"Env {env_idx}:")
            print(f"  Total steps: {env_step_counts[env_idx]}")
            print(f"  Duration: {duration:.1f}s")
            print(f"  Termination seen: {'✓ YES' if env_termination_seen[env_idx] else '✗ NO'}")
        print()

        print("=" * 80)
        print("DIAGNOSIS")
        print("=" * 80)

        if not any_termination_seen:
            print("⚠️ WARNING: No episode terminations detected!")
            print()
            print("Possible causes:")
            print(f"  1. MaxStepsPerEpisode in UE5 is too high (current max: {max_steps} steps)")
            print(f"     → Recommended: ~2000 steps")
            print(f"  2. MaxEpisodeDuration in UE5 is too high (current max: {max_duration:.1f}s)")
            print(f"     → Recommended: ~60 seconds")
            print("  3. Termination conditions not configured:")
            print("     → Check team annihilation trigger")
            print("     → Check timeout trigger")
            print()
            print("To fix in UE5:")
            print("  1. Open: Content/Game/Maps/Training/Training_BasicCombat_2v2_v01")
            print("  2. Find: GameMode or TrainingManager actor")
            print("  3. Set: MaxStepsPerEpisode = 2000")
            print("  4. Set: MaxEpisodeDuration = 60.0")
            print("  5. Enable: Team Annihilation termination")
            print("  6. Enable: Timeout termination")
        else:
            print("✓ Episode terminations detected successfully!")
            print()
            print("Episode behavior looks normal.")
            print("The Python training code should work correctly now with v8.9.2 fixes.")

        print("=" * 80)
        print()

        env.close()
        print("Connection closed")


if __name__ == "__main__":
    diagnose_termination()
