# Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.
"""
Scripts for training with Stable Baselines 3.
"""

from cyclopts import App, Parameter, validators, group_extractors, Group

sb3_app = App(name="schola-sb3", help="Use StableBaselines3 with Schola!")

from .train import app as train_app

from .sb3_to_onnx import app as onnx_export

sb3_app.command(train_app,name="train")

sb3_app.command(onnx_export, name="export")

if __name__=="__main__":
    sb3_app()