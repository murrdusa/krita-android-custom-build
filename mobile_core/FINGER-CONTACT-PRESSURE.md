# Finger contact area as simulated brush pressure (prototype v1)

This is an opt-in experiment for Krita Android. **Off by default.**
It preserves Krita's existing touch/pen event routing and never changes
`QTabletEvent` pressure for an S Pen or other stylus.

## Enable / disable

In **Mobile Studio**, open a painting document and choose:

**More → Experimental finger pressure (contact area)**

The checkable action stores the setting in
`QSettings: MobileStudio/fingerContactPressureV1`. It applies to the
**next finger stroke** and stays set until switched off. When off, Krita
uses the exact original touch pressure. This also affects finger painting
outside Mobile Studio while enabled because the input converter is shared.

The option works with brushes that actually respond to the **Pressure**
input sensor. Use a clearly pressure-sensitive brush to try it.

## Current pressure model

- For one touchscreen contact, sample Qt 5's
  `QTouchEvent::TouchPoint::ellipseDiameters()` in logical screen pixels.
  These describe the contact ellipse reported by the Android/Qt driver.
- Use `sqrt(width × height)` as a contact-diameter proxy, compared to
  the **first valid sample in that stroke**. No assumptions about physical
  force, calibrated sensor area or device DPI are made.
- A new stroke starts near pressure **0.52**.
  `target = clamp(0.52 + 1.15 × ln(diameter / startingDiameter), 0.16, 0.96)`.
  The target increases for a wider/larger finger contact.
- Apply a 0.015 deadband and clamp jumps in target to ±0.18 before a
  0.26-step exponential moving average. Duplicate touch samples do not
  repeatedly advance the filter.
- When no meaningful contact dimensions are provided, preserve Krita's
  original touch pressure (or the last valid smoothed value within the
  same stroke) rather than inventing a change.
- The filter resets at stroke end, after a large event-time gap, or when
  the point ID changes. Touch gestures with multiple simultaneous points
  and touchscreen contacts marked with Qt's Pen flag are excluded.

This is **not actual force sensing**. Contact ellipse data may be zero,
quantized or unrelated to force on some devices, including devices where
Android itself reports a `MotionEvent.getTouchMajor()` but Qt does not
expose it. Also, the relative per-stroke baseline cannot vary the first
dab's pressure; that could be calibrated in a future version.

## Device testing

1. Keep a disposable drawing and a pressure-sensitive brush selected.
2. Paint with a finger while the option is disabled. Observe the line
   width/opacity, then enable the option.
3. Make a long stroke and deliberately roll/flatten the finger to change
   contact area during the stroke. Observe whether brush width/opacity
   changes smoothly. Dragging in the same way with the S Pen should
   behave identically with the option on or off.
4. Pinch/zoom, pan, use two fingers, save/undo, and switch Mobile Studio
   modes; these interactions should be unchanged.
5. After a completed finger stroke, choose
   **More → Copy Mobile Studio diagnostics** and inspect the values:
   `Last finger area data` and `Last finger contact diameter range`.
   If `unavailable`, the Qt touch data was missing; we should explore
   capturing Android's raw `getTouchMajor/getTouchMinor` as a second
   implementation, not alter painting pressure blindly.
6. If the brush jitters or the simulated response is too weak/strong,
   disable the option and retain the diagnostics. We can later expose
   smoothing strength, sensitivity, calibration and pressure floor.

## Safety and verification

- The patch is in `apply_finger_contact_pressure.py` and adds one
  isolated Android-only call to Krita's touch pressure accessor in
  `libs/flake/KoPointerEvent.cpp`.
- UI toggle is in `mobile_core/standalone_host.h`.
- `mobile_core/finger_contact_pressure.h` is copied into
  `libs/flake/finger_contact_pressure.h` at local build time.
- `tests/qt_finger_contact_pressure.cpp` exercises synthetic touch
  geometry, rising/falling pressure, smoothing, idempotence, settings
  disabled, missing data, stylus flags, multi-finger fallback and
  diagnostic availability without needing an APK.
- Android compilation and on-device rendering are separate validation
  steps. The synthetic Qt test cannot prove the phone's touchscreen
  reports meaningful contact dimensions.
- APK publication stays disabled; this patch does not upload anything.
