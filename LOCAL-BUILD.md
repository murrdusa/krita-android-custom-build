# Fast local Krita Android builds (Windows + WSL2)

This builds the same modified Krita ARM64 Android APK as GitHub Actions, but **on your Windows PC**, using Ubuntu on WSL2 and KDE's Docker build environment. The downloaded dependencies, Docker container, and CMake compilation outputs are **kept between runs**.

The setup and build script is [local-build.sh](local-build.sh).

## One-time setup on Windows

1. Open **PowerShell as Administrator** and install WSL Ubuntu:

   `powershell
   wsl --install -d Ubuntu
   `

   Restart Windows if prompted, open **Ubuntu**, and create your Linux username/password. If Ubuntu is already installed, skip this.

2. Install **Docker Desktop for Windows**, enable **Use the WSL 2 based engine**, and in **Settings > Resources > WSL Integration**, enable your **Ubuntu** distro. Start Docker Desktop.

3. Open the **Ubuntu terminal** (not regular PowerShell) and run:

   `bash
   sudo apt update
   sudo apt install -y git python3
   docker info
   `

   The last command must succeed. If it says it cannot connect to Docker, open Docker Desktop and enable Ubuntu integration.

4. Still in Ubuntu, clone the custom Krita repository and start the build:

   `bash
   git clone https://github.com/murrdusa/krita-android-custom-build.git ~/krita-tablet-customizations
   cd ~/krita-tablet-customizations
   bash local-build.sh build
   `

   The first run sets up KDE's build environment and downloads all Android and Qt dependencies. It also builds the whole app; this will still be lengthy. The script prints the APK's location if successful.

**Note:** The newer v2 source patch is experimental and may still have compilation errors. The script makes those errors easier to fix iteratively; it doesn't guarantee that v2 compiles successfully on the first try.

## Updating Krita after changing the customization scripts

In Ubuntu:

`bash
cd ~/krita-tablet-customizations
git pull --ff-only
bash local-build.sh build
`

The script re-applies the latest patches from your repository, but keeps the downloaded Android dependencies and existing container/build directory. Only affected files and their dependents need recompilation when the build system permits it.

You can also run:

- `bash local-build.sh setup` — initialize Docker and dependencies without compiling Krita.
- `bash local-build.sh patch` — reapply sources without compiling.
- `bash local-build.sh status` — show output location and any existing APKs.

By default, the script uses **6 parallel compiler jobs**. On a Ryzen 7 9800X3D and 32 GB RAM, a reasonable starting value is 6; once WSL has enough available memory you can try:

`bash
KRITA_BUILD_JOBS=8 bash local-build.sh build
`

## WSL memory and storage (optional)

If builds seem memory constrained, create or edit `C:Users<your-Windows-username>.wslconfig` and add:

`ini
[wsl2]
memory=24GB
processors=12
swap=8GB
`

Save it, then run `wsl --shutdown` in **PowerShell** and start Ubuntu again. This is a machine-wide WSL setting; do not overwrite other custom settings you may already have.

Keep the build project in Ubuntu's **Linux home filesystem** (`~/krita-local-android`), not under `/mnt/c`. Reserve at least ~100 GB of free SSD space until actual disk usage is known.

## Getting the APK into Windows

Successful builds are copied to:

`~/krita-local-android/persistent/krita-custom-apks/`

The script also prints a Windows-compatible path. You can browse it from Explorer with:

`\wsl$Ubuntuhome<your-Linux-username>krita-local-androidpersistentkrita-custom-apks`

Transfer the APK to the tablet, then install it there. Since it's experimental, **preserve existing installations and backup drawings first**.

## Troubleshooting

- **Docker connection error:** enable Ubuntu WSL integration and start Docker Desktop.
- **No space:** remove unused files from the WSL disk; avoid deleting Docker images or the build directory if you want fast incremental builds.
- **Patch fails:** run `git pull --ff-only` in the customization repository and retry.
- **C++ build fails:** the compiler output is a real source error; the next local run after a patch fix should reuse previously successful compiled objects.
- **Power button/volume button shortcuts:** these apply to the tablet APK, not Windows.

Important: the script calls `git restore .` inside its **dedicated managed Krita source checkout** to reapply customizations cleanly. Do not make hand-edits in that checkout. Put persistent source edits in this customization repository.

Official references: [Krita Android compilation guide](https://docs.krita.org/en/untranslatable_pages/building/build_krita_for_android.html), [Krita Docker guide](https://docs.krita.org/en/untranslatable_pages/building/build_krita_with_docker_on_linux.html).
