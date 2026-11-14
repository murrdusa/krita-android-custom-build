#!/usr/bin/env python3
"""Apply every Krita source patch in build order."""
import argparse
from pathlib import Path
import subprocess
import sys

PATCH_DIR = Path(__file__).resolve().parent
ORDER = (
    "apply_customizations.py",
    "apply_tablet_update.py",
    "apply_tablet_refinements.py",
    "apply_tablet_polish.py",
    "apply_tablet_cleanup.py",
    "apply_reference_touch_fix.py",
    "apply_compact_skin.py",
    "apply_wheel_rotation.py",
    "apply_diamond_rotation.py",
    "apply_gallery_tabs.py",
    "apply_home_mobile.py",
    "apply_mobile_recovery.py",
    "apply_mobile_touch_scroll.py",
    "apply_mobile_shell.py",
    "apply_finger_contact_pressure.py",
    "apply_finger_raw_motion.py",
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path,
                        help="Krita source tree")
    parser.add_argument("--list", action="store_true",
                        help="print the patch order and exit")
    args = parser.parse_args()
    if args.list:
        print("\n".join(ORDER))
        return 0
    if args.source is None:
        parser.error("a Krita source tree is required")
    missing = [n for n in ORDER if not (PATCH_DIR / n).is_file()]
    if missing:
        raise SystemExit("Missing patch scripts: " + ", ".join(missing))
    for name in ORDER:
        print(f"==> {name}", flush=True)
        subprocess.run([sys.executable, str(PATCH_DIR / name),
                        str(args.source)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
