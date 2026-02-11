"""
MOC v10.2 Training Quick Start Script

This script provides a simplified entry point for training the v10.2 command-driven policy.

Usage:
    # Start training with default settings
    python train_v10_2.py

    # Custom host and port
    python train_v10_2.py --host localhost --port 50051

    # Specify number of iterations
    python train_v10_2.py --iterations 200

    # Resume from checkpoint
    python train_v10_2.py --resume training_results_v10_2/v10_2_20260211_143022

Prerequisites:
    1. Install dependencies:
       pip install ray[rllib] torch schola gymnasium numpy

    2. Start UE5 with Schola plugin:
       - Open your UE5 project
       - Enable Schola plugin
       - Run in PIE mode (Play in Editor)

    3. Ensure UE5 is configured for v10.2:
       - Squad Commander assigns strategies (Assault/Defend/Support)
       - Agents receive 52-dim local observations
       - Action space accepts 8-dim EQS weights in [-1, 1]
"""

import sys
import os

# Add training directory to path
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

if __name__ == '__main__':
    from phase1_policy_training_v10_2 import train_with_rllib, MOCv10_2TrainingConfig
    import argparse

    parser = argparse.ArgumentParser(description="MOC v10.2 Quick Start Training")
    parser.add_argument("--host", type=str, default="localhost",
                       help="UE5 host address (default: localhost)")
    parser.add_argument("--port", type=int, default=50051,
                       help="UE5 gRPC port (default: 50051)")
    parser.add_argument("--iterations", type=int, default=100,
                       help="Number of training iterations (default: 100)")
    parser.add_argument("--checkpoint-freq", type=int, default=10,
                       help="Checkpoint frequency (default: 10)")
    parser.add_argument("--num-envs", type=int, default=4,
                       help="Number of parallel UE5 environments (default: 4)")
    parser.add_argument("--resume", type=str, default=None,
                       help="Path to checkpoint directory to resume from")

    args = parser.parse_args()

    # Update configuration
    MOCv10_2TrainingConfig.HOST = args.host
    MOCv10_2TrainingConfig.PORT = args.port
    MOCv10_2TrainingConfig.NUM_UE5_ENVIRONMENTS = args.num_envs

    print("""
    ╔══════════════════════════════════════════════════════════════════════════╗
    ║                                                                          ║
    ║                  MOC v10.2 Training - Quick Start                       ║
    ║                                                                          ║
    ║  Architecture: Command-Driven Executor                                  ║
    ║  Input: 52-dim local obs + 3 strategy commands                          ║
    ║  Output: 8-dim EQS weights in [-1, 1]                                   ║
    ║                                                                          ║
    ╚══════════════════════════════════════════════════════════════════════════╝
    """)

    print("Configuration:")
    print(f"  Host: {args.host}:{args.port}")
    print(f"  Environments: {args.num_envs}")
    print(f"  Iterations: {args.iterations}")
    print(f"  Checkpoint Frequency: every {args.checkpoint_freq} iterations")
    if args.resume:
        print(f"  Resume from: {args.resume}")
    print()

    try:
        output_dir = train_with_rllib(args)
        print(f"\n✅ Training completed successfully!")
        print(f"📁 Results saved to: {output_dir}")
        print(f"🎯 ONNX model: {os.path.join(output_dir, 'moc_policy_v10_2.onnx')}")

    except KeyboardInterrupt:
        print("\n\n⚠️ Training interrupted by user")
        sys.exit(0)
    except Exception as e:
        print(f"\n\n❌ Training failed: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
