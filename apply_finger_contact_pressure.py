#!/usr/bin/env python3
"""Inject optional contact-area pressure into Krita's existing touch path.

Only KoPointerEvent's QTouchEvent pressure accessor changes on Android.
No QTabletEvent, touch dispatcher, input shortcut or brush engine changes.
Run after other Krita source patches in local-build.sh.
"""
from pathlib import Path
import sys


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"Finger pressure {label}: expected 1 anchor, got {count}")
    return text.replace(old, new, 1)


def main(root: Path) -> None:
    root = root.resolve()
    own = Path(__file__).resolve().parent
    source = own / "mobile_core/finger_contact_pressure.h"
    if not source.is_file():
        raise RuntimeError("Missing finger contact pressure implementation")
    flake = root / "libs/flake"
    target = flake / "KoPointerEvent.cpp"
    content = target.read_text(encoding="utf-8")
    content = replace_once(
        content,
        '#include "KoPointerEvent.h"',
        '#include "KoPointerEvent.h"\n'
        '#ifdef Q_OS_ANDROID\n'
        '#include "finger_contact_pressure.h"\n'
        '#endif',
        "touch pressure helper include",
    )
    original = (
        '            return s_optionContainer->useTouchPressure ? '
        'event->touchPoints().at(0).pressure() : 1.0;'
    )
    updated = (
        '            const qreal nativePressure = s_optionContainer->useTouchPressure ?\n'
        '                event->touchPoints().at(0).pressure() : 1.0;\n'
        '#ifdef Q_OS_ANDROID\n'
        '            // Experimental finger-only contact area proxy. Disabled\n'
        '            // by default; stylus/QTabletEvent path is unchanged.\n'
        '            return MobileFingerContactPressure::pressure(event,nativePressure);\n'
        '#else\n'
        '            return nativePressure;\n'
        '#endif'
    )
    content = replace_once(
        content, original, updated, "Qt5 KoPointerEvent touch pressure",
    )
    target.write_text(content, encoding="utf-8")
    (flake / "finger_contact_pressure.h").write_text(
        source.read_text(encoding="utf-8"), encoding="utf-8",
    )
    settings_header = own / "mobile_core/finger_pressure_settings.h"
    if not settings_header.is_file():
        raise RuntimeError("Missing shared finger pressure calibration settings")
    (flake / "finger_pressure_settings.h").write_text(
        settings_header.read_text(encoding="utf-8"), encoding="utf-8",
    )
    source_selector = own / "mobile_core/finger_contact_source.h"
    if not source_selector.is_file():
        raise RuntimeError("Missing per-stroke Qt/raw contact source selector")
    (flake / "finger_contact_source.h").write_text(
        source_selector.read_text(encoding="utf-8"), encoding="utf-8",
    )
    print("Installed optional calibrated finger pressure in KoPointerEvent")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: apply_finger_contact_pressure.py KRITA_SOURCE")
    main(Path(sys.argv[1]))
