# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

from datetime import datetime
import json
import subprocess
from typing import Callable, List, Literal, TypedDict
from dataclasses import dataclass
import sys
import os

import pytest
import pathlib
import re
from functools import cache
from typing import Dict, List, Optional, Any, Tuple

from schola.core.utils.ubt import (
    get_project_file,
    get_editor_executable_path,
    get_ue_version,
    get_ubt_path,
    build_executable,
)


# Constants
MAX_SEARCH_DEPTH = 10
CPP_EXTENSIONS = {".cpp", ".cc"}
AUTOMATION_TEST_PATTERN = r'\w+_AUTOMATION_TEST\s*\([^,]+,\s*"([^"]+)"'


@dataclass
class UnrealTestEvent:
    message: str
    filename: str
    line_number: int
    timestamp: str

    @classmethod
    def make(cls,event_entry):
        return cls(
            message=event_entry["event"].get("message", ""),
            filename=event_entry.get("filename", ""),
            line_number=event_entry.get("lineNumber", 0),
            timestamp=event_entry.get("timestamp", datetime.now().isoformat()),
        )
    
    @classmethod
    def make_from_item(cls, item: "UnrealTestItem", msg: str):
        return cls(
            message=msg,
            filename=item.cpp_file,
            line_number=item.lineno,
            timestamp=datetime.now().isoformat(),
        )

@dataclass
class UnrealTestResult:
    name: str
    test_path: str
    outcome: Literal["passed", "failed", "skipped"]
    duration: float
    errors: List[UnrealTestEvent]
    warnings: List[UnrealTestEvent]

    @property
    def num_warnings(self) -> bool:
        return len(self.warnings)
    
    @property
    def num_errors(self) -> bool:
        return len(self.errors)

    @property
    def passed(self) -> bool:
        return self.outcome == "passed"
    
    @property
    def failed(self) -> bool:
        return self.outcome == "failed"
    
    @property
    def skipped(self) -> bool:
        return self.outcome == "skipped"

    @property
    def sanitized_test_path(self) -> str:
        return self.test_path.strip()

    @classmethod
    def make(cls,test_results):
        
        entries = filter(lambda x: "event" in x, test_results.get("entries", []))
        errors = filter(lambda x: x["event"].get("type", "").lower() == "error", entries)
        warnings = filter(lambda x: x["event"].get("type", "").lower() == "warning", entries)
        
        duration = float(test_results.get("duration", 0))
        
        outcome = test_results.get("state", "").strip().lower()
        if outcome == "success":
            outcome = "passed"
        elif outcome == "fail":
            outcome = "failed"
        else:
            outcome = "skipped"
        # hard fail if we don't have a valid display name or full test path
        if not test_results["testDisplayName"] or not test_results["fullTestPath"]:
            raise Exception(f"Invalid test results: {test_results}. Test display name or full test path is missing.")
        
        return cls(
            name=test_results["testDisplayName"],
            test_path=test_results["fullTestPath"],
            outcome=outcome,
            duration=test_results.get("duration", 0),
            errors=[UnrealTestEvent.make(entry) for entry in errors],
            warnings=[UnrealTestEvent.make(entry) for entry in warnings],
        )
    

def run_unreal_tests(
    editor_path, uproject_path, report_path: pathlib.Path, tests_to_run
):
    """Run Unreal automation tests using a simple blocking subprocess call."""

    timeout = int(os.getenv("SCHOLA_UNREAL_TIMEOUT", "600"))  # seconds

    tests_to_run_cmd = f"-ExecCmds=Automation RunTest {tests_to_run};Quit"
    
    command = [
        str(editor_path),
        f"-project={uproject_path}",
        "-unattended",
        "-nullrhi",
        "-NoTrace",
        f"-ReportExportPath={report_path}",
        "-log",
        f"{tests_to_run_cmd}",
    ]

    print(f"Running Unreal Test Command: {' '.join(command)}")

    try:
        result = subprocess.run(
            command,
            timeout=timeout,
            capture_output=True,
        )
        
        stdout_str = result.stdout.decode("utf-8", errors="replace")
        stderr_str = result.stderr.decode("utf-8", errors="replace")
        
        # 0 is success, 255 is some test came back as failed but the overall test run was successful
        if result.returncode != 0 and result.returncode != 255:
            raise Exception(f"Unreal Automation Test failed with error {result.returncode}\nstdout: {stdout_str}\nstderr: {stderr_str}")
            
        print(f"Unreal Automation Test Results Saved To: {report_path}")
        
    except subprocess.TimeoutExpired:
        raise Exception(f"Unreal Automation Test timed out after {timeout} seconds.")


