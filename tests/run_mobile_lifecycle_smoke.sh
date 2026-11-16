#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Fast host-Qt smoke tests for native lifecycle, save rollback and cache retention.
# Called from the existing WSL Android build watcher, not GitHub Actions.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
state_dir="${1:-$repo/.build-watcher}"
mkdir -p "$state_dir"

if ! command -v c++ >/dev/null || ! command -v pkg-config >/dev/null ||
   ! pkg-config --exists Qt5Widgets; then
    echo "Native Qt lifecycle tests: NOT RUN (Qt5Widgets host development libraries unavailable)"
    echo "Native Qt lifecycle tests: Android build result does not validate these tests"
    exit 0
fi

# Cache only verified passes. Source/header changes force another run,
# while docs-only watcher ticks do not recompile five desktop tests.
inputs=(
    "$repo/tests/run_mobile_lifecycle_smoke.sh"
    "$repo/tests/qt_native_view_stack.cpp"
    "$repo/tests/qt_native_document_rollback.cpp"
    "$repo/tests/qt_canvas_resume_observer.cpp"
    "$repo/tests/qt_project_preview_cache.cpp"
    "$repo/tests/qt_saved_version_archive.cpp"
    "$repo/mobile_core/native_view_stack.h"
    "$repo/mobile_core/lightweight_canvas_host.h"
    "$repo/mobile_core/native_document_rollback.h"
    "$repo/mobile_core/canvas_resume_observer.h"
    "$repo/mobile_core/view_identity_guard.h"
    "$repo/mobile_core/view_host_port.h"
    "$repo/mobile_core/host_services.h"
    "$repo/mobile_core/native_canvas_chrome.h"
    "$repo/mobile_core/project_preview_cache.h"
    "$repo/mobile_core/saved_version_archive.h"
    "$repo/mobile_core/mobile_draft_recovery.h"
)
# These headers are included transitively by the tested native host.
# Include the host Qt version too: a previously passing binary is not
# evidence of compatibility after WSL upgrades its Qt development kit.
fingerprint="$(
    { sha256sum "${inputs[@]}"; pkg-config --modversion Qt5Core Qt5Widgets; } |
        sha256sum | cut -d ' ' -f 1
)"
stamp="$state_dir/.native-qt-lifecycle-pass"
if [[ -s "$stamp" && "$(cat "$stamp")" == "$fingerprint" ]]; then
    echo "Native Qt lifecycle tests: PASS (cached identical sources)"
    exit 0
fi

out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
for name in qt_native_view_stack qt_native_document_rollback qt_canvas_resume_observer qt_project_preview_cache qt_saved_version_archive; do
    echo "Native Qt lifecycle tests: compiling $name"
    c++ -std=c++17 -fPIC -O1 -g -DKRITA_MOBILE_QT_TEST -I"$repo" \
        "$repo/tests/$name.cpp" -o "$out/$name" \
        $(pkg-config --cflags --libs Qt5Widgets)
    echo "Native Qt lifecycle tests: running $name"
    QT_QPA_PLATFORM=offscreen "$out/$name"
done
printf '%s\n' "$fingerprint" > "$stamp.tmp"
mv "$stamp.tmp" "$stamp"
echo "Native Qt lifecycle tests: PASS (5 executables; exact source fingerprint $fingerprint)"
