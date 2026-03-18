# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

from typing import Tuple
import torch as th


class ScholaModel(th.nn.Module):
    """
    A PyTorch Module that is compatible with Schola inference.

    """

    def __init__(
        self,
    ):
        super().__init__()

    def forward(self, x: th.Tensor, state) -> Tuple[th.Tensor, th.Tensor]:
        """
        Forward pass of the model.

        Parameters
        ----------
        x : th.Tensor
            The input tensor to the model.
        state : Any
            The current state of the model (e.g., hidden states for RNNs).

        Returns
        -------
        Tuple[th.Tensor, th.Tensor]
            A tuple containing:
            - The output tensor from the forward pass.
            - The updated state of the model.

        Raises
        ------
        NotImplementedError
            This method must be implemented in subclasses.
        """
        raise NotImplementedError("forward method must be implemented in subclass")

    def save_as_onnx(self, export_path: str, onnx_oppset: int = 17):
        """
        Export the model to ONNX format.

        Parameters
        ----------
        export_path : str
            The file path where the ONNX model will be saved.
        onnx_oppset : int, default=17
            The ONNX opset version to use for the export.

        Raises
        ------
        NotImplementedError
            This method must be implemented in subclasses.
        """
        raise NotImplementedError("save as ONNX method must be implemented in subclass")