def load_unreal_test_results(report_path: pathlib.Path) -> Dict[str, UnrealTestResult]:
    report_file = report_path / "index.json"
    if not report_file.resolve().exists():
        raise FileNotFoundError(f"Expected Report file at {report_path} but no file found. This can sometimes mean Unreal failed to build correctly.")
    with open(report_file, "r", encoding="utf-8-sig") as f:
        data = json.load(f)
    test_results = {}
    for test in data["tests"]:
        test_result = UnrealTestResult.make(test)
        test_results[test_result.sanitized_test_path] = test_result
    return test_results

def get_uproject_file(project_dir: pathlib.Path) -> Optional[pathlib.Path]:
    uproject_file = get_project_file(project_dir)
    return uproject_file.resolve() if uproject_file else None

def get_engine_path_from_config(config) -> pathlib.Path:
    engine_path = config.getoption("--engine-path") or config.getini("engine_path")
    return pathlib.Path(engine_path)

def get_build_unreal_from_config(config) -> bool:
    # --no-build-unreal takes precedence
    if config.getoption("--no-build-unreal"):
        return False
    build_option = config.getoption("--build-unreal")
    if build_option is not None:
        return build_option
    return config.getini("build_unreal")

def pytest_addoption(parser):
    try:
        parser.addoption(
            "--engine-path",
            action="store",
            default=None,
            help="The absolute path to the Unreal Engine installation",
        )
    except ValueError:
        pass  # Option already exists

    try:
        parser.addini(
            "engine_path",
            help="The path to the Unreal Engine installation",
            default="C:/Program Files/Epic Games/UE_5.6",
        )
    except ValueError:
        pass  # Exists in .ini

    try:
        parser.addoption(
            "--build-unreal",
            action="store_true",
            default=None,
            help="Build Unreal project before running tests",
        )
    except ValueError:
        pass  # Option already exists

    try:
        parser.addoption(
            "--no-build-unreal",
            action="store_true",
            default=False,
            help="Skip building Unreal project before running tests",
        )
    except ValueError:
        pass  # Option already exists

    try:
        parser.addini(
            "build_unreal",
            type="bool",
            help="Build Unreal project before running tests",
            default=False,
        )
    except ValueError:
        pass  # Exists in .ini

@pytest.fixture(scope="session")
def unreal_path(request) -> pathlib.Path:
    return get_engine_path_from_config(request.config)


@pytest.fixture(scope="session")
def unreal_report_dir(tmp_path_factory) -> pathlib.Path:
    """Create a session-scoped temporary directory for Unreal test reports."""
    report_dir = tmp_path_factory.mktemp("unreal_test_reports")
    return report_dir


@pytest.hookimpl(tryfirst=True)
def pytest_sessionstart(session):
    """Set up session-level test report directory."""
    # Use pytest's temp path factory to create a session temp directory
    tmp_path_factory = session.config._tmp_path_factory
    report_dir = tmp_path_factory.mktemp("unreal_test_reports")
    session.config._unreal_report_dir = report_dir


class UnrealTestFile(pytest.File):

    def collect(self):
        # Skip C++ test collection on Linux
        if sys.platform != "win32":
            return
            
        test_details = []
        
        with open(self.path, "r", encoding="utf-8", errors="ignore") as f:
            for i,line in enumerate(f.readlines()):
                found_tests = re.findall(AUTOMATION_TEST_PATTERN, line)
                # If we found any tests, add them to the list
                if len(found_tests) > 0:
                    test_details += zip(found_tests, [i] * len(found_tests))

        for test_path, line_number in test_details:
            name = test_path.split(".")[-1] # Use the last part of the full path
            yield UnrealTestItem.from_parent(self, name=name, test_path=test_path, cpp_file=self.path, line_number=line_number)


