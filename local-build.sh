#!/usr/bin/env bash
# compat entrypoint: the PC build watcher calls this path.
# real script lives in tools/build.
exec bash "$(cd "$(dirname "$0")" && pwd)/tools/build/local-build.sh" "$@"
