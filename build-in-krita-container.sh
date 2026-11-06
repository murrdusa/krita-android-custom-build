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
grep -q 'applicationIdSuffix ".debug"' "$SOURCE/packaging/android/apk/build.gradle" || { echo 'Separate debug app ID not configured' >&2; exit 1; }
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
cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=OFF \
  -DBUILD_WITH_QT6=OFF \
  -DCMAKE_INSTALL_PREFIX="$KDECI_SHARED_INSTALL_PATH" \
  -DCMAKE_TOOLCHAIN_FILE="$SOURCE/krita-deps-management/tools/android-toolchain-krita.cmake" \
  -DANDROID_ENABLE_STDIO_FORWARDING=ON \
  "$SOURCE"
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
echo 'APK created. Verify the Android package ID is org.krita.debug.'
