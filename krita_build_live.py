#!/usr/bin/env python3
"""compat shim: the PC watcher calls this path. real module in tools/build."""
from pathlib import Path
import runpy

if __name__ == "__main__":
    runpy.run_path(
        str(Path(__file__).resolve().parent / "tools/build/krita_build_live.py"),
        run_name="__main__")
