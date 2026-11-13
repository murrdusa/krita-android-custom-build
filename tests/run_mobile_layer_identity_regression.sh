#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Host-only transform hierarchy checks; no Android build required.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
c++ -std=c++17 -O2 -Wall -Wextra -Werror \
    "$repo/tests/mobile_layer_identity_regression.cpp" \
    -o "$out/mobile_layer_identity_regression"
"$out/mobile_layer_identity_regression"
