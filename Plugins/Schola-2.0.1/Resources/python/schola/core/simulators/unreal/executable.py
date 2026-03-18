# Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.

"""
A connection that launches an Environment in a new Process.
"""
import logging
import os
from pathlib import Path
import subprocess
import sys
from typing import Any, Dict, List, Optional, Union
from schola.core.simulators.unreal.base import BaseUnrealSimulator

PROCESS_KILL_TIMEOUT=1

class UnrealExecutable(BaseUnrealSimulator):
    """
    A standalone client that launches an Unreal Engine instance when started.

    Parameters
    ----------
    url : str
        The URL to connect to
    ue_path : str
        The path to the Unreal Engine executable
    headless_mode : bool
        Whether to run in headless mode
    map : str, optional
        The map to load. Defaults to the default map in the Unreal Engine project
    display_logs : bool, default=True
        Whether to display logs
    set_fps : int, optional
        Use a fixed fps while running, if None, no fixed timestep is used
    disable_script : bool, default=False
        Whether to disable the autolaunch script setting in the Unreal Engine Schola Plugin

    Attributes
    ----------
    executable_path : str
        The path to the Unreal Engine executable
    headless_mode : bool, default=False
        Whether to run in headless mode
    display_logs : bool
        Whether to display logs
    set_fps : int, optional
        Use a fixed fps while running, if None, no fixed timestep is used
    env_process : subprocess.Popen, optional
        The process running the Unreal Engine. None if the process is not running
    disable_script : bool
        Whether to disable the autolaunch script setting in the Unreal Engine Schola Plugin
    map : str, optional
        The map to load.  Defaults to the default map in the Unreal Engine project
    """

    def __init__(
        self,
        executable_path: Union[str, Path],
        headless_mode: bool = False,
        map: str = None,
        display_logs: bool = True,
        set_fps: Optional[int] = None,
        disable_script: bool = True,
    ):
        if isinstance(executable_path, str):
            executable_path = Path(executable_path)
        
        if not executable_path.exists() or not executable_path.is_file():
            raise FileNotFoundError(f"executable_path {executable_path} does not exist or is not a file")
        self.executable_path = executable_path
        self.headless_mode = headless_mode
        self.display_logs = display_logs
        self.set_fps = set_fps
        self.env_process = None
        self.disable_script = disable_script
        # Note any maps we want to use here need to be added to the build via Project Settings>Packaging>Advanced> List of Maps...
        # or on the command line with the -Map flag for UnrealAutomationTool
        self.map = map

    def make_args(self) -> List[str]:
        """
        Make the arguments supplied to the Unreal Engine Executable.

        Returns
        -------
        List[str]
            The arguments to be supplied to the Unreal Engine Executable
        """
        
        args = [self.executable_path]

        args.append("-UNATTENDED")
        if self.headless_mode:
            args += ["-nullRHI"]
        else:
            args += ["-WINDOWED"]

        if self.map:
            args += [self.map]
        if self.display_logs:
            args += ["-LOG"]
        if not self.set_fps is None:
            args += ["-BENCHMARK"]
            args += ["-FPS=" + str(self.set_fps)]
        if self.disable_script:
            args += ["-ScholaDisableScript"]
        return args

    def start(self, protocol_properties: Dict[str, Any]) -> None:
        """
        Start the Unreal Engine process.

        Raises
        ------
        Exception
            If the subprocess is already running
        """
        if self.env_process != None:
            raise Exception("Subprocess already running")
        # don't need to call super().start(...) since no parent implementation
        args = self.make_args()
        
        for key, value in protocol_properties.items():
            args += [f"-Schola{key}={value}"]
        
        self.env_process = subprocess.Popen(args)
        logging.info(f"Executable launched with PID: {self.env_process.pid}")

    def stop(self) -> None:
        """
        Close the connection to the Unreal Engine. Kills the Unreal Engine process if it is running.
        """
        super().stop()
        PROCESS_KILL_TIMEOUT=1

        if self.env_process != None:
            logging.debug("Killing subprocess")
            self.env_process.kill()
            try:
                self.env_process.wait(timeout=PROCESS_KILL_TIMEOUT)
            except subprocess.TimeoutExpired:
                if self.env_process.poll() is None:
                    logging.warning(
                        "Subprocess.kill() failed, forcibly killing subprocess"
                    )
                    if sys.platform.startswith("win"):
                        subprocess.run(
                            f"TASKKILL /F /PID {self.env_process.pid} /T", check=False
                        )
                    else:
                        subprocess.run(
                            ["kill", "-9", str(self.env_process.pid)], check=False
                        )

    @property
    def __bool__(self) -> bool:
        # We have a process, and it hasn't completed yet
        return ((not self.env_process is None) and (self.env_process.poll() is None))


    