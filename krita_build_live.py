#!/usr/bin/env python3
"""Live terminal viewer for the existing Krita Android WSL build watcher.

This is read-only. It follows the active build log, automatically switches
between revisions, prints progress/error lines, and flags suspicious silence.
The build watcher and GitHub reporter continue running independently.
"""
import argparse
from collections import deque
from datetime import datetime
import json
import os
from pathlib import Path
import re
import sys
import time

BASE = Path(__file__).resolve().parent / ".build-watcher"
STATE = BASE / "status.json"
WATCHER_LOG = BASE / "watcher.log"
ERROR_LINE = re.compile(r"(?:\berror:|\bfatal error:|undefined reference|FAILED|Error [12]\b)", re.I)
PERCENT = re.compile(r"\[\s*(\d{1,3})%\]")
ANSI = sys.stdout.isatty()
CLEAR = "\033[0m"
COLORS = {"green": "\033[32m", "yellow": "\033[33m", "red": "\033[31m", "cyan": "\033[36m"}


def colored(message, color):
    return COLORS[color] + message + CLEAR if ANSI else message


def stamp():
    return datetime.now().strftime("%H:%M:%S")


def out(message, color=None):
    line = f"[{stamp()}] {message}"
    print(colored(line, color) if color else line, flush=True)


def state():
    try:
        return json.loads(STATE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def alive(pid):
    """Return False for absent/stale PIDs without crashing the live viewer."""
    if pid is None or pid == "":
        return False
    try:
        pid = int(pid)
        if pid <= 0:
            return False
        os.kill(pid, 0)
        proc = Path(f"/proc/{pid}/stat")
        return not proc.exists() or proc.read_text().split()[2] != "Z"
    except (OSError, ValueError, TypeError, IndexError):
        return False


def when(value):
    if not value:
        return None
    try:
        return datetime.fromisoformat(value).timestamp()
    except (ValueError, TypeError):
        return None


def duration(seconds):
    seconds = max(0, int(seconds))
    hours, rest = divmod(seconds, 3600)
    minutes, secs = divmod(rest, 60)
    return f"{hours:d}:{minutes:02d}:{secs:02d}" if hours else f"{minutes:02d}:{secs:02d}"


def last_lines(path, count):
    try:
        with path.open("r", encoding="utf-8", errors="replace") as fh:
            lines = deque(fh, maxlen=count)
            offset = path.stat().st_size
        return list(lines), offset
    except OSError:
        return [], 0


def main():
    parser = argparse.ArgumentParser(description="Follow current and future Krita build logs")
    parser.add_argument("--interval", type=float, default=1.0, help="poll interval in seconds")
    parser.add_argument("--last", type=int, default=20, help="lines shown from an already-running build")
    parser.add_argument("--quiet", action="store_true", help="show errors, build phases and progress; hide routine compiler lines")
    parser.add_argument("--stale-seconds", type=int, default=120, help="warn if a running build stops producing log output")
    args = parser.parse_args()
    interval = max(0.5, args.interval)
    log_path = None
    offset = 0
    partial = ""
    latest_progress = None
    last_heartbeat = 0.0
    last_warning = 0.0
    last_phase = None
    last_revision = None
    last_output_time = None
    printed_result = None

    out("LIVE Krita Android builds | Ctrl+C exits this VIEWER only", "cyan")
    out("The watcher and GitHub reporter remain in the background.")
    out(f"Watching {STATE}")
    try:
        while True:
            now = time.time()
            data = state()
            phase = str(data.get("phase") or "unknown")
            sha = str(data.get("revision") or data.get("last_attempted") or "")
            short = sha[:12] or "not started"
            current_log = Path(data["log"]) if data.get("log") else None

            if (phase, sha) != (last_phase, last_revision):
                out(f"Build state: {phase.upper()} | commit {short}", "cyan")
                last_phase, last_revision = phase, sha
                latest_progress = None
                last_warning = 0
                printed_result = None

            if current_log != log_path:
                log_path = current_log
                offset = 0
                partial = ""
                last_output_time = now
                if log_path:
                    out(f"Following new log: {log_path}", "cyan")
                    if log_path.is_file():
                        if args.last:
                            existing, offset = last_lines(log_path, args.last)
                            for line in existing:
                                display(line.rstrip("\n"), args.quiet)
                        else:
                            offset = log_path.stat().st_size
                    else:
                        out("Build log not created yet; monitoring for it.", "yellow")

            if log_path and log_path.is_file():
                try:
                    stat = log_path.stat()
                    if stat.st_size < offset:
                        out("Log truncated; following from beginning.", "yellow")
                        offset = 0
                        partial = ""
                    if stat.st_size > offset:
                        with log_path.open("rb") as fh:
                            fh.seek(offset)
                            raw = fh.read()
                            offset = fh.tell()
                        last_output_time = now
                        chunk = partial + raw.decode("utf-8", errors="replace")
                        lines = chunk.split("\n")
                        partial = lines.pop()
                        for line in lines:
                            match = PERCENT.search(line)
                            if match:
                                pct = int(match.group(1))
                                if pct != latest_progress:
                                    latest_progress = pct
                            display(line.rstrip("\r"), args.quiet)
                except OSError as exc:
                    out(f"Cannot read log: {exc}", "yellow")

            if phase == "building":
                started = when(data.get("started_at"))
                elapsed = duration(now - started) if started is not None else "unknown"
                if now - last_heartbeat >= 25:
                    active = alive(data.get("build_pid"))
                    pct = f" | compiler {latest_progress}%" if latest_progress is not None else ""
                    pid = data.get("build_pid")
                    running = ("not reported" if pid in (None, "") else
                               "running" if active else "not found")
                    out(f"BUILD {short} | elapsed {elapsed}{pct} | build PID {running}", "cyan")
                    last_heartbeat = now
                if (last_output_time is not None and
                        now - last_output_time >= args.stale_seconds and
                        now - last_warning >= args.stale_seconds):
                    out(f"No new compiler log lines for {duration(now-last_output_time)}. "
                        "Build may still be working; inspect Docker if this persists.", "yellow")
                    last_warning = now
            else:
                result = data.get("result")
                key = (sha, data.get("completed_at"), result)
                if result in ("success", "failure") and key != printed_result:
                    secs = data.get("duration_sec")
                    took = f" in {duration(secs)}" if isinstance(secs, (int, float)) else ""
                    out(f"BUILD {result.upper()} | {short}{took}", "green" if result == "success" else "red")
                    if data.get("report"):
                        out(f"Report: {data['report']}")
                    printed_result = key
                if now - last_heartbeat >= 60:
                    out("Idle. Watching GitHub-driven local watcher for the next build.", "cyan")
                    last_heartbeat = now

            if not data and now - last_heartbeat >= 30:
                out("No watcher status file yet. Is krita_build_watcher.py running?", "yellow")
                last_heartbeat = now
            time.sleep(interval)
    except KeyboardInterrupt:
        out("Viewer closed. The background watcher was NOT stopped.")
    return 0


def display(line, quiet):
    if not line.strip():
        return
    if ERROR_LINE.search(line):
        out(line, "red")
    elif (not quiet or PERCENT.search(line) or
          line.startswith("==>") or "APK output:" in line):
        out(line)


if __name__ == "__main__":
    sys.exit(main())
