# Finger contact size as simulated pressure — prototype v3

**Opt-in and off by default**. Krita's native brush engines, tablet pressure, multitouch gestures, and documents remain authoritative. No handwriting, touch coordinates, images, files, or gesture paths are recorded by this feature.

## How to enable and calibrate

In **Mobile Studio → More**, enable **Experimental finger pressure (contact area)**, then open **Calibrate finger pressure (minimum / maximum)**. The same calibration dialog is available from the opt-in stationary canvas UI.

The dialog now has:

- **Minimum / Maximum contact:** editable logical-pixel contact diameters corresponding to 0% and 100% simulated pressure. Initial defaults remain 8px and 24px.
- **Response curve:** a slider from 0.30–3.00, default 1.00. Values below 1.00 emphasize smaller contact changes; values above 1.00 emphasize larger contacts. Changes apply from the next finger stroke.
- **Stability:** 0–100%, default 50%. A higher value filters small variations more heavily; lower values feel faster.
- **Prefer Android raw touch size:** enabled by default *when the overall experimental finger-pressure toggle is on*. Turn it off to compare Qt's contact ellipse with Android MotionEvent measurements. If raw samples cannot be matched, painting automatically falls back to Qt. If neither provides usable dimensions, Krita's original input pressure is used.
- **Capture minimum / maximum:** touch the diagnostic pad with a small/light contact, or a broad/flattened contact, and lift. The median contact diameter from that touch sets the corresponding endpoint.
- **Capture medium contact — fit 50% pressure:** after minimum and maximum, touch the pad at a comfortable middle contact. The dialog computes a response exponent so that the measured medium contact corresponds to approximately 50%. The result is clamped to the safe slider range.
- **Live contact graph:** shows recent reported contact diameter values. The status displays raw source, mapped pressure, total sample count and the number of distinct quarter-logical-pixel contact levels seen so far. After each capture it also reports **within-touch spread** and the number of distinct levels during that particular contact. Holding the finger still makes this a rough noise estimate; intentional finger movement contributes to spread. These numbers help distinguish genuine sensor resolution from smoothing artifacts.
- **Save / Cancel:** Save persists all values and applies them on the next finger stroke. Cancel does not change saved values. The content scrolls on short screens while Save and Cancel remain accessible.

Settings keys:

`MobileStudio/fingerContactPressureV1` (enable)

`MobileStudio/fingerContactMinDiameterV2`, `MobileStudio/fingerContactMaxDiameterV2` (contact endpoints)

`MobileStudio/fingerPressureCurveGammaV3`, `MobileStudio/fingerPressureStabilityV3`, `MobileStudio/fingerPressureRawAndroidV3` (response and input)

The settings are snapshotted at stroke start to avoid sudden jumps mid-stroke. The pressure proxy remains separate from persistent/stationary canvas-host experiments.

## Pressure response

Let `D` be the effective finger contact diameter, the geometric mean of the reported major and minor axes. The input normalizes as:

`x = clamp((D - Dmin)/(Dmax - Dmin), 0, 1)`

and the output is:

`P = x^gamma`

The native brush receives the full 0–100% pressure range. Thumb and pinky can start at different pressure if their measured contacts differ. Krita presets can apply further pressure-to-size or opacity curves.

The adaptive filter estimates small-sample noise and chooses a response coefficient based on the pressure error, the estimated noise, and the interval between samples. Large legitimate movements respond quickly; small jitter is smoothed. It does **not** impose the previous fixed 1.5% deadband or 16–96% output limits. Sustained calibrated endpoints can reach exactly 0% and 100%.

A response curve cannot recover detail the screen never measures: a sensor that exposes five distinguishable contact sizes still offers only five independently measured levels, even if interpolation looks smooth.

## Raw Android MotionEvent support

A small addition to Krita's existing Android `MainActivity.dispatchTouchEvent` reads `getTouchMajor`, `getTouchMinor`, `getHistoricalTouchMajor`, and `getHistoricalTouchMinor` for **single-finger touches only**. It passes contact size, pointer ID, event uptime, density, and whether a sample is historical to a native size-only ring buffer. It never consumes the Android event; `super.dispatchTouchEvent(event)` still controls Krita's input.

When `KoPointerEvent` asks for a finger's pressure, the converter selects raw data only if the pointer ID and event timestamp match within 50 ms. The buffer is limited to 96 samples; no coordinates or paths are retained. All pen and multi-finger events continue through the original routes.

Raw Android size is normalized by Android display density, which may not exactly match Qt's logical-pixel scale on every device. This is why the live diagnostics name the measurement source and the raw preference can be disabled. The Android Java-to-C++ path has compiled, but real size values still need phone testing.

## On-device tests

1. Choose a Krita preset with pressure-controlled size. Enable experimental finger pressure and use a disposable canvas.
2. In the calibration dialog, capture a light/small finger contact, then a broad/flat contact. Capture a medium contact and try gamma values below/above 1.0. Save.
3. Draw separate pinky/thumb strokes. Look for a meaningful difference in initial brush width.
4. Flatten and unflatten a finger slowly. Adjust Stability to balance smoothness with responsiveness.
5. On the diagnostic pad, vary light/medium/firm contact for several seconds. Watch the trace and distinct sensor levels. Try Android raw preference on and off.
6. Use **More → Copy Mobile Studio diagnostics** to compare calibrated settings, last contact source, and level count. If Qt or Android reports no usable touch size, the feature falls back rather than inventing pressure.
7. Check S Pen pressure, palm contact, two-finger pan/zoom, rotation, and normal brushes with the experimental toggle off. They should be unchanged.

## Source and verification

- `mobile_core/finger_pressure_settings.h` — calibrated curve and adaptive coefficient.
- `mobile_core/finger_contact_pressure.h` — stroke state, smoothing, diagnostics and raw fallback.
- `mobile_core/finger_raw_bridge.h` — thread-safe raw sample ring buffer.
- `mobile_core/finger_pressure_calibration.h` — scrollable calibration UI and live sensor-level graph.
- `apply_finger_contact_pressure.py` and `apply_finger_raw_motion.py` — optional Krita Android source patches.
- `tests/qt_finger_contact_pressure.cpp`, `tests/qt_finger_pressure_calibration.cpp`, and `tests/qt_finger_raw_bridge.cpp` — synthetic regression tests. These do not prove that a specific phone exports usable contact data.

Automatic APK uploads remain paused.
