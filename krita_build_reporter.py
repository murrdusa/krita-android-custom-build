#!/usr/bin/env python3
"""Publish existing WSL Krita watcher build results to GitHub."""
import argparse
import datetime
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

REPO = "murrdusa/krita-android-custom-build"
CONTEXT = "local/android-wsl-build"
DASHBOARD = "[Local Android build] Latest compiler result"
BASE = Path.home() / "krita-tablet-customizations" / ".build-watcher"
PID = BASE / "reporter.pid"
LOG = BASE / "reporter.log"
STATE = BASE / "reporter-state.json"
WATCHER = BASE / "status.json"

def run(args, timeout=45):
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return subprocess.CompletedProcess(args, 1, "", str(exc))

def gh(*args):
    return run(["gh", *args])

def load(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}

def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(".tmp")
    temp.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    temp.replace(path)

def publish_status(sha, state, description, url=""):
    args = ["api", "--method", "POST", f"repos/{REPO}/statuses/{sha}",
            "-f", f"state={state}", "-f", f"context={CONTEXT}",
            "-f", f"description={description[:140]}"]
    if url:
        args.extend(["-f", f"target_url={url}"])
    result = gh(*args)
    if result.returncode:
        raise RuntimeError("Commit status: " + (result.stderr or result.stdout)[-500:])

def get_dashboard():
    response = gh("issue", "list", "--repo", REPO, "--state", "all",
                  "--search", '"Latest compiler result" in:title',
                  "--json", "number,title", "--limit", "50")
    if response.returncode:
        raise RuntimeError("Issue lookup: " + response.stderr[-500:])
    for item in json.loads(response.stdout):
        if item.get("title") == DASHBOARD:
            return int(item["number"])
    return None

def update_dashboard(body_path):
    number = get_dashboard()
    if number is None:
        response = gh("issue", "create", "--repo", REPO, "--title", DASHBOARD,
                      "--body-file", str(body_path))
        if response.returncode:
            raise RuntimeError("Issue creation: " + response.stderr[-500:])
        return response.stdout.strip().splitlines()[-1]
    response = gh("issue", "edit", str(number), "--repo", REPO,
                  "--body-file", str(body_path))
    if response.returncode:
        raise RuntimeError("Issue update: " + response.stderr[-500:])
    return f"https://github.com/{REPO}/issues/{number}"

def report_completed(watcher):
    sha = watcher["revision"]
    success = watcher["result"] == "success"
    report_path = Path(watcher.get("report") or "")
    if report_path.is_file():
        body = report_path.read_text(encoding="utf-8", errors="replace")
    else:
        body = f"# Local Android build: {'SUCCESS' if success else 'FAILED'}\n\nRevision: `{sha}`\n"
    body = (f"**Commit:** `{sha}`\n\n"
            f"**Outcome:** {'SUCCESS' if success else 'FAILED'}\n\n"
            f"**Completed UTC:** {watcher.get('completed_at', 'unknown')}\n\n" + body)
    issue_url = ""
    if not success:
        result = gh("issue", "create", "--repo", REPO,
                    "--title", f"[Local build] Android compile failed at {sha[:12]}",
                    "--body-file", str(report_path)) if report_path.is_file() else None
        if result and result.returncode == 0:
            issue_url = result.stdout.strip().splitlines()[-1]
            body = f"**Failure diagnostics issue:** {issue_url}\n\n" + body
        else:
            print("[reporter] Could not create separate failure issue", flush=True)
    temp = BASE / "reporter-latest.md"
    temp.write_text(body[:60000], encoding="utf-8")
    dashboard_url = update_dashboard(temp)
    publish_status(sha, "success" if success else "failure",
                   "WSL Android APK build passed" if success else "WSL Android build failed",
                   issue_url or dashboard_url)
    return dashboard_url

def process_once():
    if shutil.which("gh") is None:
        raise RuntimeError("GitHub CLI not installed. Run sudo apt install gh")
    if gh("auth", "status").returncode:
        raise RuntimeError("GitHub CLI not authenticated. Run gh auth login")
    watcher = load(WATCHER)
    state = load(STATE)
    sha = watcher.get("revision")
    if not sha:
        return "No watcher revision to report yet"
    if watcher.get("phase") == "building":
        key = f"{sha}:{watcher.get('started_at', '')}"
        if key != state.get("last_pending"):
            publish_status(sha, "pending", "Compiling Krita Android in WSL")
            state["last_pending"] = key
            save(STATE, state)
            return f"Pending status posted for {sha[:12]}"
    elif (watcher.get("phase") == "idle" and watcher.get("result") in ("success", "failure")
          and watcher.get("completed_at")):
        key = f"{sha}:{watcher['completed_at']}"
        if key != state.get("last_complete"):
            url = report_completed(watcher)
            state["last_complete"] = key
            state["latest_url"] = url
            state["last_result"] = watcher["result"]
            save(STATE, state)
            return f"{watcher['result'].upper()} reported for {sha[:12]}: {url}"
    return "No new build state"

def is_alive(pid):
    if not pid:
        return False
    try:
        os.kill(pid, 0)
        stat = Path(f"/proc/{pid}/stat")
        return not (stat.is_file() and stat.read_text().split()[2] == "Z")
    except (OSError, IndexError):
        return False

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["start", "run", "once", "status", "stop"])
    parser.add_argument("--interval", type=int, default=10)
    args = parser.parse_args()
    BASE.mkdir(parents=True, exist_ok=True)
    pid = int(PID.read_text()) if PID.exists() and PID.read_text().strip().isdigit() else 0
    if args.action == "status":
        print(json.dumps({"running": is_alive(pid), "pid": pid,
                          "publication": load(STATE), "watcher": load(WATCHER)}, indent=2))
        return 0
    if args.action == "stop":
        if is_alive(pid):
            os.kill(pid, signal.SIGTERM)
            print("Reporter stop requested")
        else:
            print("Reporter not running")
        return 0
    if args.action == "start":
        if is_alive(pid):
            print(f"Reporter already running (PID {pid})")
            return 0
        with LOG.open("a", encoding="utf-8") as stream:
            proc = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), "run",
                                    "--interval", str(args.interval)], stdin=subprocess.DEVNULL,
                                    stdout=stream, stderr=subprocess.STDOUT,
                                    cwd=Path(__file__).resolve().parent, start_new_session=True)
        print(f"Started GitHub reporter PID {proc.pid}, log: {LOG}")
        return 0
    if args.action == "once":
        print(process_once())
        return 0
    import fcntl
    lock = BASE / "reporter.lock"
    with lock.open("a+") as f:
        try:
            fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("Another GitHub reporter is running", file=sys.stderr)
            return 2
        PID.write_text(str(os.getpid()))
        try:
            while True:
                try:
                    msg = process_once()
                    if msg != "No new build state":
                        print(f"[{datetime.datetime.now().isoformat(timespec='seconds')}] {msg}", flush=True)
                except Exception as exc:
                    print(f"[reporter] {exc}", flush=True)
                time.sleep(max(5, args.interval))
        finally:
            PID.unlink(missing_ok=True)

if __name__ == "__main__":
    raise SystemExit(main())
