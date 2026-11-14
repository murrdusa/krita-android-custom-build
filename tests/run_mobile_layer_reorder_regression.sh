#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Host-only layer reorder regression; does not build Android or upload APKs.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
c++ -std=c++17 -I"$repo/mobile_studio/include" -O2 -Wall -Wextra -Werror \
    "$repo/tests/cpp/mobile_layer_reorder_regression.cpp" \
    -o "$out/mobile_layer_reorder_regression"
"$out/mobile_layer_reorder_regression"
