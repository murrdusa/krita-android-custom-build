#!/usr/bin/env python3
"""Report the contents of built Android APKs without changing their packaging.

Run: python3 apk_size_report.py /path/to/apk-output
Results are written alongside the APKs as apk-size-report.json.
Compressed ZIP member sizes are not the same as installed or memory size.
"""
from __future__ import annotations

import hashlib
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


def digest(apk: Path) -> str:
    sha = hashlib.sha256()
    with apk.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(chunk)
    return sha.hexdigest()


def inspect(apk: Path) -> dict:
    groups: dict[str, dict[str, int]] = defaultdict(
        lambda: {"compressed_bytes": 0, "uncompressed_bytes": 0, "count": 0}
    )
    largest = []
    abis = set()
    with zipfile.ZipFile(apk) as archive:
        for entry in archive.infolist():
            if entry.is_dir():
                continue
            if entry.filename.startswith("lib/"):
                parts = entry.filename.split("/")
                if len(parts) > 2:
                    abis.add(parts[1])
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
        "sha256": digest(apk),
        "abis": sorted(abis),
        "categories": dict(sorted(
            groups.items(), key=lambda item: -item[1]["compressed_bytes"]
        )),
        "largest_entries": sorted(
            largest, key=lambda item: -item["compressed_bytes"]
        )[:12],
    }


def record_history(output: Path, newest: dict) -> None:
    """Keep a local build-to-build size baseline even when APKs overwrite."""
    path = output / "apk-size-history.json"
    history = []
    if path.is_file():
        try:
            saved = json.loads(path.read_text(encoding="utf-8"))
            if saved.get("format_version") == 1:
                history = saved.get("snapshots", [])
                if not isinstance(history, list):
                    history = []
        except (OSError, ValueError, AttributeError):
            print("APK size audit: resetting unreadable local history", file=sys.stderr)
    previous = history[-1] if history else None
    if previous and previous.get("sha256") == newest["sha256"]:
        print("APK size audit: unchanged from the last recorded build")
        return
    if previous:
        delta = newest["apk_bytes"] - previous["apk_bytes"]
        sign = "+" if delta > 0 else ""
        print(f"  APK change from prior build: {sign}{mib(delta)} "
              f"({delta:+,} bytes)")
        if previous.get("abis") != newest["abis"]:
            print("  NOTE: ABI sets changed; this is not a like-for-like comparison")
        categories = sorted(set(newest["categories"]) | set(previous["categories"]))
        for name in categories:
            new_bytes = newest["categories"].get(name, {}).get("compressed_bytes", 0)
            old_bytes = previous["categories"].get(name, {}).get("compressed_bytes", 0)
            change = new_bytes - old_bytes
            if change:
                print(f"    {name}: {change:+,} compressed bytes")
    else:
        print("APK size audit: recorded the first size baseline")
    snapshot = {key: newest[key] for key in
                ("apk", "apk_bytes", "sha256", "abis", "categories")}
    history.append(snapshot)
    path.write_text(json.dumps({
        "format_version": 1, "snapshots": history[-20:]
    }, indent=2) + "\n", encoding="utf-8")
    print(f"Size history: {path}")


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
        record_history(output, reports[0])
        destination = output / "apk-size-report.json"
        destination.write_text(json.dumps(
            {"format_version": 1, "apks": reports}, indent=2
        ) + "\n", encoding="utf-8")
        print(f"Full size breakdown: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
