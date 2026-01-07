#!/usr/bin/env python3
"""
Sync training environment config from C++ RL/RLTypes.h
Run before training to ensure Python and C++ environments match

This script parses the RLConfig namespace from C++ header files and
generates a Python configuration file. This ensures that training
environment parameters match the runtime parameters exactly, preventing
sim2real gaps that would cause trained models to fail in-game.

Usage:
    python tools/sync_config_from_cpp.py

Requirements:
    - C++ header: Source/GameAI_Project/Public/RL/RLTypes.h
    - Output: training_env/config.py
"""

import re
import os
import sys
from pathlib import Path
from datetime import datetime


def parse_cpp_constants(header_path):
    """
    Extract constexpr values from RLConfig namespace in C++ header

    Args:
        header_path: Path to RLTypes.h

    Returns:
        List of tuples: [(type, name, value), ...]
    """
    with open(header_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Extract RLConfig namespace block
    namespace_pattern = r'namespace RLConfig\s*\{(.*?)\n\}'
    namespace_match = re.search(namespace_pattern, content, re.DOTALL)

    if not namespace_match:
        raise ValueError("RLConfig namespace not found in header file")

    namespace_content = namespace_match.group(1)

    # Extract constexpr declarations
    # Pattern: constexpr TYPE NAME = VALUE;
    const_pattern = r'constexpr\s+(\w+)\s+(\w+)\s*=\s*([^;]+);'
    constants = re.findall(const_pattern, namespace_content)

    if not constants:
        raise ValueError("No constexpr constants found in RLConfig namespace")

    return constants


def generate_python_config(constants, output_path):
    """
    Generate Python config file from C++ constants

    Args:
        constants: List of (type, name, value) tuples
        output_path: Output file path
    """
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    with open(output_path, 'w', encoding='utf-8') as f:
        # Header
        f.write('# AUTO-GENERATED FROM C++ RL/RLTypes.h - DO NOT EDIT MANUALLY\n')
        f.write('# Run: python tools/sync_config_from_cpp.py\n')
        f.write(f'# Last synced: {datetime.now().isoformat()}\n\n')

        # Docstring
        f.write('class RLConfig:\n')
        f.write('    """RL training configuration (synced from C++)\n\n')
        f.write('    CRITICAL: Values must match C++ RLConfig namespace exactly.\n')
        f.write('    Any drift will cause trained models to fail in-game.\n')
        f.write('    """\n\n')

        # Constants
        for type_, name, value in constants:
            # Clean up value (remove 'f' suffix from floats, handle comments)
            value_clean = value.strip()

            # Remove inline comments
            if '//' in value_clean:
                value_clean = value_clean.split('//')[0].strip()

            # Remove 'f' suffix from floats
            value_clean = value_clean.rstrip('f')

            # Convert C++ types to Python
            if type_ == 'bool':
                value_clean = 'True' if 'true' in value_clean.lower() else 'False'
            elif type_ == 'int32':
                # Ensure it's an integer
                try:
                    value_clean = str(int(float(value_clean)))
                except ValueError:
                    print(f"Warning: Could not parse {name} = {value_clean} as int32, using as-is")

            f.write(f'    {name} = {value_clean}\n')

        f.write('\n')

    print(f'[SUCCESS] Config synced to: {output_path}')


def verify_sync(cpp_header, python_config):
    """
    Verify Python config matches C++ header

    Args:
        cpp_header: Path to C++ header
        python_config: Path to Python config

    Returns:
        List of mismatch messages (empty if all match)
    """
    constants = parse_cpp_constants(cpp_header)

    # Import Python config
    config_dir = Path(python_config).parent
    sys.path.insert(0, str(config_dir))

    try:
        # Import the config module
        import importlib.util
        spec = importlib.util.spec_from_file_location("config", python_config)
        config_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(config_module)
        RLConfig = config_module.RLConfig
    except Exception as e:
        return [f'Failed to import Python config: {e}']

    mismatches = []
    for type_, name, value in constants:
        if not hasattr(RLConfig, name):
            mismatches.append(f'Missing in Python: {name}')
            continue

        py_value = getattr(RLConfig, name)

        # Parse C++ value
        cpp_value_str = value.strip().rstrip('f')
        if '//' in cpp_value_str:
            cpp_value_str = cpp_value_str.split('//')[0].strip()

        try:
            if type_ == 'int32':
                cpp_value = int(float(cpp_value_str))
                if py_value != cpp_value:
                    mismatches.append(f'{name}: Python={py_value}, C++={cpp_value}')
            elif type_ == 'float':
                cpp_value = float(cpp_value_str)
                if abs(py_value - cpp_value) > 0.001:
                    mismatches.append(f'{name}: Python={py_value}, C++={cpp_value}')
            elif type_ == 'bool':
                cpp_value = 'true' in cpp_value_str.lower()
                if py_value != cpp_value:
                    mismatches.append(f'{name}: Python={py_value}, C++={cpp_value}')
        except ValueError as e:
            mismatches.append(f'{name}: Could not parse C++ value "{cpp_value_str}": {e}')

    return mismatches


def main():
    """Main entry point"""
    # Paths
    script_dir = Path(__file__).parent
    project_root = script_dir.parent.parent
    cpp_header = project_root / 'Source' / 'GameAI_Project' / 'Public' / 'RL' / 'RLTypes.h'
    python_config = script_dir.parent / 'training_env' / 'config.py'

    # Verify C++ header exists
    if not cpp_header.exists():
        print(f'[ERROR] C++ header not found at {cpp_header}')
        print(f'   Please ensure you are running this script from the correct directory.')
        return 1

    # Parse and generate
    print(f'[INFO] Parsing C++ header: {cpp_header}')
    try:
        constants = parse_cpp_constants(str(cpp_header))
        print(f'   Found {len(constants)} constants')
    except ValueError as e:
        print(f'[ERROR] {e}')
        return 1

    print(f'[INFO] Generating Python config: {python_config}')
    generate_python_config(constants, str(python_config))

    # Verify
    print(f'[INFO] Verifying sync...')
    mismatches = verify_sync(str(cpp_header), str(python_config))

    if mismatches:
        print('[ERROR] SYNC VERIFICATION FAILED:')
        for mismatch in mismatches:
            print(f'   - {mismatch}')
        return 1
    else:
        print('[SUCCESS] Sync verified successfully!')
        print('')
        print('[SUMMARY]')
        print(f'   - Constants synced: {len(constants)}')
        print(f'   - Output: {python_config.relative_to(project_root)}')
        print('')
        print('[WARNING] IMPORTANT: Run this script before training to prevent sim2real drift!')
        return 0


if __name__ == '__main__':
    sys.exit(main())
