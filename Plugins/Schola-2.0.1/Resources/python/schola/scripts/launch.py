# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
import sys
from cyclopts import App, Parameter, validators, group_extractors, Group
from schola.scripts.panel import print_error

from rich.console import Console
console=Console()
app = App(console=console, name="schola", help="CLI for Schola. Useful for training a model or invoking utilities")

#TODO make these errors only appear when you try and invoke the specific commands
try:
    from schola.scripts.sb3 import sb3_app 
    app.command(sb3_app,name="sb3")
except ImportError:
    console.print_exception()
    print_error("Stable Baselines3 (SB3) is not installed. Install via:\n"
                    "pip install 'stable_baselines3'\n"
                    "Then re-run your schola command.")
    
try:
    from schola.scripts.rllib import rllib_app
    app.command(rllib_app,name="rllib")
except ImportError as e:
    console.print_exception()
    print_error("Ray RLlib is not installed. Install via:\n"
                    "pip install 'ray[rllib]'\n"
                    "Then re-run your schola command.")


from schola.scripts.utils.compile_proto import app as compile_proto_app
app.command(compile_proto_app, name="compile-proto")

from schola.scripts.utils.build_docs import app as build_docs_app
app.command(build_docs_app, name="build-docs")

#app["compile-proto"].group = "Utilities"
app["--version"].group = "Utilities"
app["--help"].group = "Utilities"

# indirection function for CLI entry point
def main():
    try:
        app()
    except Exception as e:  # keep lightweight panel reporting
        #print_error(f"Unhandled exception: {e}")
        console.print_exception()
        sys.exit(1)

if __name__ == "__main__":
    main()