class UnrealTestItem(pytest.Item):

    def __init__(self, name:str, test_path:str, cpp_file:pathlib.Path=None, line_number:int=0, **kwargs):
        name = name.strip()
        super().__init__(name=name,**kwargs)
        self.unreal_result = None  # Will be populated by pytest_runtestloop
        self.cpp_file = cpp_file
        self.lineno = line_number 
        self.test_path = test_path

    @property
    def sanitized_test_path(self) -> str:
        return self.test_path.strip()

    def runtest(self):
        if self.unreal_result is None:
            pytest.skip(
                "Unreal Engine did not execute this test. "
                "Verify --engine-path, --test-hierarchy-path, and any test filters."
            )

        else:
            self._handle_test_outcome(self.unreal_result)

    # Handles unreal test results depending on success or failure of test execution
    def _handle_test_outcome(self, result: UnrealTestResult):
        if result.skipped:
            pytest.skip("Test not executed in this batch")
        elif result.passed:
            pass
        elif result.failed:
            # Format error messages from Unreal entries
            message_parts = [f"Test failed with {result.num_errors} errors and {result.num_warnings} warnings"]
            
            # Add error details
            for entry in result.errors:
                message_parts.append(f"\n  Error: {entry.message}")
                if entry.filename and entry.line_number:
                    message_parts.append(f"\n    at {entry.filename}:{entry.line_number}")
            
            # Add warning details
            for entry in result.warnings:
                message_parts.append(f"\n  Warning: {entry.message}")
                if entry.filename and entry.line_number:
                    message_parts.append(f"\n    at {entry.filename}:{entry.line_number}")
            
            message = "".join(message_parts)
            raise UnrealTestException(message)

    def repr_failure(self, excinfo):
        if isinstance(excinfo.value, UnrealTestException):
            return excinfo.value.args[0]
        return super().repr_failure(excinfo)

    def reportinfo(self):
        if self.cpp_file:
            return self.cpp_file, self.lineno, f"usecase: {self.name}"
        return self.fspath, 0, f"usecase: {self.name}"

# Use this to have custom exceptions
class UnrealTestException(Exception):
    pass

def is_cpp_test_file(file_path: pathlib.Path) -> bool:
    if file_path.suffix.lower() not in CPP_EXTENSIONS:
        return False
    return file_path.name.endswith("Test" + file_path.suffix)

def pytest_collect_file(parent, file_path : pathlib.Path):
    # Skip C++ test collection on Linux
    if sys.platform != "win32":
        return None
        
    # Only custom-collect C++ automation tests; let pytest's normal Python collectors
    # handle .py tests in `Test/` and doctests in `Resources/python/schola`.
    if file_path.suffix.lower() in CPP_EXTENSIONS:
        return UnrealTestFile.from_parent(parent=parent, path=file_path)
    return None

def pytest_ignore_collect(collection_path: pathlib.Path, config):
    # Skip C++ test files on Linux
    if sys.platform != "win32" and is_cpp_test_file(collection_path):
        return True  # Ignore C++ test files on Linux
        
    if is_cpp_test_file(collection_path):
        return False # Forcibly do not ignore C++ test files
    return None # Punt on everything else

