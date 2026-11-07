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
