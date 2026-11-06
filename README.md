# Krita Android custom build (experimental)

This repository holds a source patch and a manually triggered GitHub Actions workflow for **Krita 5.3.4 Android** (source tag `v6.0.4`).

## Requested features
- Assignable **one-finger double-tap** gesture for **Mirror View**, via Krita Canvas Input Settings.
- Smaller Layers docker for a narrower left sidebar.
- Existing Canvas Only controls can hide the menu bar/document chrome while retaining toolbars and dockers.
- Experimental local-folder `.kra` project gallery in the Welcome page.

## IMPORTANT
**No installable APK has been built or tested.** Code and workflow are experimental. Android's Storage Access Framework may restrict gallery access to folders. Keep your existing Play Store Krita and artwork. Any APK must be verified to have a separate `org.krita.debug` package ID before installation.

## Building
Go to **Actions → Krita Android custom build (experimental) → Run workflow**. Run with `validate` first (source patch and basic checks); use `build` only after validation succeeds. The Android build is large and may exceed GitHub runner resources. If a run fails, inspect its first failing step and share the error.

The patch script applies to the official KDE Krita `v6.0.4` source tree. Its changes have not been verified by a full compilation.
