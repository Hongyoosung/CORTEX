"""
ONNX Export Utility for Multi-Head RL Policy

Exports trained PyTorch model to ONNX format for UE5 deployment.
Validates model schema and performs inference speed benchmarks.

Usage:
    python export_to_onnx.py --checkpoint path/to/checkpoint.pth --output policy_weights.onnx
"""

import sys
import os
import argparse
import time
import torch
import onnx
import onnxruntime as ort
import numpy as np
from pathlib import Path

# Add parent directory to path
sys.path.append(str(Path(__file__).parent.parent))

from models.multi_head_policy import MultiHeadRLPolicy


def load_checkpoint(checkpoint_path: str, device: str = 'cpu') -> MultiHeadRLPolicy:
    """
    Load trained policy from checkpoint.

    Args:
        checkpoint_path: Path to .pth checkpoint file
        device: Device to load model on

    Returns:
        Loaded policy model
    """
    if not os.path.exists(checkpoint_path):
        raise FileNotFoundError(f"Checkpoint not found: {checkpoint_path}")

    print(f"Loading checkpoint: {checkpoint_path}")

    checkpoint = torch.load(checkpoint_path, map_location=device)

    # Initialize model
    policy = MultiHeadRLPolicy()
    policy.load_state_dict(checkpoint['policy_state_dict'])
    policy.eval()

    print(f"✅ Checkpoint loaded successfully")
    print(f"   Training stats: {checkpoint.get('stats', {})}")

    return policy


def export_onnx(
    policy: MultiHeadRLPolicy,
    output_path: str,
    batch_size: int = 1,
    opset_version: int = 14
):
    """
    Export policy to ONNX format.

    Args:
        policy: Trained policy model
        output_path: Output .onnx file path
        batch_size: Batch size for export
        opset_version: ONNX opset version
    """
    print(f"\nExporting to ONNX...")
    print(f"  Output: {output_path}")
    print(f"  Batch size: {batch_size}")
    print(f"  Opset version: {opset_version}")

    # Create output directory
    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)

    # Export using built-in method
    policy.export_onnx(output_path, batch_size=batch_size)

    # Verify file exists
    if not os.path.exists(output_path):
        raise RuntimeError(f"ONNX export failed: {output_path} not created")

    file_size_mb = os.path.getsize(output_path) / (1024 * 1024)
    print(f"✅ ONNX export successful ({file_size_mb:.2f} MB)")


def validate_onnx_model(onnx_path: str):
    """
    Validate ONNX model schema and structure.

    Args:
        onnx_path: Path to .onnx file
    """
    print(f"\nValidating ONNX model: {onnx_path}")

    # Load ONNX model
    model = onnx.load(onnx_path)

    # Check validity
    try:
        onnx.checker.check_model(model)
        print("✅ ONNX model is valid")
    except Exception as e:
        print(f"❌ ONNX model validation failed: {e}")
        return False

    # Print model info
    print("\nModel Information:")
    print(f"  Producer: {model.producer_name}")
    print(f"  IR Version: {model.ir_version}")
    print(f"  Opset Version: {model.opset_import[0].version if model.opset_import else 'Unknown'}")

    print("\n  Inputs:")
    for input_tensor in model.graph.input:
        shape = [dim.dim_value for dim in input_tensor.type.tensor_type.shape.dim]
        print(f"    - {input_tensor.name}: {shape}")

    print("\n  Outputs:")
    for output_tensor in model.graph.output:
        shape = [dim.dim_value for dim in output_tensor.type.tensor_type.shape.dim]
        print(f"    - {output_tensor.name}: {shape}")

    return True


def benchmark_inference(onnx_path: str, num_runs: int = 1000):
    """
    Benchmark ONNX model inference speed.

    Args:
        onnx_path: Path to .onnx file
        num_runs: Number of inference runs for benchmarking
    """
    print(f"\nBenchmarking inference speed ({num_runs} runs)...")

    # Create ONNX Runtime session
    sess_options = ort.SessionOptions()
    sess_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL

    session = ort.InferenceSession(onnx_path, sess_options)

    # Get input/output names
    input_names = [input.name for input in session.get_inputs()]
    output_names = [output.name for output in session.get_outputs()]

    print(f"  Session created successfully")
    print(f"  Input names: {input_names}")
    print(f"  Output names: {output_names}")

    # Prepare dummy inputs
    dummy_state = np.random.randn(1, 52).astype(np.float32)
    dummy_option = np.array([0], dtype=np.int64)
    dummy_target = np.random.randn(1, 3).astype(np.float32)
    dummy_duration = np.random.randn(1, 1).astype(np.float32)

    inputs = {
        'state': dummy_state,
        'option_idx': dummy_option,
        'target_pos': dummy_target,
        'duration': dummy_duration
    }

    # Warmup
    for _ in range(10):
        _ = session.run(output_names, inputs)

    # Benchmark
    latencies = []
    for _ in range(num_runs):
        start = time.perf_counter()
        outputs = session.run(output_names, inputs)
        end = time.perf_counter()
        latencies.append((end - start) * 1000)  # Convert to ms

    # Statistics
    latencies = np.array(latencies)
    mean_latency = np.mean(latencies)
    std_latency = np.std(latencies)
    p50_latency = np.percentile(latencies, 50)
    p95_latency = np.percentile(latencies, 95)
    p99_latency = np.percentile(latencies, 99)

    print(f"\n  Inference Latency Statistics:")
    print(f"    Mean: {mean_latency:.3f} ms")
    print(f"    Std:  {std_latency:.3f} ms")
    print(f"    P50:  {p50_latency:.3f} ms")
    print(f"    P95:  {p95_latency:.3f} ms")
    print(f"    P99:  {p99_latency:.3f} ms")

    # Check against budget
    budget_ms = 2.0  # 2ms budget per v10.0Architecture.md
    if p95_latency < budget_ms:
        print(f"\n  ✅ Inference latency within budget ({budget_ms}ms)")
    else:
        print(f"\n  ⚠️ Inference latency exceeds budget ({p95_latency:.3f}ms > {budget_ms}ms)")

    # Test output
    print(f"\n  Sample Output (EQS Weights):")
    eqs_weights = outputs[0][0]
    weight_names = [
        'EnemyObjectiveProximity',
        'AllyObjectiveProximity',
        'CoverDensity',
        'EnemyVisibility',
        'AllyProximity',
        'CombatRange',
        'PickupProximity',
    ]
    for name, value in zip(weight_names, eqs_weights):
        print(f"    {name}: {value:.3f}")

    return mean_latency, p95_latency


