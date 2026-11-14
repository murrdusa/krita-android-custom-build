# Fast local Krita Android builds (Windows + WSL2)

This builds the modified Krita ARM64 Android APK on your Windows PC using **Ubuntu on WSL2 + Docker Desktop**, following KDE's official container build procedure. The downloaded Qt/Android dependencies, Docker container and compiled files are kept between runs.

Script: [local-build.sh](../local-build.sh), a thin wrapper around
[tools/build/local-build.sh](../tools/build/local-build.sh).

## One-time setup

1. Open **PowerShell as Administrator** and install Ubuntu for WSL2:

   ~~~powershell
   wsl --install -d Ubuntu
   ~~~

   Restart Windows if asked. Open Ubuntu once to create your Linux username and password. If Ubuntu is already installed, skip this step.

2. Install **Docker Desktop** on Windows. In Docker Desktop, enable **Use the WSL 2 based engine** and under **Settings > Resources > WSL Integration**, enable your **Ubuntu** distro. Start Docker Desktop.

3. Open the **Ubuntu terminal** (not PowerShell) and run:

   ~~~bash
   sudo apt update
   sudo apt install -y git python3
   docker info
   ~~~

   The final command must succeed. Otherwise check Docker Desktop's WSL integration setting.

4. Still in Ubuntu, run:

   ~~~bash
   git clone https://github.com/murrdusa/krita-android-custom-build.git ~/krita-tablet-customizations
   cd ~/krita-tablet-customizations
   bash local-build.sh build
   ~~~

   The first run downloads KDE's Android build dependencies, creates the build container and compiles Krita from scratch. The first run can still take a long time, and the current v2 patches are experimental.

## Every future build

After changing files in the GitHub customization repository:

~~~bash
cd ~/krita-tablet-customizations
git pull --ff-only
bash local-build.sh build
~~~

**Do not delete or recreate the local KDE build container** between runs. Keeping its CMake build directory makes this incremental: when only a few source files change, most previously compiled files can be reused.

Other commands:

- Run **bash local-build.sh setup** to prepare dependencies and container without compiling Krita.
- Run **bash local-build.sh patch** to reapply the source patches without compiling.
- Run **bash local-build.sh status** to list any existing APKs.

By default the build uses **6 parallel jobs**, a conservative starting point for a Ryzen 7 9800X3D with 32 GB RAM. If you've allocated enough RAM to WSL2, try:

~~~bash
KRITA_BUILD_JOBS=8 bash local-build.sh build
~~~

## Optional: allocate more memory to WSL2

In Windows, edit the file at **C:\Users\YOUR-WINDOWS-USERNAME\.wslconfig** and add:

~~~ini
[wsl2]
memory=24GB
processors=12
swap=8GB
~~~

Do not overwrite other WSL settings you have already configured. Save the file and run **wsl --shutdown** in PowerShell; then reopen Ubuntu.

Keep the project in Ubuntu's own Linux filesystem (under your home directory), **not /mnt/c**, for much better build I/O performance. Reserve approximately **100 GB** of SSD space initially, then review actual disk usage.

## Where the APK appears

On successful completion, the APK is copied into:

~~~text
~/krita-local-android/persistent/krita-custom-apks/
~~~

The script prints a Windows Explorer path. An equivalent WSL Explorer location is:

~~~text
\\wsl$\Ubuntu\home\YOUR-LINUX-USERNAME\krita-local-android\persistent\krita-custom-apks
~~~

Transfer the APK to your Android tablet. The v2 build is still experimental: retain your original Krita and back up artwork before installation.

## Important notes

- **Current status:** The v1 APK was successfully built; v2 contains new C++ and Java changes and may still fail compilation. Incremental builds help us fix these failures faster.
- **Docker error:** Ensure Docker Desktop is running and Ubuntu is enabled under WSL Integration.
- **Source changes:** The script invokes *git restore* on its **dedicated generated Krita source checkout**, so edit the patch scripts in this customization repository rather than hand-editing tracked files inside the generated source.
- **No automatic updates:** The setup does not alter your Android tablet or the Krita installed on Windows. It builds APKs locally.
- **Future ccache improvement:** Persistent incremental CMake builds provide the initial speedup. Compiler caching can be added later if measurement shows a need.

References: [Krita Android build guide](https://docs.krita.org/en/untranslatable_pages/building/build_krita_for_android.html) and [Krita Docker build guide](https://docs.krita.org/en/untranslatable_pages/building/build_krita_with_docker_on_linux.html).

## Independent Mobile Studio APK

The customized debug build now has Android application ID
`org.krita.mobilestudio.debug` and launcher name **Krita Mobile Studio**.
It installs separately from the official Krita and earlier
`org.krita.debug` test builds. This is an independent Android app
package but still uses the genuine Krita paint engine, resource
registries and libraries. Existing drawings/settings in the old
package are not automatically copied to the new app.

For a crash report, reproduce the problem with USB debugging enabled,
then run the following from a terminal with Android `adb` available:

```bash
adb logcat -d -v threadtime -b main -b crash > mobile-studio-crash.log
```

Share the relevant crash excerpt or log after checking it for other
applications' private details. Startup log lines contain `Mobile Studio:`
and `Standalone engine checkpoint:`. No automatic UI fallback is
activated after a crash; reopening starts the same native drawing host.
