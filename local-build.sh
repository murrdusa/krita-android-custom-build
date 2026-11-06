#!/usr/bin/env bash
# Local persistent Krita Android build. Run from Ubuntu WSL2 with Docker Desktop integration.
set -Eeuo pipefail
MODE=build
if (( $# > 0 )); then MODE="$1"; fi
case "$MODE" in setup|build|status|patch) ;; *) echo "Usage: bash local-build.sh [setup|build|status|patch]" >&2; exit 2;; esac
REPO="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(printenv KRITA_LOCAL_ROOT || echo "$HOME/krita-local-android")"
SOURCE="$ROOT/persistent/krita"
OUT="$ROOT/persistent/krita-custom-apks"
JOBS="$(printenv KRITA_BUILD_JOBS || echo 6)"
say() { printf '\n==> %s\n' "$*"; }
die() { echo "ERROR: $*" >&2; exit 1; }
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || die "Invalid KRITA_BUILD_JOBS"
for p in git python3 docker; do command -v "$p" >/dev/null || die "Missing $p. See LOCAL-BUILD.md"; done
docker info >/dev/null 2>&1 || die "Start Docker Desktop and enable Ubuntu under Resources > WSL Integration."
[[ -f "$REPO/apply_tablet_update.py" ]] || die "Please clone/update the Krita customization repository."
if [[ "$MODE" == status ]]; then
    echo "Workspace: $ROOT"
    [[ ! -d "$OUT" ]] || find "$OUT" -maxdepth 1 -name '*.apk' -print
    exit 0
fi
if [[ ! -d "$ROOT/.git" ]]; then
    if [[ -d "$ROOT" ]] && [[ -n "$(ls -A "$ROOT")" ]]; then die "Build root is nonempty and unmanaged: $ROOT"; fi
    say "First run: clone KDE's Android Docker environment"
    git clone --depth 1 https://invent.kde.org/dkazakov/krita-docker-env.git "$ROOT"
fi
mkdir -p "$ROOT/persistent"
if [[ ! -d "$SOURCE/.git" ]]; then
    [[ ! -e "$SOURCE" ]] || die "Existing non-Git Krita source: $SOURCE"
    say "First run: download Krita source v6.0.4"
    git clone --depth 1 --branch v6.0.4 https://github.com/KDE/krita.git "$SOURCE"
    touch "$SOURCE/.krita_android_custom_patch_applied"
else
    if [[ ! -f "$SOURCE/.krita_android_custom_patch_applied" ]] && [[ -n "$(git -C "$SOURCE" status --porcelain --untracked-files=no)" ]]; then
        die "Refusing to overwrite unrecognized source changes"
    fi
    say "Restore managed source; keep the existing compiled CMake build directory"
    git -C "$SOURCE" restore .
fi
say "Apply customizations from this repository"
python3 "$REPO/apply_customizations.py" "$SOURCE"
python3 "$REPO/apply_tablet_update.py" "$SOURCE"
python3 - "$SOURCE" <<'PY'
import hashlib, pathlib, sys
root=pathlib.Path(sys.argv[1])
xml=(root/'krita/krita5.xmlgui').read_text()
cmake=(root/'krita/CMakeLists.txt').read_text()
actual=hashlib.md5(''.join(xml.split()).encode()).hexdigest()
expected=next(s.split('"')[1] for s in cmake.splitlines() if 'set(krita5_xmlgui_hash_expected' in s)
assert actual == expected, f'Toolbar checksum mismatch: {actual} != {expected}'
print('Toolbar checksum validated')
PY
git -C "$SOURCE" diff --check
cp "$REPO/build-in-krita-container.sh" "$SOURCE/build-in-krita-container.sh"
touch "$SOURCE/.krita_android_custom_patch_applied"
if [[ "$MODE" == patch ]]; then say "Done; run bash local-build.sh build next"; exit 0; fi
if [[ ! -d "$SOURCE/krita-deps-management/.git" ]]; then
    git clone --depth 1 https://invent.kde.org/packaging/krita-deps-management.git "$SOURCE/krita-deps-management"
fi
if [[ ! -d "$SOURCE/krita-deps-management/ci-utilities/.git" ]]; then
    git clone --depth 1 https://invent.kde.org/packaging/krita-ci-utilities.git "$SOURCE/krita-deps-management/ci-utilities"
fi
cd "$ROOT"
if [[ ! -d "$ROOT/persistent/deps/_install" ]]; then
    say "First run: download Android Qt dependencies"
    ./bin/bootstrap-krita-deps.sh --android=arm64-v8a
else
    say "Reusing downloaded Android Qt dependencies"
fi
NAME=""
if [[ -s .container_name ]]; then NAME="$(head -1 .container_name | tr -d '\r\n')"; fi
if [[ -n "$NAME" ]] && docker inspect "$NAME" >/dev/null 2>&1; then
    if [[ "$(docker inspect -f '{{.State.Running}}' "$NAME")" != true ]]; then
        docker start "$NAME" >/dev/null
    fi
    say "Reusing build container and incremental build outputs"
else
    say "First run: create Android build container"
    ./bin/build_image --android
    ./bin/run_container
    [[ -s .container_name ]] || die "No .container_name after starting container"
    NAME="$(head -1 .container_name | tr -d '\r\n')"
fi
docker exec -u appimage "$NAME" bash -lc 'test -f "$HOME/persistent/krita/build-in-krita-container.sh"' ||
    die "Container cannot access source directory"
if [[ "$MODE" == setup ]]; then say "Setup complete; next run bash local-build.sh build"; exit 0; fi
say "Build Android ARM64 APK with $JOBS jobs (incremental on future runs)"
docker exec -i -u appimage -e "KRITA_BUILD_JOBS=$JOBS" "$NAME" \
    bash -lc 'bash "$HOME/persistent/krita/build-in-krita-container.sh"'
say "APK output:"
[[ -d "$OUT" ]] || die "APK output folder not created"
find "$OUT" -maxdepth 1 -name '*.apk' -type f -printf '%p (%s bytes)\n'
if command -v wslpath >/dev/null 2>&1; then echo "Windows folder: $(wslpath -w "$OUT")"; fi