def compare_pytorch_onnx(
    policy: MultiHeadRLPolicy,
    onnx_path: str,
    num_samples: int = 100
):
    """
    Compare PyTorch and ONNX outputs for consistency.

    Args:
        policy: PyTorch policy model
        onnx_path: Path to ONNX model
        num_samples: Number of test samples
    """
    print(f"\nComparing PyTorch vs ONNX outputs ({num_samples} samples)...")

    # Load ONNX model
    session = ort.InferenceSession(onnx_path)

    max_diff = 0.0
    mean_diff = 0.0

    policy.eval()
    with torch.no_grad():
        for _ in range(num_samples):
            # Generate random input
            state = torch.randn(1, 52)
            option_idx = torch.randint(0, 5, (1,))
            target = torch.randn(1, 3)
            duration = torch.rand(1, 1) * 15.0 + 5.0

            # PyTorch inference
            pytorch_output = policy(state, option_idx, target, duration).numpy()

            # ONNX inference
            onnx_inputs = {
                'state': state.numpy().astype(np.float32),
                'option_idx': option_idx.numpy().astype(np.int64),
                'target_pos': target.numpy().astype(np.float32),
                'duration': duration.numpy().astype(np.float32)
            }
            onnx_output = session.run(None, onnx_inputs)[0]

            # Compute difference
            diff = np.abs(pytorch_output - onnx_output).max()
            max_diff = max(max_diff, diff)
            mean_diff += diff

    mean_diff /= num_samples

    print(f"  Max difference: {max_diff:.6f}")
    print(f"  Mean difference: {mean_diff:.6f}")

    tolerance = 1e-4
    if max_diff < tolerance:
        print(f"  ✅ PyTorch and ONNX outputs match (tolerance: {tolerance})")
    else:
        print(f"  ⚠️ PyTorch and ONNX outputs differ (max diff: {max_diff})")


def main():
    parser = argparse.ArgumentParser(description='Export Multi-Head Policy to ONNX')
    parser.add_argument('--checkpoint', type=str, required=True, help='Path to trained checkpoint (.pth)')
    parser.add_argument('--output', type=str, default='policy_weights.onnx', help='Output ONNX file path')
    parser.add_argument('--batch-size', type=int, default=1, help='Batch size for export')
    parser.add_argument('--opset', type=int, default=14, help='ONNX opset version')
    parser.add_argument('--validate', action='store_true', help='Validate ONNX model')
    parser.add_argument('--benchmark', action='store_true', help='Benchmark inference speed')
    parser.add_argument('--compare', action='store_true', help='Compare PyTorch vs ONNX')
    parser.add_argument('--device', type=str, default='cpu', help='Device (cpu/cuda)')

    args = parser.parse_args()

    print("="*80)
    print("ONNX Export Utility - Multi-Head RL Policy")
    print("="*80)

    try:
        # Load checkpoint
        policy = load_checkpoint(args.checkpoint, device=args.device)

        # Export to ONNX
        export_onnx(policy, args.output, batch_size=args.batch_size, opset_version=args.opset)

        # Validation
        if args.validate:
            validate_onnx_model(args.output)

        # Benchmark
        if args.benchmark:
            benchmark_inference(args.output)

        # Comparison
        if args.compare:
            compare_pytorch_onnx(policy, args.output)

        print("\n" + "="*80)
        print("✅ Export completed successfully!")
        print("="*80)
        print(f"\nONNX model ready for UE5 deployment: {args.output}")
        print("\nNext steps:")
        print("  1. Copy .onnx file to UE5 Content/AI/Models/")
        print("  2. Update UMultiHeadPolicyExecutor::PolicyModelPath")
        print("  3. Test inference in UE5 using GetEQSWeights()")

    except Exception as e:
        print(f"\n❌ Export failed: {e}")
        import traceback
        traceback.print_exc()
        return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
