# Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.

"""
Script to convert a Stable Baselines3 model to an ONNX model for use in Unreal Engine.
"""

from cyclopts import App, Parameter, validators, types
from pathlib import Path
from typing import Annotated

app = App(name="sb3-to-onnx", help="Convert a StableBaselines 3 policy to ONNX for Unreal Engine")

@app.default
def export(policy_checkpoint_path:types.ExistingFile, output_path:types.File):
    from schola.sb3.utils import convert_ckpt_to_onnx_for_unreal
    convert_ckpt_to_onnx_for_unreal(
        model_path=str(policy_checkpoint_path), export_path=str(output_path)
        )

if __name__ == "__main__":
    app()
