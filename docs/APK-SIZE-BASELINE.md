# Krita Mobile Studio APK size baseline

Captured 2026-11-08 from the local WSL Android build log, referenced by
[the latest compiler status issue](https://github.com/murrdusa/krita-android-custom-build/issues/1).
These are **compressed APK file sizes**, not installed bytes, resident RAM
or peak painting memory. The APK remains a debug build.

**Observed total: 173.5 MiB**, arm64-v8a debug APK
`krita-arm64-v8a-5.3.4-e7e52a7-debug.apk`.

| Main category | Compressed APK size |
| --- | ---: |
| Native libraries | 96.0 MiB |
| Bundled assets | 72.7 MiB |
| Android DEX bytecode | 3.1 MiB |
| Android resources | 0.8 MiB |

## Highest-value investigation targets

| Candidate | Observed size | Priority / compatibility constraints |
| --- | ---: | --- |
| Default brush resource bundle | 16.6 MiB | **Keep**; core brush presets are essential |
| ICU data | 12.5 MiB | Research Qt locale/Unicode dependencies before touching |
| FFmpeg libavcodec | 8.2 MiB | Audit video / animation import and export references |
| Native Krita UI library | 4.4 MiB | Eventually replace desktop-specific startup, but do not remove while mobile still needs Krita services |
| Pattern resources | 6.4 MiB | Do not remove until all patterns are available another way |
| Compiled translations | 4.7 MiB | Candidate for language-specific optional packages, not a universal removal |
| Comics + animation templates | 6.1 MiB | Candidate for optional content packs when template workflows exist |
| Donation images/media | 1.8 MiB | Candidate only after verifying no essential welcome or legal resource references |

## Safe development sequence

1. Keep the present fully functional engine/resources intact while
   validating the single-window Back transition, document lifecycle, and
   brush pressure sensitivity. Avoid drawing-data loss to save megabytes.
2. Compare `apk-size-report.json` and `apk-size-history.json` from the
   WSL output directory after every real APK rebuild. Reports include the
   largest individual files and bundled asset directories.
3. Distinguish code that must exist at runtime from desktop-only UI
   dependencies. Never remove Krita libraries just because the new UI
   does not visibly call them.
4. When safe, test a separate opt-in *lite package* against the exact
   features it removes, and preserve a full-compatible build as fallback.
5. Only move reductions into the default build after both functionality
   and output sizes are verified on Android devices.

No resources or libraries have been removed yet. APK publishing is paused;
the size reporter does not publish installation files.
