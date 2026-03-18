# Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.

"""
A connection to an existing Unreal Editor running in a separate process.
"""

from schola.core.simulators.unreal.base import BaseUnrealSimulator


class UnrealEditor(BaseUnrealSimulator):
    """
    A connection to a running Unreal Editor instance.

    Raises
    ------
    AssertionError
        If the port is not supplied
    """

    def __init__(self):
        super().__init__()
    
    def __base__(self):
        return True 