# Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.
"""
This script is used to compile .proto files into python/C++ Source Files for Schola.
"""
from pathlib import Path
import subprocess
import os
import logging
from os.path import isfile, join
import re
from typing import Annotated, Any, List

# Logging setup
if not logging.getLogger().handlers:
    logging.basicConfig(
        level=logging.INFO,
        format="%(levelname)s %(name)s: %(message)s",
    )
logger = logging.getLogger(__name__)


def get_files(folder):
    return [
        file_name for file_name in os.listdir(folder) if isfile(join(folder, file_name))
    ]


def get_proto_files(folder):
    return [
        file_name for file_name in get_files(folder) if file_name.endswith(".proto")
    ]


def get_generated_cpp_file_types(folder):
    files = get_files(folder)
    output = {
        "proto-header": [],
        "grpc-header": [],
        "proto-c": [],
        "grpc-c": [],
    }

    for file_name in files:

        if file_name.endswith(".grpc.pb.cc"):
            output["grpc-c"].append(file_name)
        elif file_name.endswith(".pb.cc"):
            output["proto-c"].append(file_name)
        elif file_name.endswith(".grpc.pb.h"):
            output["grpc-header"].append(file_name)
        elif file_name.endswith(".pb.h"):
            output["proto-header"].append(file_name)
    return output


def get_generated_python_file_types(folder):
    files = get_files(folder)
    output = {
        "proto": [],
        "grpc": [],
    }

    for file_name in files:
        if file_name.endswith("_pb2_grpc.py"):
            output["grpc"].append(file_name)
        elif file_name.endswith("_pb2.py"):
            output["proto"].append(file_name)

    return output


def fix_imports(folder):
    files = get_files(folder)
    import_pattern = "^import (.*) as (.*)"
    for file in files:
        with open(join(folder, file), "r+") as f:
            file_contents = f.readlines()
            for i, line in enumerate(file_contents):
                result = re.search(import_pattern, line)
                if result:
                    file_contents[i] = (
                        f"import schola.generated.{result.group(1)} as {result.group(2)}\n"
                    )
            f.seek(0)
            f.writelines(file_contents)
            f.truncate()


def disable_warnings(folder, file_paths, warnings):
    for file_path in file_paths:
        with open(join(folder, file_path), "r+") as f:
            file_contents = f.readlines()
            # files have two lines of headers to start
            try:
                headers_start_index = next(
                    (
                        line_num
                        for line_num, line in enumerate(file_contents)
                        if line.startswith("#include ")
                    )
                )
            except StopIteration:
                logger.warning("Could not find header insertion point in %s; using default index", file_path)
                headers_start_index = 3
            for warning in warnings:
                file_contents.insert(
                    headers_start_index, f"#pragma warning (disable : {warning})\n"
                )
            f.seek(0)
            f.writelines(file_contents)
            f.truncate()

def add_api_macro(folder, file_paths, api_macro):
    SERVICE_CLASS = "class Service : public ::grpc::Service {"
    END_OF_CLASS = "};"
    for file_path in file_paths:
        with open(join(folder, file_path), "r+") as f:
            file_contents = f.readlines()
            file_contents_cleaned = [line.rstrip().lstrip() for line in file_contents]  # remove trailing whitespace
            # TODO make this work for more than one service per file
            if file_contents_cleaned.count(SERVICE_CLASS) == 1:
                logger.info("Found Service in %s. Adding Macros to methods.", file_path)

                # find start and end of service class
                service_class_start = file_contents_cleaned.index(SERVICE_CLASS)
                service_class_end = file_contents_cleaned.index(END_OF_CLASS, service_class_start)

                # add the api macro to each virtual method in the service class, and the Constructor
                for i in range(service_class_start+1, service_class_end):
                     #replace lines that start with whitespace followed by 'virtual'or 'Service() with the same line but with the api_macro added after the whitespace
                     file_contents[i] = re.sub(r'^(\s+)(virtual .*|Service\(\);)', r'\1'+api_macro+r' \2', file_contents[i])

                f.seek(0)
                f.writelines(file_contents)
                f.truncate()
            elif file_contents_cleaned.count(SERVICE_CLASS) > 1:
                logger.warning("Multiple service classes found in %s; skipping API macro addition", file_path)


def move_files(src_folder:Path, files : List[str], target_folder: Path):
    # Move all files to the target_folder
    import shutil

    target_folder = Path(target_folder)
    try:
        target_folder.mkdir(parents=True, exist_ok=True)
    except Exception as e:
        logger.error("Could not create target folder %s: %s", target_folder, e)
        return

    for entry in files:
        src = src_folder / entry
        dest = target_folder / entry

        # If entry is not an existing path
        if not src.exists():
            logger.warning("Source file %s does not exist; skipping", src.absolute())
        elif src.resolve() == dest.resolve():
            logger.warning("Source and destination identical for %s; skipping", src)
        else:
            try:
                shutil.move(str(src), str(dest))
            except Exception as e:
                logger.error("Failed to move %s -> %s: %s", src, dest, e)


