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
# Publication is best-effort: the APK remains valid even if GitHub is offline
# or its CLI login lacks Release permissions.
publish_apk() {
    [[ "${KRITA_PUBLISH_APK:-1}" == 1 ]] || return 0
    [[ -d "$OUT" ]] || return 0
    command -v gh >/dev/null 2>&1 || {
        echo "NOTE: Install/authenticate gh once for automatic APK downloads."
        return 0
    }
    say "Publish Android APK to the stable GitHub download link"
    local revision
    revision="$(git -C "$REPO" rev-parse HEAD)"
    python3 "$REPO/krita_apk_publisher.py" \
        --output-dir "$OUT" \
        --revision "$revision" \
        --state-file "$ROOT/persistent/.krita-published-apk.json" ||
        echo "WARNING: GitHub APK upload failed; local APK remains available."
}
die() { echo "ERROR: $*" >&2; exit 1; }
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || die "Invalid KRITA_BUILD_JOBS"
for p in git python3 docker; do command -v "$p" >/dev/null || die "Missing $p. See LOCAL-BUILD.md"; done
docker info >/dev/null 2>&1 || die "Start Docker Desktop and enable Ubuntu under Resources > WSL Integration."
[[ -f "$REPO/apply_tablet_refinements.py" ]] || die "Please clone/update the Krita customization repository."
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
# A watcher tick or a docs-only commit must not trigger an APK rebuild.
# Skip ONLY when a previous successful APK uses identical app inputs.
APP_FINGERPRINT=""
INPUT_STAMP="$ROOT/persistent/.krita-last-successful-app-inputs"
if [[ "$MODE" == build ]]; then
    APP_FINGERPRINT="$(python3 "$REPO/krita_build_cache.py" fingerprint "$REPO")"
    if [[ "${KRITA_FORCE_BUILD:-0}" != 1 && -s "$INPUT_STAMP" ]] &&
       [[ "$(cat "$INPUT_STAMP")" == "$APP_FINGERPRINT" ]] &&
       [[ -d "$OUT" ]] &&
       [[ -n "$(find "$OUT" -maxdepth 1 -type f -name '*.apk' -size +0c -print -quit)" ]]; then
        say "No Android application changes; reusing previous successful APK"
        echo "Skipped compilation and packaging. Override with KRITA_FORCE_BUILD=1."
        publish_apk
        exit 0
    fi
fi
PATCH_MTIME_SNAPSHOT="$ROOT/persistent/.krita-patch-mtimes.json"
if [[ ! -d "$SOURCE/.git" ]]; then
    [[ ! -e "$SOURCE" ]] || die "Existing non-Git Krita source: $SOURCE"
    say "First run: download Krita source v6.0.4"
    git clone --depth 1 --branch v6.0.4 https://github.com/KDE/krita.git "$SOURCE"
    touch "$SOURCE/.krita_android_custom_patch_applied"
else
    if [[ ! -f "$SOURCE/.krita_android_custom_patch_applied" ]] && [[ -n "$(git -C "$SOURCE" status --porcelain --untracked-files=no)" ]]; then
        die "Refusing to overwrite unrecognized source changes"
    fi
    say "Restore managed source; preserve unchanged file timestamps"
    python3 "$REPO/krita_build_cache.py" snapshot "$SOURCE" "$PATCH_MTIME_SNAPSHOT"
    git -C "$SOURCE" restore .
fi
say "Apply customizations from this repository"
python3 "$REPO/apply_customizations.py" "$SOURCE"
python3 "$REPO/apply_tablet_update.py" "$SOURCE"
python3 "$REPO/apply_tablet_refinements.py" "$SOURCE"
python3 "$REPO/apply_tablet_polish.py" "$SOURCE"
python3 "$REPO/apply_tablet_cleanup.py" "$SOURCE"
python3 "$REPO/apply_reference_touch_fix.py" "$SOURCE"
python3 "$REPO/apply_compact_skin.py" "$SOURCE"
python3 "$REPO/apply_wheel_rotation.py" "$SOURCE"
python3 "$REPO/apply_diamond_rotation.py" "$SOURCE"
python3 "$REPO/apply_gallery_tabs.py" "$SOURCE"
python3 "$REPO/apply_home_mobile.py" "$SOURCE"
python3 "$REPO/apply_mobile_recovery.py" "$SOURCE"
python3 "$REPO/apply_mobile_touch_scroll.py" "$SOURCE"
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
# Avoid touching an unchanged generated build script.
if ! cmp -s "$REPO/build-in-krita-container.sh" "$SOURCE/build-in-krita-container.sh"; then
    cp "$REPO/build-in-krita-container.sh" "$SOURCE/build-in-krita-container.sh"
fi
python3 "$REPO/krita_build_cache.py" restore "$SOURCE" "$PATCH_MTIME_SNAPSHOT"
touch "$SOURCE/.krita_android_custom_patch_applied"
if [[ "$MODE" == patch ]]; then say "Done; run bash local-build.sh build next"; exit 0; fi
if [[ ! -d "$SOURCE/krita-deps-management/.git" ]]; then
    git clone --depth 1 https://invent.kde.org/packaging/krita-deps-management.git "$SOURCE/krita-deps-management"
fi
if [[ ! -d "$SOURCE/krita-deps-management/ci-utilities/.git" ]]; then
    git clone --depth 1 https://invent.kde.org/packaging/krita-ci-utilities.git "$SOURCE/krita-deps-management/ci-utilities"
fi
cd "$ROOT"
# Bootstrap fetches many dependency packages. A successful container setup
# from an earlier version of this script also proves bootstrap already ran.
# Use a success marker instead of guessing the install directory layout.
DEPS_READY="$ROOT/persistent/.android-arm64-deps-bootstrap-complete"
if [[ ! -f "$DEPS_READY" && ! -s "$ROOT/.container_name" ]]; then
    say "First run: download Android Qt dependencies"
    ./bin/bootstrap-krita-deps.sh --android=arm64-v8a
    touch "$DEPS_READY"
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
if [[ "$MODE" == build ]]; then
    printf '%s\n' "$APP_FINGERPRINT" > "$INPUT_STAMP.tmp"
    mv "$INPUT_STAMP.tmp" "$INPUT_STAMP"
fi
find "$OUT" -maxdepth 1 -name '*.apk' -type f -printf '%p (%s bytes)\n'
publish_apk
if command -v wslpath >/dev/null 2>&1; then echo "Windows folder: $(wslpath -w "$OUT")"; fi
