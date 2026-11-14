"""Paths shared by the Krita source patch scripts."""
from pathlib import Path

PATCH_DIR = Path(__file__).resolve().parent
REPO = PATCH_DIR.parents[1]
CORE = REPO / "mobile_studio" / "include" / "mobile_studio"
SRC = REPO / "mobile_studio" / "src"
