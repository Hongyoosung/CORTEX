# Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.

"""
Script to convert a Ray model to an ONNX model for use in Unreal Engine.
"""

from cyclopts import App, types

app= App(name="rllib-to-onnx", help="Convert a Ray RLlib policy to ONNX for Unreal Engine")

@app.default
def export(policy_checkpoint_path:types.ExistingFile, output_path:types.File):
    from ray.rllib.policy.policy import Policy
    from schola.rllib.utils import export_onnx_from_policy
    policy = Policy.from_checkpoint(policy_checkpoint_path)
    export_onnx_from_policy(policy, output_path)


        
