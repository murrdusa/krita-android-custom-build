# On-demand Krita Android APK upload

UPLOAD_REQUEST_ID=none
UPLOAD_RELEASE_TAG=krita-mobile-studio-preview-v11

**PAUSED at user request.** Keep `UPLOAD_REQUEST_ID=none` during normal source edits and WSL builds. Do not resume APK publishing unless explicitly requested.

Normal local builds must **never** upload an APK automatically. The build
watcher reads this file on each GitHub revision but publishes only when the
UPLOAD_REQUEST_ID starts with `request-` and differs from its last
successfully published request ID.

To upload at the user's explicit request in chat, replace `none` with a
**new unique request ID** such as `request-000000000001`. For later requests,
change the ID again. Do not update this ID as part of normal app changes.

The existing WSL watcher reuses a compiled APK if no application inputs changed.
The uploader marks each request complete only after verifying the GitHub
Release asset exists, avoiding repeated uploads.

This preview uses a separate release tag so that the previously working
`krita-android-latest` APK remains downloadable.
