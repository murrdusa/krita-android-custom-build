# Finger contact area as simulated brush pressure (prototype v2)

This is an opt-in experiment for Krita Android, **disabled by default**.
Krita's native touch/pen event routing is unchanged. The existing
`MobileStudio/fingerContactPressureV1` enable switch is preserved.

## Enable and calibrate

Open a painting document in **Mobile Studio → More**:

- **Experimental finger pressure (contact area):** enable or disable the
  feature. Changes apply at the start of the next finger stroke.
- **Calibrate finger pressure (minimum / maximum):** open the new dialog.
  Use **Capture minimum** and gently touch the pad with a small contact;
  lift your finger to record its median contact diameter. Use
  **Capture maximum**, flatten the finger or use a broad contact on the
  pad, then lift. The dialog also accepts manual values, in Qt logical
  pixels, if the touchscreen does not provide meaningful contact sizes.
  Press **Save** to store the range; Cancel leaves the prior range intact.

The minimum and maximum are the *contact diameters* at which simulated
pressure reaches 0% and 100%. Maximum must exceed minimum by at least
1 logical pixel. The initial approximate defaults are **8 and 24 logical
pixels**; actual hardware may require substantially different values.

Settings are saved as:

- `MobileStudio/fingerContactMinDiameterV2`
- `MobileStudio/fingerContactMaxDiameterV2`

The calibration is shared between Mobile Studio and finger painting in
Krita's other UI modes, because the input converter is shared. Each
stroke snapshots the current calibration on its first touch sample:
changing values while drawing cannot make the middle of an existing
stroke jump.

## Absolute contact-area pressure mapping

Krita's Qt 5 `QTouchEvent::TouchPoint::ellipseDiameters()` returns the
reported major/minor contact diameter in logical pixels. The prototype
uses `D = sqrt(width × height)`; this is a contact-size proxy, **not
physical force measurement**.

`P = clamp((D - min) / (max - min), 0, 1)`

**A thumb can now begin at higher simulated pressure than a pinky**,
provided the device reports a larger contact area and the calibration
range spans both. The first dab uses its *absolute measured diameter*,
not a fixed per-stroke baseline. Larger/smaller contacts during the
stroke change the target pressure. At 0%, some brush presets may create
a faint mark or no mark at all, because Krita's own pressure curves
control the final width and opacity.

Smoothing remains in place: changes under 0.015 are ignored, sudden
changes are capped at 0.18 per sample before applying a 0.26-step
low-pass filter. Duplicate touch events do not repeatedly advance it.
The first valid sample initializes to the calibrated target immediately,
so it does not begin at a false 52% pressure. There is **no artificial
minimum or maximum pressure floor/ceiling** beyond Krita's native
0–100% pressure domain.

When valid contact size is unavailable, the original Krita touch
pressure is used; during a stroke the last valid smoothed reading is
retained instead of introducing an invalid spike. Multiple fingers,
screen contacts flagged as a pen, and `QTabletEvent` stylus pressure
retain their original behavior.

## Testing on an Android device

1. With a disposable drawing, enable the feature and select a brush
   whose size or opacity responds to Krita's pressure sensor.
2. Set the minimum using a small fingertip contact (0% pressure) and
   the maximum using a wide, flattened fingertip contact (100% pressure).
   Save the calibration. Use a brush with visible low-pressure behavior
   when testing near the minimum.
3. Draw separate strokes using a pinky and a thumb. The thumb should
   produce a thicker/stronger stroke from the **first dab**, provided
   their observed contact diameters differ.
4. Flatten and unflatten your finger during a long stroke. Confirm the
   response is gradual rather than jittery.
5. Check your pen and two-finger gestures with the feature on and off.
   They should behave the same.
6. Use **More → Copy Mobile Studio diagnostics** to check the saved
   minimum/maximum and the most recent stroke's observed diameter range.
   If the contact data is unavailable, adjust values manually only if
   the hardware reports any usable dimensions; otherwise future versions
   will need to inspect raw Android `getTouchMajor/getTouchMinor` data.

## Architecture and verification

- `mobile_core/finger_pressure_settings.h` holds validated calibration
  defaults and the pure absolute mapping formula, shared between UI
  and Krita's touch input converter.
- `mobile_core/finger_contact_pressure.h` reads calibration once per
  stroke, smooths touch pressure, and preserves all previous exclusions.
- `mobile_core/finger_pressure_calibration.h` provides the modal
  touch-pad calibration and manual number controls.
- `apply_mobile_shell.py` copies the UI/settings headers into Krita UI;
  `apply_finger_contact_pressure.py` copies the input/settings headers
  into Krita's flake painting library.
- The synthetic Qt tests verify initial thumb/pinky differences,
  calibrated bounds, smoothing, invalid settings fallback, persistence,
  disabled mode, multitouch and stylus preservation. A separate
  Qt Widgets test verifies the calibration dialog and saved values.

Android compilation is not proof that contact-ellipse measurements are
supported by a particular phone. Actual pressure response and
calibration touch-pad input must still be tested on-device.

APK publishing remains paused. No painting, document or source assets
are removed by this prototype.
