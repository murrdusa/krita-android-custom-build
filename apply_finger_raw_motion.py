#!/usr/bin/env python3
"""Capture Android finger contact major/minor + historical samples for Krita.

Applied AFTER apply_tablet_refinements.py and apply_finger_contact_pressure.py.
Does not intercept or consume touch events; Qt retains the original gesture,
stylus and brush routing. The Java trampoline stores size only, never points.
"""
from pathlib import Path
import sys


def replace_once(source, old, new, label):
    n=source.count(old)
    if n!=1:
        raise RuntimeError(f"Raw finger bridge {label}: expected 1 anchor, got {n}")
    return source.replace(old,new,1)


def main(root):
    root=Path(root).resolve()
    own=Path(__file__).resolve().parent
    jni=root/"packaging/android/apk/src/org/krita/android/JNIWrappers.java"
    activity=root/"packaging/android/apk/src/org/krita/android/MainActivity.java"
    ko=root/"libs/flake/KoPointerEvent.cpp"
    header=own/"mobile_core/finger_raw_bridge.h"
    for path in (jni,activity,ko,header):
        if not path.is_file():
            raise RuntimeError(f"Missing raw finger bridge input: {path}")

    s=jni.read_text(encoding="utf-8")
    s=replace_once(s,"class JNIWrappers {",
        """class JNIWrappers {
    // Called only for single-finger Android MotionEvents. Does not
    // transmit screen coordinates or alter Java/Qt event dispatch.
    public static native void fingerRawContactSample(
        int pointerId, long eventUptimeMs, float majorPixels,
        float minorPixels, float density, boolean historical);
    // Reset the previous stroke's size-only ring buffer on finger-down,
    // pointer-down (multitouch) and cancelled gestures.
    public static native void fingerRawGestureReset();""",
        "Java native declaration")
    jni.write_text(s,encoding="utf-8")

    s=activity.read_text(encoding="utf-8")
    s=replace_once(s,
        "    private static final String TAG = \"krita.MainActivity\";",
        """    // Pressure proxy diagnostics use Android's raw contact dimensions
    // and historical samples before Qt may coalesce touch updates.
    // Keep the underlying Qt touchscreen behavior exactly unchanged.
    private boolean fingerRawBridgeAvailable = true;
    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (haveLibsLoaded && fingerRawBridgeAvailable && event != null) {
            final int action = event.getActionMasked();
            // Android reuses pointer IDs from one gesture to the next.
            // Never allow a new or interrupted gesture to read contact
            // sizes from a previous finger, or from a multi-finger pan.
            if (action == MotionEvent.ACTION_DOWN ||
                    action == MotionEvent.ACTION_POINTER_DOWN ||
                    action == MotionEvent.ACTION_CANCEL) {
                try {
                    JNIWrappers.fingerRawGestureReset();
                } catch (UnsatisfiedLinkError unavailable) {
                    fingerRawBridgeAvailable = false;
                }
            }
            if (fingerRawBridgeAvailable &&
                    event.getPointerCount() == 1 &&
                    event.getToolType(0) == MotionEvent.TOOL_TYPE_FINGER &&
                    (action == MotionEvent.ACTION_DOWN ||
                     action == MotionEvent.ACTION_MOVE ||
                     action == MotionEvent.ACTION_UP)) {
                final int id = event.getPointerId(0);
                final float density = getResources()
                    .getDisplayMetrics().density;
                try {
                    final int history = event.getHistorySize();
                    // Keep the most recent historical samples. The native
                    // buffer itself is bounded and records no positions.
                    for (int i=Math.max(0, history-8); i<history; ++i) {
                        JNIWrappers.fingerRawContactSample(id,
                            event.getHistoricalEventTime(i),
                            event.getHistoricalTouchMajor(0,i),
                            event.getHistoricalTouchMinor(0,i),
                            density,true);
                    }
                    JNIWrappers.fingerRawContactSample(id,
                        event.getEventTime(), event.getTouchMajor(0),
                        event.getTouchMinor(0), density,false);
                } catch (UnsatisfiedLinkError unavailable) {
                    // Optional native symbol missing: do not break Qt
                    // painting, document dialogs or normal touch input.
                    fingerRawBridgeAvailable = false;
                }
            }
        }
        return super.dispatchTouchEvent(event);
    }

    private static final String TAG = "krita.MainActivity";""",
        "MainActivity raw touch trampoline")
    activity.write_text(s,encoding="utf-8")

    s=ko.read_text(encoding="utf-8")
    s=replace_once(s,
        '#include "finger_contact_pressure.h"',
        '''#include "finger_contact_pressure.h"
#include "finger_raw_bridge.h"
#include <jni.h>''',
        "KoPointerEvent Android JNI includes")
    s += """
#ifdef Q_OS_ANDROID
// JNI export is in Krita's flake input module. Only contact dimensions,
// pointer IDs and event uptime cross this boundary. Qt remains responsible
// for brush/gesture event consumption. Historical samples are retained in
// a bounded in-memory buffer for diagnostics and event-time matching.
extern "C" JNIEXPORT void JNICALL
Java_org_krita_android_JNIWrappers_fingerRawContactSample(
    JNIEnv*,jclass,jint pointerId,jlong uptimeMs,jfloat major,
    jfloat minor,jfloat density,jboolean historical)
{
    MobileFingerRawBridge::record(static_cast<int>(pointerId),
        static_cast<quint64>(uptimeMs),major,minor,density,
        historical==JNI_TRUE);
}
extern "C" JNIEXPORT void JNICALL
Java_org_krita_android_JNIWrappers_fingerRawGestureReset(JNIEnv*,jclass)
{
    MobileFingerRawBridge::reset();
}
#endif
"""
    ko.write_text(s,encoding="utf-8")
    (root/"libs/flake/finger_raw_bridge.h").write_text(
        header.read_text(encoding="utf-8"),encoding="utf-8")
    print("Installed opt-in Android MotionEvent major/minor history bridge")


if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: apply_finger_raw_motion.py <krita-source-root>")
    main(sys.argv[1])
