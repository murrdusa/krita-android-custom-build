#!/usr/bin/env python3
"""Fingerprint Krita build inputs and retain timestamps for unchanged patches."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

NON_APP={"krita_build_live.py","krita_build_reporter.py",
         "krita_build_watcher.py","krita_build_cache.py",
         "apk_size_report.py",".gitignore"}
GENERATED=("libs/ui/tablet_compact_skin.h","libs/ui/mobile_shell.h",
           "libs/ui/standalone_host.h","libs/ui/canvas_session.h",
           "libs/ui/canvas_transfer.h","libs/ui/canvas_surface.h",
           "libs/ui/engine_bridge.h","libs/ui/document_service.h",
           "libs/ui/command_service.h",
           "libs/ui/brush_workspace.h","libs/ui/color_workspace.h",
           "libs/ui/layer_workspace.h",
           "libs/ui/command_browser.h","libs/ui/quick_actions.h","libs/ui/touch_reorder_list.h",
           "libs/ui/tool_browser.h",
           "libs/ui/context_tool_bar.h",
           "libs/ui/document_workflow.h",
           "libs/ui/document_switcher.h",
           "libs/ui/document_home.h",
           "build-in-krita-container.sh")

def sha(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        while True:
            block=f.read(1024*1024)
            if not block:break
            h.update(block)
    return h.hexdigest()

def paths(repo,*args):
    raw=subprocess.check_output(["git","-C",str(repo),*args,"-z"])
    return [os.fsdecode(x) for x in raw.split(b"\0") if x]

def fingerprint(repo):
    h=hashlib.sha256(b"krita-app-inputs-v1\0")
    for rel in sorted(paths(repo,"ls-files")):
        if (rel in NON_APP or rel.startswith((".github/", "tests/"))
                or rel.endswith(".md")):
            continue
        item=repo/rel
        data=sha(item) if item.is_file() else "missing"
        h.update(os.fsencode(rel)+b"\0"+data.encode("ascii")+b"\0")
    return h.hexdigest()

def snapshot(source,manifest):
    items=set(paths(source,"diff","--name-only","--no-ext-diff"))
    items.update(GENERATED)
    records={}
    for rel in sorted(items):
        f=source/rel
        if f.is_file() and not f.is_symlink():
            records[rel]={"sha":sha(f),"mtime":f.stat().st_mtime_ns}
    manifest.parent.mkdir(parents=True,exist_ok=True)
    tmp=manifest.with_suffix(".tmp")
    tmp.write_text(json.dumps({"version":1,"files":records}))
    tmp.replace(manifest)
    print(f"[incremental] Recorded {len(records)} patched files",flush=True)

def restore(source,manifest):
    if not manifest.is_file():return
    obj=json.loads(manifest.read_text())
    if obj.get("version")!=1:raise RuntimeError("Unsupported patch snapshot")
    same=changed=0
    for rel,record in obj["files"].items():
        f=source/rel
        if not f.is_file() or f.is_symlink() or sha(f)!=record["sha"]:
            changed+=1
            continue
        now=f.stat()
        os.utime(f,ns=(now.st_atime_ns,int(record["mtime"])))
        same+=1
    manifest.unlink(missing_ok=True)
    print(f"[incremental] Restored {same} unchanged timestamps; {changed} changed",flush=True)

def main():
    p=argparse.ArgumentParser()
    sub=p.add_subparsers(dest="action",required=True)
    sub.add_parser("fingerprint").add_argument("repo",type=Path)
    for action in ("snapshot","restore"):
        q=sub.add_parser(action)
        q.add_argument("source",type=Path)
        q.add_argument("manifest",type=Path)
    args=p.parse_args()
    if args.action=="fingerprint":print(fingerprint(args.repo.resolve()))
    elif args.action=="snapshot":snapshot(args.source.resolve(),args.manifest.resolve())
    else:restore(args.source.resolve(),args.manifest.resolve())

if __name__=="__main__":main()
