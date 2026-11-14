#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Execute desktop Qt archive checks without Android builds or phone tests.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
if ! command -v pkg-config >/dev/null || ! pkg-config --exists Qt5Core Qt5Gui Qt5Widgets; then
  echo 'Qt5 desktop development libraries are required (Qt5Core, Qt5Gui, Qt5Widgets).' >&2
  exit 2
fi
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
c++ -std=c++17 -I"$repo/mobile_studio/include" -fPIC -O2 -Wall -Wextra \
  $(pkg-config --cflags Qt5Core Qt5Gui Qt5Widgets) \
  "$repo/tests/cpp/mobile_archive_regression.cpp" -o "$out/mobile_archive_regression" \
  $(pkg-config --libs Qt5Core Qt5Gui Qt5Widgets)
"$out/mobile_archive_regression"
