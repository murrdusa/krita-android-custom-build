#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Rebuild only Krita's pinned Qt Android platform plugin with window lifetime fixes."""
import hashlib
import os
from pathlib import Path
import subprocess
import tarfile
import urllib.request

REVISION = "a49b8cc76bb927d21dd768a25f0f37c24ad3986c"
ARCHIVE_SHA = "9daeb078f0733c4343609ee3bd7850ada1ed30404923d8f9ff7b0d7b920cf351"
BASE = f"https://invent.kde.org/szaman/qtbase/-/raw/{REVISION}/"


def replace(path, before, after):
    value = path.read_text()
    if value.count(before) != 1:
        raise RuntimeError(f"Unexpected pinned Qt source in {path.name}")
    path.write_text(value.replace(before, after))


def prepare(prefix, cache, jobs):
    plugin = prefix / "plugins/platforms/libplugins_platforms_qtforandroid_arm64-v8a.so"
    stamp = cache / "installed.sha256"
    version = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    digest = lambda: hashlib.sha256(plugin.read_bytes()).hexdigest()
    if stamp.exists() and stamp.read_text() == version + "\n" + digest():
        print("Qt Android window lifetime fix already installed", flush=True)
        return
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / "android.tar.gz"
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA:
        url = (f"https://invent.kde.org/szaman/qtbase/-/archive/{REVISION}/"
               f"qtbase-{REVISION}.tar.gz?path=src/plugins/platforms/android")
        archive.write_bytes(urllib.request.urlopen(url, timeout=120).read())
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA:
        raise RuntimeError("Qt Android source archive checksum mismatch")
    source = cache / "source"
    source.mkdir(exist_ok=True)
    with tarfile.open(archive) as package:
        for member in package.getmembers():
            relative = Path(*Path(member.name).parts[1:])
            if member.isfile() and relative.parts and ".." not in relative.parts:
                target = source / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(package.extractfile(member).read())
    for name in (".qmake.conf", "src/3rdparty/android/extract.h", "src/3rdparty/android/LICENSE"):
        target = source / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(urllib.request.urlopen(BASE + name, timeout=60).read())
    android = source / "src/plugins/platforms/android"
    replace(android / "qandroidplatformwindow.h",
            "    explicit QAndroidPlatformWindow(QWindow *window);",
            "    explicit QAndroidPlatformWindow(QWindow *window);\n"
            "    ~QAndroidPlatformWindow() override;")
    replace(android / "qandroidplatformwindow.cpp",
            "void QAndroidPlatformWindow::lower()",
            """QAndroidPlatformWindow::~QAndroidPlatformWindow()
{
    // Reparented or abruptly destroyed windows may miss setVisible(false).
    // Unregister while QWindow and its surface virtual methods are alive.
    platformScreen()->removeWindow(this);
}

void QAndroidPlatformWindow::lower()""")
    replace(android / "qandroidplatformscreen.cpp",
            """void QAndroidPlatformScreen::removeWindow(QAndroidPlatformWindow *window)
{
    if (window->parent() && window->isRaster())
        return;
""",
            """void QAndroidPlatformScreen::removeWindow(QAndroidPlatformWindow *window)
{
    // Membership reflects the original registration even after reparenting.
    // Destruction also calls this after an ordinary hide removed the window.
    if (!m_windowStack.removeOne(window))
        return;
    if (window->isRaster()) {
        m_rasterSurfaces.deref();
        setDirty(window->geometry());
    }
""")
    replace(android / "qandroidplatformscreen.cpp",
            """    Q_ASSERT(m_windowStack.contains(window));
    m_windowStack.removeOne(window);
    Q_ASSERT(!m_windowStack.contains(window));

    if (window->isRaster()) {
        m_rasterSurfaces.deref();
        setDirty(window->geometry());
    }
""",
            """    // Remove the real window before closing its decoration window,
    // whose callbacks can synchronously dispatch a queued screen redraw.
""")
    environment = os.environ.copy()
    environment["ANDROID_ABI"] = "arm64-v8a"
    environment.setdefault("ANDROID_NDK_ROOT", "/opt/android-tooling/android-ndk-r27d")
    subprocess.run([str(prefix / "bin/qmake"), "android.pro", "CONFIG+=release",
                    "CONFIG-=debug"], cwd=android, env=environment, check=True)
    subprocess.run(["make", f"-j{jobs}"], cwd=android, env=environment, check=True)
    built = source / "plugins/platforms" / plugin.name
    if not built.is_file():
        raise RuntimeError("Rebuilt Qt Android platform plugin missing")
    temporary = plugin.with_suffix(".studio-tmp")
    temporary.write_bytes(built.read_bytes())
    temporary.replace(plugin)
    stamp.write_text(version + "\n" + digest())
    print("Qt Android window lifetime fix installed", flush=True)


if __name__ == "__main__":
    prefix = Path(os.environ["KDECI_SHARED_INSTALL_PATH"])
    cache = Path(os.environ["KDECI_WORKDIR_PATH"]) / "studio-qt-platform"
    prepare(prefix, cache, int(os.environ.get("KRITA_BUILD_JOBS", "2")))
