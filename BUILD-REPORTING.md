# GitHub build reporting (WSL)

This companion runs alongside the **existing** `krita_build_watcher.py`. Do not replace or stop the working watcher. The companion reads `.build-watcher/status.json` every 10 seconds and publishes results.

## One-time setup

In Ubuntu:

```bash
sudo apt update
sudo apt install -y gh
gh auth login
gh auth status
cd ~/krita-tablet-customizations
git pull --ff-only
python3 krita_build_reporter.py start
python3 krita_build_reporter.py status
```

Log in to GitHub.com as the account with access to `murrdusa/krita-android-custom-build`; choose HTTPS and browser authorization when prompted. The reporter needs permission to create/edit repository issues and commit statuses. It does not need an OpenAI API key.

The reporter can be running alongside the existing build watcher. The old watcher keeps its own incremental-build loop.

To test the publisher against the latest completed build:

```bash
python3 krita_build_reporter.py once
```

To trigger a **new** build, use the existing command:

```bash
python3 krita_build_watcher.py build-now
```

## What appears on GitHub

- **Commit status:** `local/android-wsl-build` (pending, success, or failure), attached to the exact source commit.
- **Stable report issue:** `[Local Android build] Latest compiler result`, updated on each completed build.
- **Failure issue:** `[Local build] Android compile failed at <sha>`, containing actionable compiler excerpts.

You can inspect the latest report from GitHub Issues even when away from the terminal. ChatGPT can read the issue/status during an active conversation, but GitHub does not automatically wake up a completed ChatGPT response.

### Controls

```bash
python3 krita_build_reporter.py status
python3 krita_build_reporter.py stop
tail -f .build-watcher/reporter.log
```

This is a separate process: if WSL shuts down, the reporter stops. Start it again when you reopen Ubuntu or add a Windows scheduled task.

**Privacy:** GitHub issues in a public repository are public. Build excerpts may include local file paths and source snippets. Complete build logs remain local and are not uploaded.

**Troubleshooting:** If the reporter log says GitHub authentication failed, run `gh auth login` again. If it cannot publish status, check the GitHub account's repository permissions. It retries reporting the same build during the next poll if a request fails.


## Live build output in Ubuntu / Windows Terminal

The background watcher continues compiling even when its terminal is closed.
For a *live viewer* that switches between builds automatically, open an Ubuntu
terminal and run:

```bash
cd ~/krita-tablet-customizations
git pull --ff-only
python3 krita_build_live.py
```

Keep this viewer tab open while playing games or working elsewhere. It shows
the latest build output as it is written, the revision being compiled, elapsed
time, completion status, and a heartbeat approximately every 25 seconds.
If the compiler is silent for two minutes it prints a warning, **not** a
failure claim. When the watcher starts another build, the viewer automatically
follows its new log. Press Ctrl+C to close only the viewer; compilation and
GitHub reporting continue in the background.

To show only the useful progress lines and errors:

```bash
python3 krita_build_live.py --quiet
```

You can also use a separate Windows Terminal tab for the viewer. No special
permissions or new Python packages are required.


## Incremental build optimization

The local watcher still polls GitHub for every commit, but the build script
now fingerprints app-affecting inputs and compares them to the last successful
APK. Documentation, GitHub workflows, and monitoring/reporting-only changes
are skipped without recompiling or repackaging. The existing APK is reused.

For app changes, byte-for-byte comparison restores original modification times
for patched Krita files whose contents did not change. This prevents the
git restore / patch-application cycle from making unchanged C++ files appear
new to the compiler. The existing Android CMake configuration is also reused;
CMake automatically regenerates when relevant build inputs actually change.

The first measured optimized build completed in 71.7 seconds, compared with
278.7 seconds for the preceding build. Other builds may take longer when
significant source files, dependencies, or APK packaging change.

Force a build for unchanged inputs:
    KRITA_FORCE_BUILD=1 bash local-build.sh build

Force both compilation and CMake reconfiguration:
    KRITA_FORCE_BUILD=1 KRITA_RECONFIGURE=1 bash local-build.sh build

These changes do not delete the persistent Docker container or its build
directory, and do not alter the running watcher. The watcher's earlier
--jobs 2 setting reduces CPU load while gaming; increasing it may speed
compilation at the cost of higher background resource use.

## Upload an APK only when requested

The local WSL watcher continues compiling changes and publishing build results
without uploading the APK. **APK uploads are opt-in**, not automatic on success.

A chat request like **"Upload the latest Krita APK"** is implemented by
updating the tracked [APK-UPLOAD-REQUEST.md](APK-UPLOAD-REQUEST.md) file to
a new unique `request-...` ID. The watcher sees that GitHub commit, checks
that the compiled APK is current, and publishes it to the stable Release.
As this request file is Markdown, the incremental build fingerprint ignores it:
**an upload-only request does not itself force a new Android compilation**.

- Stable APK download (after a requested publish):
  https://github.com/murrdusa/krita-android-custom-build/releases/download/krita-android-latest/krita-android-custom-arm64-debug.apk
- Latest Release page:
  https://github.com/murrdusa/krita-android-custom-build/releases/tag/krita-android-latest
- Confirm publication with `local/android-apk-published` commit status
  or verify that the GitHub Release has an attached APK asset.

The request is marked complete on the WSL computer **only after GitHub
confirms the APK asset**. Subsequent builds do not re-upload for the same
request ID. Failed uploads can be retried on a subsequent watcher run.
Normal build status reporting remains unchanged.

No manual computer interaction is needed for an ordinary upload request
while the existing WSL watcher and GitHub CLI authentication are available.
If WSL is shut down, the request waits until the watcher runs again.
A published APK remains downloadable when the computer is off.

To disable uploads altogether, set `KRITA_PUBLISH_APK=0` in WSL.
**This repository and its release APK are public.** Only the generated APK
is uploaded, not drawings or credentials.
