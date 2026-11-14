# On-demand Krita Android APK upload

UPLOAD_REQUEST_ID=request-20261114-1031-user-apk
UPLOAD_RELEASE_TAG=krita-mobile-studio-preview-v11

**ONE-TIME UPLOAD REQUESTED on November 14, 2026.** Normal source edits and WSL builds must not trigger additional APK publishing. A later upload requires a new explicit request ID.

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
