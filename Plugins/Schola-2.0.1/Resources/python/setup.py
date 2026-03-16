# Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.
from itertools import chain
from setuptools import setup, find_packages
import sys


def get_ray_deps():
    """
    Dependencies for running training with RLlib.

    * ray[rllib] is required to run training with RLlib.
    * ray[rllib]>=2.49 is the earliest version of RLlib supporting Gymnasium>=1.1.0, and 2.52 is the latest available version.
    """
    return ["ray[rllib]>=2.49, <2.53"]


def get_sb3_deps():
    """
    Dependencies for running training with Stable Baselines 3.
    
    * stable-baselines3 is required to run training with Stable Baselines 3.
    * tqdm and rich are required to display the training progress bar from Stable Baselines 3.
    """
    return ["stable-baselines3>=2.6", "tqdm", "rich"]

def get_docs_deps():
    """
    Dependencies for building documentation with Sphinx.

    * sphinx is required to build the documentation. <9.0 is required to avoid issues with sphinx-tabs. Once those are resolved we can remove this.
    * breathe is required to generate the C++ API documentation.
    * sphinx-book-theme is required to use the book theme for the documentation.
    * sphinx-tabs==3.4.7 is required to create tabs in the documentation.
    * sphinx-copybutton is required to add copy buttons to code blocks.
    """
    return ["sphinx<9.0", "breathe", "sphinx_book_theme", "sphinx-tabs==3.4.7", "sphinx-copybutton"]

def get_minari_deps():
    """
    Dependencies for collecting Minari datasets with Schola.
    
    * minari[hdf5,create] is required to collect Minari datasets with Schola. 0.5.2 is required for MultiBinary and MultiDiscrete spaces.
    """
    return ["minari[hdf5,create]>=0.5.2"]


def get_test_deps():
    return ["pytest", "pytest-timeout", "pytest-mock", "minigrid", "pettingzoo[butterfly]"]


def merge_deps(*dep_lists):
    return list(set(chain.from_iterable(dep_lists)))


def get_all_deps():
    return merge_deps(get_sb3_deps(), get_ray_deps(), get_minari_deps())


if __name__ == "__main__":
    # load readme
    desc = None
    with open("../../README.md", "rt") as readme:
        desc = readme.read()
    assert desc != None, "failed to load readme"

    setup(
        name="schola",
        version="2.0.1",
        python_requires=">=3.10, <3.13",
        author="Advanced Micro Devices, Inc.",
        author_email="alexcann@amd.com",
        packages=find_packages(),
        description="Schola is a toolkit/plugin for Unreal Engine that facilitates training agents using reinforcement learning frameworks.",
        long_description=desc,
        long_description_content_type="text/markdown",
        install_requires=[
            "protobuf>=3.20",
            "grpcio>=1.51.1",
            "onnx>=1.11, <1.16.2",
            "onnxscript",
            "gymnasium>=1.1.0",
            "backports.strenum; python_version<'3.11'",
            "cyclopts>=4.0"
        ],
        extras_require={
            "sb3": get_sb3_deps(),
            "rllib": get_ray_deps(),
            "minari": get_minari_deps(),
            "all": get_all_deps(),
            "docs": get_docs_deps(),
            "test": get_test_deps(),
        },
        entry_points={
            "console_scripts": [
                "schola = schola.scripts.launch:main"
            ]
        },
    )