def make_proto_files(
    protoc_path, proto_folder, python_folder, cpp_folder, add_type_stubs=False, cpp_output_options=""
):
    args = []
    args += [f"-I={proto_folder}"]
    args += [f"--python_out={python_folder}"]
    args += [f"--cpp_out={cpp_output_options}:{cpp_folder}"]

    args += [f"--pyi_out={python_folder}"] if add_type_stubs else []

    for file in get_proto_files(proto_folder):
        subprocess.run([protoc_path] + args + [join(proto_folder, file)], check=True)


def make_grpc_files(protoc_path, proto_folder, plugin_path, target_folder,output_options=""):

    I_arg = f"-I={proto_folder}"
    grpc_out = f"--grpc_out={output_options}:{target_folder}"
    plugin = f"--plugin=protoc-gen-grpc={plugin_path}"

    for file in get_proto_files(proto_folder):
        subprocess.run(
            [protoc_path, I_arg, grpc_out, plugin, join(proto_folder, file)], check=True
        )


from cyclopts import App, Parameter, validators, group_extractors, Group

app = App(name="compile-proto", help="Compile Schola Protobuf files to python/c++!")


def default_warnings(value:Any):
    return ["4125", "4800"]

@app.default
def main(plugin_folder: Path = Path("."), warnings_to_disable: Annotated[List[str],Parameter(show_default=default_warnings)] = None, add_type_stubs: bool = True):
    """
    Compile Protobuf files for Schola

    Parameters
    ----------
    plugin_folder : Path
        Path to the root directory of Schola (i.e. Parent of the folder containing the protobuf files).
    warnings_to_disable : List[str]
        List of warnings to disable
    add_type_stubs: bool
       Generate additional .pyi type stubs for type linting in python.
    """
    if warnings_to_disable is None:
        warnings_to_disable = default_warnings(None)

    module_name = "ScholaProtobuf"

    plugin_folder = plugin_folder
    proto_folder = plugin_folder / "Proto"
    tools_path = plugin_folder / "Resources" / "tools"
    protoc_path = tools_path / "protoc.exe"
    python_plugin_path = tools_path / "grpc_python_plugin.exe"
    cpp_plugin_path = tools_path / "grpc_cpp_plugin.exe"
    cpp_code_folder = plugin_folder / "Source" / module_name / "Private"
    cpp_header_folder = plugin_folder / "Source" / module_name / "Public"
    python_code_folder = plugin_folder / "Resources" / "python" / "schola" / "generated"

    short_dep_path = r"Schola\Resources\Build\windows_dependencies.bat"

    api_macro = module_name.upper() + "_API"

    # Check if protoc_path exists
    if not protoc_path.exists():
        raise FileNotFoundError(
            f"Protoc Path {protoc_path} does not exist. Please run {short_dep_path} to generate this. Please note that Linux is not supported."
        )

    # Check if plugin paths exist
    if not python_plugin_path.exists():
        raise FileNotFoundError(
            f"Python Plugin Path {python_plugin_path} does not exist. Please run {short_dep_path} to generate this. Please note that Linux is not supported."
        )

    if not cpp_plugin_path.exists():
        raise FileNotFoundError(
            f"C++ Plugin Path {cpp_plugin_path} does not exist. Please run {short_dep_path} to generate this. Please note that Linux is not supported."
        )

    # generate protobuf files defining serialization for the messages
    logging.info("Generating protobuf files")
    make_proto_files(
        protoc_path, proto_folder, python_code_folder, cpp_code_folder, add_type_stubs, cpp_output_options=f"dllexport_decl={api_macro}"
    )

    # generate source for the various message services
    logging.info("Generating gRPC files")
    make_grpc_files(protoc_path, proto_folder, python_plugin_path, python_code_folder)
    make_grpc_files(protoc_path, proto_folder, cpp_plugin_path, cpp_code_folder)

    generated_cpp_files = get_generated_cpp_file_types(cpp_code_folder)
    generated_python_files = get_generated_python_file_types(python_code_folder)

    # need to disable safe to ignore warnings that would otherwise cause Unreal compilation errors
    logging.info("Removing Warnings from generated C++ files")
    disable_warnings(
        cpp_code_folder, generated_cpp_files["proto-c"], warnings_to_disable
    )
    logging.info(f"Adding UHT API Macro to grpc C++ files {generated_cpp_files['grpc-header']} (protobuf files get theirs from protoc already)")
    add_api_macro(cpp_code_folder, generated_cpp_files["grpc-header"], api_macro)

    #Note this will leave the generated_cpp_files dict out of date so any other operations need to be done first
    logging.info("Moving c++ headers to Public folder")
    move_files(cpp_code_folder, generated_cpp_files["proto-header"], cpp_header_folder)
    move_files(cpp_code_folder, generated_cpp_files["grpc-header"], cpp_header_folder)

    # generated code doesn't import correctly so we need to prepend Schola.generated._____
    fix_imports(python_code_folder)


if __name__ == "__main__":
    app()
