#!/usr/bin/env python3
"""Report the contents of built Android APKs without changing their packaging.

Run: python3 apk_size_report.py /path/to/apk-output
Results are written alongside the APKs as apk-size-report.json.
Compressed ZIP member sizes are not the same as installed or memory size.
"""
from __future__ import annotations

import json
import sys
import zipfile
from collections import defaultdict
from pathlib import Path


def category(name: str) -> str:
    if name.startswith("lib/"):
        return "Native libraries"
    if name.endswith(".dex"):
        return "Android bytecode"
    if name.startswith("assets/"):
        return "Bundled assets"
    if name.startswith("res/") or name == "resources.arsc":
        return "Android resources"
    if name.startswith("META-INF/"):
        return "Signatures/metadata"
    return "Other files"


def mib(size: int) -> str:
    return f"{size / (1024 * 1024):,.1f} MiB"


def inspect(apk: Path) -> dict:
    groups: dict[str, dict[str, int]] = defaultdict(
        lambda: {"compressed_bytes": 0, "uncompressed_bytes": 0, "count": 0}
    )
    largest = []
    with zipfile.ZipFile(apk) as archive:
        for entry in archive.infolist():
            if entry.is_dir():
                continue
            group = groups[category(entry.filename)]
            group["compressed_bytes"] += entry.compress_size
            group["uncompressed_bytes"] += entry.file_size
            group["count"] += 1
            largest.append({
                "path": entry.filename,
                "compressed_bytes": entry.compress_size,
                "uncompressed_bytes": entry.file_size,
            })
    return {
        "apk": apk.name,
        "apk_bytes": apk.stat().st_size,
        "categories": dict(sorted(
            groups.items(), key=lambda item: -item[1]["compressed_bytes"]
        )),
        "largest_entries": sorted(
            largest, key=lambda item: -item["compressed_bytes"]
        )[:12],
    }


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: apk_size_report.py APK_OUTPUT_DIRECTORY", file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    apks = sorted(output.glob("*.apk"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not apks:
        print("APK size audit: no APKs found; skipping")
        return 0
    reports = []
    for apk in apks:
        if not zipfile.is_zipfile(apk):
            print(f"APK size audit: skipping invalid ZIP {apk.name}", file=sys.stderr)
            continue
        report = inspect(apk)
        reports.append(report)
        print(f"APK size audit: {report['apk']} — {mib(report['apk_bytes'])}")
        for name, info in report["categories"].items():
            print(f"  {name:<22} {mib(info['compressed_bytes']):>11}"
                  f"  ({info['count']} entries)")
        print("  Largest payloads:")
        for item in report["largest_entries"][:8]:
            print(f"    {mib(item['compressed_bytes']):>10}  {item['path']}")
    if reports:
        destination = output / "apk-size-report.json"
        destination.write_text(json.dumps(
            {"format_version": 1, "apks": reports}, indent=2
        ) + "\n", encoding="utf-8")
        print(f"Full size breakdown: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
