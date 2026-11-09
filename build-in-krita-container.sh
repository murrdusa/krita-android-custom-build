#!/usr/bin/env bash
# EXPERIMENTAL. Run inside KDE Krita Android build container as appimage.
set -Eeuo pipefail
source /var/lib/ci-files/setup-environment.sh
export KDECI_ANDROID_ABI=arm64-v8a
export KDECI_WORKDIR_PATH=/home/appimage/appimage-workspace
export KDECI_SHARED_INSTALL_PATH=/home/appimage/appimage-workspace/deps/usr
export ANDROID_ABI="$KDECI_ANDROID_ABI"
export KRITA_INSTALL_PREFIX="$KDECI_SHARED_INSTALL_PATH"
SOURCE="$HOME/persistent/krita"
BUILDDIR="$KDECI_WORKDIR_PATH/krita/_build"

[[ -f "$SOURCE/.krita_android_custom_patch_applied" ]] || { echo 'Patched source missing' >&2; exit 1; }
[[ -f "$SOURCE/build-tools/ci-scripts/build-android-package.py" ]] || { echo 'Krita package script missing' >&2; exit 1; }
grep -q 'applicationIdSuffix ".debug"' "$SOURCE/packaging/android/apk/build.gradle" || { echo 'Debug variant missing' >&2; exit 1; }
grep -q 'applicationId "org.krita.mobilestudio"' "$SOURCE/packaging/android/apk/build.gradle" || { echo 'Mobile Studio package ID missing' >&2; exit 1; }
[[ -d "$KDECI_SHARED_INSTALL_PATH" ]] || { echo 'Qt/Krita Android deps missing' >&2; exit 1; }

# Krita 5.3.4 Android requires libavfilter, but some versions of KDE's
# prebuilt FFmpeg Android bundle omit it. Rebuild that dependency, including
# libavfilter, for the same Android ABI/toolchain and install prefix.
export PKG_CONFIG_PATH="$KDECI_SHARED_INSTALL_PATH/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
if ! pkg-config --exists libavfilter; then
  echo '## libavfilter absent from the prebuilt Android dependency bundle'
  echo '## Building FFmpeg with avfilter enabled, preserving animation support'
  FFMPEG_SOURCE="$SOURCE/krita-deps-management/ext_ffmpeg"
  FFMPEG_CMAKELISTS="$FFMPEG_SOURCE/CMakeLists.txt"
  [[ -f "$FFMPEG_CMAKELISTS" ]] || { echo "Missing FFmpeg dependency source: $FFMPEG_CMAKELISTS" >&2; exit 1; }
  if ! grep -q -- '-Davfilter=enabled' "$FFMPEG_CMAKELISTS"; then
    sed -i '/^[[:space:]]*-Davcodec=enabled/a\            -Davfilter=enabled' "$FFMPEG_CMAKELISTS"
  fi
  grep -q -- '-Davfilter=enabled' "$FFMPEG_CMAKELISTS" || { echo 'Failed to enable FFmpeg avfilter build option' >&2; exit 1; }
  FFMPEG_BUILDDIR="$KDECI_WORKDIR_PATH/ffmpeg-android-rebuild"
  mkdir -p "$FFMPEG_BUILDDIR" "$KDECI_WORKDIR_PATH/cache/downloads"
  cmake -S "$FFMPEG_SOURCE" -B "$FFMPEG_BUILDDIR" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DEXTERNALS_DOWNLOAD_DIR="$KDECI_WORKDIR_PATH/cache/downloads" \
    -DCMAKE_INSTALL_PREFIX="$KDECI_SHARED_INSTALL_PATH" \
    -DCMAKE_TOOLCHAIN_FILE="$SOURCE/krita-deps-management/tools/android-toolchain-krita.cmake" \
    -DANDROID_ABI="$KDECI_ANDROID_ABI" \
    -DSUBMAKE_JOBS="${KRITA_BUILD_JOBS:-2}"
  cmake --build "$FFMPEG_BUILDDIR" --target ext_ffmpeg --parallel "${KRITA_BUILD_JOBS:-2}"