class UnrealTestRunner:

    def __init__(self, session, unreal_items: List[UnrealTestItem], report_dir: pathlib.Path):
        self.session = session
        self.unreal_items = unreal_items
        self.unreal_path = get_engine_path_from_config(session.config)
        self.should_build = get_build_unreal_from_config(session.config)
        self.report_dir = report_dir

    def mark_all_unreal_tests_failed(self, msg:str):
        for item in self.unreal_items:
            item.unreal_result = UnrealTestResult(
                name=item.name,
                test_path=item.test_path,
                outcome="failed",
                duration=0.0,
                errors=[UnrealTestEvent.make_from_item(item, msg)],
                warnings=[]
            )

    def _build_and_run_unreal_tests(self, report_dir: pathlib.Path, editor_path: pathlib.Path) -> bool:
        tests_to_run = "+".join((item.test_path for item in self.unreal_items))
        # handle running from the project directory or the Schola directory
        uproject_path = get_uproject_file(self.session.config.rootpath) or get_uproject_file(self.session.config.rootpath.parent.parent)
        if not uproject_path:
            print("Error Building Unreal Project: No .uproject file found in directory")
            return False
        # Build the project if requested
        if self.should_build:
            try:
                self._build_unreal_project(uproject_path)
            except Exception as e:
                print(f"Error building Unreal project: {e}")
                return False
        try:
            run_unreal_tests(
                editor_path=editor_path,
                uproject_path=uproject_path,
                report_path=report_dir,
                tests_to_run=tests_to_run,
            )
        except Exception as e:
            print(f"Error running Unreal tests: {e}")
            return False
        
        return True

    def run_batch_tests(self) -> bool:
        # Setup the report directory
        
        editor_path = get_editor_executable_path(self.unreal_path)
        success = self._build_and_run_unreal_tests(self.report_dir, editor_path)
        if not success:
            self.mark_all_unreal_tests_failed("Unreal Engine failed to build and run tests")
            return False
        
        try:
            results = load_unreal_test_results(self.report_dir)
        except Exception as e:
            print(f"Error loading Unreal test results: {e}")
            self.mark_all_unreal_tests_failed("Unreal Engine failed to load test results")
            return False

        for item in self.unreal_items:
            if item.sanitized_test_path in results:
                item.unreal_result = results[item.sanitized_test_path]
            else:
                # Mark the test as failed if it is not in the results
                item.unreal_result = UnrealTestResult(
                    name=item.name,
                    outcome="failed",
                    duration=0.0,
                    errors=[UnrealTestEvent(message="Test not found in Unreal Engine results", filename=item.cpp_file, line_number=item.lineno, timestamp="")],
                    warnings=[]
                )
        return True

    def _build_unreal_project(self, uproject_path: pathlib.Path) -> Tuple[bool, str]:
        """Build the Unreal project before running tests."""
        project_folder = uproject_path.parent
        
        # Get UE version and UBT path
        ue_version = get_ue_version(uproject_path)
        if not ue_version:
            raise Exception("Could not determine Unreal Engine version from .uproject file")
        
        ubt_path = get_ubt_path(project_folder, ue_version)

        # Try to use the engine path if we couldn't find UBT via the project files
        if not ubt_path and self.unreal_path:
            import platform
            script_name = "RunUAT.bat" if platform.system() == "Windows" else "RunUAT.sh"
            possible_path = self.unreal_path / "Engine" / "Build" / "BatchFiles" / script_name
            if possible_path.exists():
                ubt_path = possible_path

        if not ubt_path:
            raise Exception("Could not find Unreal Build Tool (UBT) path")
        
        # Setup build directory
        build_dir = project_folder / "Build" / "Staged"
        build_dir.mkdir(parents=True, exist_ok=True)
        
        print(f"\nBuilding Unreal project: {uproject_path}")
        print(f"Using UBT at: {ubt_path}")
        print(f"Build Directory: {build_dir}")
        
        result = build_executable(
            project_file=str(uproject_path),
            build_dir=str(build_dir),
            ubt_path=str(ubt_path),
            should_package=False,
            should_cook=True,
            force_monolithic=False,
        )
        print("="*10 + " Unreal Build Output " + "="*10)
        print(result.stdout.decode('utf-8', errors='ignore'))
        print("="*10 + " End of Unreal Build Output " + "="*10)
        if result.returncode != 0:
            if result.stderr:
                print("="*10 + " Unreal Build Error " + "="*10)
                print(result.stderr.decode('utf-8', errors='ignore'))
                print("="*10 + " Unreal Build Error " + "="*10)
            raise Exception(f"Unreal build failed with return code {result.returncode}")
             
@pytest.hookimpl(tryfirst=True)
def pytest_runtestloop(session):
    if session.testsfailed and not session.config.option.continue_on_collection_errors:
        raise session.Interrupted(
            f"{session.testsfailed} error{'s' if session.testsfailed != 1 else ''} during collection"
        )

    if session.config.option.collectonly:
        return True

    unreal_items = [item for item in session.items if isinstance(item, UnrealTestItem)]

    # Run Unreal tests in batch if we have any (only on Windows)
    if unreal_items and sys.platform == "win32":
        # Ensure report directory exists (create if pytest_sessionstart wasn't called)
        if not hasattr(session.config, '_unreal_report_dir'):
            tmp_path_factory = session.config._tmp_path_factory
            session.config._unreal_report_dir = tmp_path_factory.mktemp("unreal_test_reports")
        report_dir = session.config._unreal_report_dir
        runner = UnrealTestRunner(session, unreal_items, report_dir)
        runner.run_batch_tests()
    
    # Run all tests (including the now-populated Unreal tests)
    _run_all_tests(session)

    return True


def _run_all_tests(session):
    for i, item in enumerate(session.items):
        nextitem = session.items[i + 1] if i + 1 < len(session.items) else None
        item.config.hook.pytest_runtest_protocol(item=item, nextitem=nextitem)

        if session.shouldfail:
            raise session.Failed(session.shouldfail)
        if session.shouldstop:
            raise session.Interrupted(session.shouldstop)
