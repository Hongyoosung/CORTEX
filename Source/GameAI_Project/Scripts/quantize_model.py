"""
ONNX Model Quantization Script

Quantizes FP32 ONNX models to INT8 for 2-3x faster inference.
Uses dynamic quantization (no calibration data needed).

Usage:
    python quantize_model.py --input rl_policy_network.onnx --output rl_policy_network_int8.onnx

Requirements:
    pip install onnx onnxruntime onnxruntime-extensions
"""

import argparse
import os
from pathlib import Path

try:
    import onnx
    from onnxruntime.quantization import quantize_dynamic, QuantType
    QUANTIZATION_AVAILABLE = True
except ImportError as e:
    QUANTIZATION_AVAILABLE = False
    print(f"Error: onnxruntime quantization not available: {e}")
    print("Install with: pip install onnxruntime onnx")


def quantize_model(input_path: str, output_path: str, quantization_mode: str = "dynamic"):
    """
    Quantize ONNX model to INT8.

    Args:
        input_path: Path to FP32 ONNX model
        output_path: Path to save INT8 ONNX model
        quantization_mode: "dynamic" (default) or "static"
    """
    if not QUANTIZATION_AVAILABLE:
        raise RuntimeError("ONNX quantization not available. Install onnxruntime.")

    if not os.path.exists(input_path):
        raise FileNotFoundError(f"Input model not found: {input_path}")

    print(f"[QUANTIZATION] Loading FP32 model: {input_path}")

    # Load original model to get size
    model = onnx.load(input_path)
    original_size = os.path.getsize(input_path) / (1024 * 1024)  # MB
    print(f"[QUANTIZATION] Original model size: {original_size:.2f} MB")

    if quantization_mode == "dynamic":
        print("[QUANTIZATION] Using dynamic quantization (no calibration data needed)...")

        # Dynamic quantization: quantize weights to INT8, activations computed in FP32
        # Best for models where inference is compute-bound (not memory-bound)
        quantize_dynamic(
            model_input=input_path,
            model_output=output_path,
            weight_type=QuantType.QInt8,  # Quantize weights to INT8
            optimize_model=True,  # Apply optimizations (layer fusion, etc.)
            extra_options={
                'EnableSubgraph': True,  # Enable subgraph optimization
                'ForceSymmetricInt8Weights': False,  # Asymmetric quantization for better accuracy
            }
        )

    elif quantization_mode == "static":
        # Static quantization requires calibration data
        # More accurate but more complex setup
        raise NotImplementedError("Static quantization requires calibration data. Use dynamic mode.")

    else:
        raise ValueError(f"Unknown quantization mode: {quantization_mode}")

    # Get quantized model size
    quantized_size = os.path.getsize(output_path) / (1024 * 1024)  # MB
    compression_ratio = original_size / quantized_size

    print(f"[QUANTIZATION] Quantized model size: {quantized_size:.2f} MB")
    print(f"[QUANTIZATION] Compression ratio: {compression_ratio:.2f}x")
    print(f"[QUANTIZATION] Size reduction: {100 * (1 - quantized_size/original_size):.1f}%")
    print(f"[QUANTIZATION] ✅ Quantized model saved to: {output_path}")

    # Validate quantized model
    print("[QUANTIZATION] Validating quantized model...")
    quantized_model = onnx.load(output_path)
    onnx.checker.check_model(quantized_model)
    print("[QUANTIZATION] ✅ Quantized model is valid")

    return output_path


def benchmark_inference_speed(fp32_model: str, int8_model: str, num_runs: int = 100):
    """
    Benchmark inference speed for FP32 vs INT8 models.

    Args:
        fp32_model: Path to FP32 ONNX model
        int8_model: Path to INT8 ONNX model
        num_runs: Number of inference runs for benchmarking
    """
    try:
        import onnxruntime as ort
        import numpy as np
        import time
    except ImportError:
        print("[BENCHMARK] Skipping benchmark (onnxruntime not available)")
        return

    print(f"\n[BENCHMARK] Running inference benchmark ({num_runs} runs)...")

    # Create dummy input (78 features)
    dummy_input = np.random.randn(1, 78).astype(np.float32)

    # Benchmark FP32
    print("[BENCHMARK] Testing FP32 model...")
    session_fp32 = ort.InferenceSession(fp32_model, providers=['CPUExecutionProvider'])
    input_name = session_fp32.get_inputs()[0].name

    start_time = time.perf_counter()
    for _ in range(num_runs):
        session_fp32.run(None, {input_name: dummy_input})
    fp32_time = (time.perf_counter() - start_time) / num_runs * 1000  # ms

    # Benchmark INT8
    print("[BENCHMARK] Testing INT8 model...")
    session_int8 = ort.InferenceSession(int8_model, providers=['CPUExecutionProvider'])

    start_time = time.perf_counter()
    for _ in range(num_runs):
        session_int8.run(None, {input_name: dummy_input})
    int8_time = (time.perf_counter() - start_time) / num_runs * 1000  # ms

    # Results
    speedup = fp32_time / int8_time
    print(f"\n[BENCHMARK] Results (average over {num_runs} runs):")
    print(f"  FP32:  {fp32_time:.3f} ms/inference")
    print(f"  INT8:  {int8_time:.3f} ms/inference")
    print(f"  Speedup: {speedup:.2f}x faster")

    if speedup > 1.5:
        print(f"  ✅ Significant speedup! Use INT8 model in production.")
    else:
        print(f"  ⚠️  Modest speedup. Consider CPU architecture (AVX512 for best INT8 performance).")


def main():
    parser = argparse.ArgumentParser(description="Quantize ONNX model to INT8")
    parser.add_argument("--input", type=str, required=True,
                        help="Path to input FP32 ONNX model")
    parser.add_argument("--output", type=str, default=None,
                        help="Path to output INT8 ONNX model (default: <input>_int8.onnx)")
    parser.add_argument("--mode", type=str, default="dynamic", choices=["dynamic", "static"],
                        help="Quantization mode (default: dynamic)")
    parser.add_argument("--benchmark", action="store_true",
                        help="Run inference speed benchmark")
    parser.add_argument("--benchmark-runs", type=int, default=100,
                        help="Number of benchmark runs (default: 100)")

    args = parser.parse_args()

    # Default output path
    if args.output is None:
        input_path = Path(args.input)
        args.output = str(input_path.with_name(f"{input_path.stem}_int8{input_path.suffix}"))

    # Quantize model
    try:
        quantized_path = quantize_model(args.input, args.output, args.mode)

        # Benchmark if requested
        if args.benchmark:
            benchmark_inference_speed(args.input, quantized_path, args.benchmark_runs)

        print("\n✅ Quantization complete!")
        print(f"\nTo use in Unreal Engine:")
        print(f"  1. Copy {args.output} to Content/Models/")
        print(f"  2. Update RLPolicyNetwork to load INT8 model")
        print(f"  3. Expected: 2-3x faster inference, 4x smaller file size")

    except Exception as e:
        print(f"\n❌ Quantization failed: {e}")
        import traceback
        traceback.print_exc()
        return 1

    return 0


if __name__ == "__main__":
    exit(main())