fi
if ! pkg-config --exists libavfilter; then
  echo 'ERROR: libavfilter is still missing after the FFmpeg dependency rebuild' >&2
  find "$KDECI_SHARED_INSTALL_PATH/lib" -maxdepth 3 \( -iname '*avfilter*' -o -name 'libavcodec.pc' \) -print || true
  exit 1
fi
echo "## FFmpeg libavfilter ready: $(pkg-config --modversion libavfilter)"

mkdir -p "$BUILDDIR"
cd "$BUILDDIR"
# CMake's generated build system automatically reconfigures when CMake files
# actually change. Avoid unconditional reconfigure on every incremental build.
if [[ ! -f CMakeCache.txt || "${KRITA_RECONFIGURE:-0}" == 1 ]]; then
  echo '## Configuring Android CMake build tree'

cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DBUILD_TESTING=OFF \
  -DBUILD_WITH_QT6=OFF \
  -DCMAKE_INSTALL_PREFIX="$KDECI_SHARED_INSTALL_PATH" \
  -DCMAKE_TOOLCHAIN_FILE="$SOURCE/krita-deps-management/tools/android-toolchain-krita.cmake" \
  -DANDROID_ENABLE_STDIO_FORWARDING=ON \
  "$SOURCE"
else
  echo "## Reusing configured Android CMake build tree"
fi
# Shorter feedback loop for independent Krita-engine development.
# Real Android kritaui compiler only: no Gradle package or APK artifact.
if [[ "${KRITA_COMPILE_ONLY:-0}" == "1" ]]; then
  echo "## Syntax-checking independent Krita engine before the full native build"
  python3 - "$BUILDDIR/compile_commands.json" <<'PY'
import json
import pathlib
import shlex
import subprocess
import sys

database = pathlib.Path(sys.argv[1])
if not database.is_file():
    sys.exit(f"Missing CMake compile_commands.json: {database}")
entries = json.loads(database.read_text())
candidates = [
    entry for entry in entries
    if pathlib.Path(entry["file"]).name == "mobile_native_engine_compile_probe.cpp"
]
if len(candidates) != 1:
    sys.exit(f"Expected one native engine compile probe, found {len(candidates)}")

entry = candidates[0]
argv = entry.get("arguments") or shlex.split(entry["command"])
clean = []
skip_next = False
for token in argv:
    if skip_next:
        skip_next = False
        continue
    if token in ("-c", "-MD", "-MMD"):
        continue
    if token in ("-o", "-MF", "-MT", "-MQ"):
        skip_next = True
        continue
    clean.append(token)
clean.append("-fsyntax-only")
print("## Compiler:", clean[0], flush=True)
print("## Target: mobile_native_engine_compile_probe.cpp", flush=True)
subprocess.run(clean, cwd=entry["directory"], check=True)
print("## PASS: standalone Krita API headers type-checked", flush=True)
PY
  if [[ "${KRITA_NATIVE_PREFLIGHT_ONLY:-0}" == "1" ]]; then
    echo "## PASS: Android compiler preflight only (no target build, no APK)"
    exit 0
  fi
  echo "## Building real Android kritaui target (no APK packaging)"
  cmake --build . --target kritaui --parallel "${KRITA_BUILD_JOBS:-3}"
  echo "## PASS: Native Krita engine sources compiled into kritaui"
  exit 0
fi
cmake --build . --target install --parallel "${KRITA_BUILD_JOBS:-2}"
cd "$SOURCE"
python3 build-tools/ci-scripts/build-android-package.py --package-type debug

DEST="$HOME/persistent/krita-custom-apks"
mkdir -p "$DEST"
count=0
while IFS= read -r -d '' apk; do
  cp -v "$apk" "$DEST/"
  ((count+=1))
done < <(find "$KDECI_WORKDIR_PATH/krita/_packaging" -maxdepth 4 -type f -name '*.apk' -print0)
[[ "$count" -gt 0 ]] || { echo 'No APK found' >&2; exit 1; }
echo 'APK created. Android app ID: org.krita.mobilestudio.debug.'
