# Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.
"""
Scripts for training with Ray and RLlib.
"""

from cyclopts import App, Parameter, validators, group_extractors, Group

rllib_app= App(name="schola-rllib", help="Use Ray-Rllib with Schola!")

from .train import app as train_app

from .rllib_to_onnx import app as onnx_export_app

rllib_app.command(train_app, name="train")

rllib_app.command(onnx_export_app, name="export")


if __name__=="__main__":
    rllib_app()