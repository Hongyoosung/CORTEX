# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.


import pytest
from schola.core.utils.ubt import UBTCommand


def test_default_ubt_command_args():

    output_args = set(
        UBTCommand(
            ubt_path="UBTPATH",
            project_file="PROJECTFILE",
            target_platform="UNREAL_PLATFORM",
            staging_dir="STAGINGDIR",
            maps=["A", "B", "C"],
        ).build_args()
    )

    args = {
        "UBTPATH",
        "BuildCookRun",
        "-build",
        "-cook",
        "-FastCook",
        "-NoP4",
        "-prereqs",
        "-clean",
        "-nocompile",
        "-nocompileuat",
        "-package",
        "-project=PROJECTFILE",
        "-platform=UNREAL_PLATFORM",
        "-configuration=Development",
        "-nodebuginfo",  # Don't include debug info since we are going to be running headless
        "-unattended",  # Automated process so don't add popups
        "-stage",
        "-stagingdirectory=STAGINGDIR",  # Stage built files to a temporary directory
        "-ForceMonolithic",
        "-stdout",
        "-map=A+B+C",
    }

    assert (
        len(args - output_args) == 0
    ), f"Missing expected args in UBT Command: {args - output_args}"
    assert (
        len(output_args - args) == 0
    ), f"Unexpected extra args in UBT Command: {output_args - args}"


def test_all_maps_property():
    all_maps_command = UBTCommand(
        ubt_path="UBTPATH",
        project_file="PROJECTFILE",
        target_platform="UNREAL_PLATFORM",
        staging_dir="STAGINGDIR",
    )

    assert (
        all_maps_command.all_maps
    ), "all_maps property should be True when no maps are specified"

    # check that -AllMaps is included in build args when no maps are specified
    args = all_maps_command.build_args()
    assert (
        "-AllMaps" in args
    ), "-AllMaps should be included in UBT args when no maps are specified"


def test_specified_maps():
    # check that all_maps is false if we have specified maps
    command_with_maps = UBTCommand(
        ubt_path="UBTPATH",
        project_file="PROJECTFILE",
        target_platform="UNREAL_PLATFORM",
        staging_dir="STAGINGDIR",
        maps=["A", "B", "C"],
    )

    assert (
        not command_with_maps.all_maps
    ), "all_maps property should be False when maps are specified"

    # check that -map is included in build args when maps are specified
    args = command_with_maps.build_args()
    assert (
        "-map=A+B+C" in args
    ), "-map should be included in UBT args when maps are specified"
    assert (
        "-AllMaps" not in args
    ), "-AllMaps should not be included in UBT args when maps are specified"
