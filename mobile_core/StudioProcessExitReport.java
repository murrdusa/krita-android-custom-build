// SPDX-License-Identifier: GPL-2.0-or-later
package org.krita.android;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

final class StudioProcessExitReport {
    static String collect(Context context) {
        if (Build.VERSION.SDK_INT < 30)
            return "Android process exit history requires Android 11 or later.\n";

        StringBuilder report = new StringBuilder("Android ")
            .append(Build.VERSION.RELEASE).append(" / ").append(Build.MODEL).append('\n');
        try {
            ActivityManager manager = context.getSystemService(ActivityManager.class);
            if (manager == null) return report.append("Exit history unavailable.\n").toString();
            boolean includeTrace = true;
            for (ApplicationExitInfo exit : manager.getHistoricalProcessExitReasons(null, 0, 6)) {
                int reason = exit.getReason();
                report.append(new java.util.Date(exit.getTimestamp()))
                    .append("  ").append(reasonName(reason)).append(" (").append(reason)
                    .append("), status ").append(exit.getStatus())
                    .append(", RSS ").append(exit.getRss()).append(" KiB\n");
                if (!includeTrace || (reason != ApplicationExitInfo.REASON_CRASH_NATIVE &&
                    reason != ApplicationExitInfo.REASON_ANR)) continue;
                try (InputStream stream = exit.getTraceInputStream()) {
                    if (stream == null) continue;
                    // Android supplies binary tombstones for native crashes and text for ANRs.
                    ByteArrayOutputStream trace = new ByteArrayOutputStream();
                    byte[] buffer = new byte[4096];
                    int limit = 24 * 1024;
                    int count;
                    while (trace.size() < limit &&
                           (count = stream.read(buffer, 0, Math.min(buffer.length, limit - trace.size()))) > 0)
                        trace.write(buffer, 0, count);
                    if (trace.size() == 0) continue;
                    includeTrace = false;
                    if (reason == ApplicationExitInfo.REASON_CRASH_NATIVE) {
                        report.append("Native tombstone (base64, up to 24 KiB)\n")
                            .append(android.util.Base64.encodeToString(trace.toByteArray(), android.util.Base64.NO_WRAP));
                    } else {
                        report.append("ANR trace (up to 24 KiB)\n")
                            .append(new String(trace.toByteArray(), StandardCharsets.UTF_8));
                    }
                    report.append("\n\n");
                } catch (java.io.IOException | RuntimeException ignored) {
                    report.append("Android could not read this trace.\n");
                }
            }
        } catch (RuntimeException ignored) {
            report.append("Android exit history unavailable.\n");
        }
        return report.toString();
    }

    private static String reasonName(int reason) {
        switch (reason) {
            case ApplicationExitInfo.REASON_CRASH: return "Java crash";
            case ApplicationExitInfo.REASON_CRASH_NATIVE: return "Native crash";
            case ApplicationExitInfo.REASON_ANR: return "App not responding";
            case ApplicationExitInfo.REASON_LOW_MEMORY: return "Low memory";
            case ApplicationExitInfo.REASON_SIGNALED: return "Killed by signal";
            case ApplicationExitInfo.REASON_USER_REQUESTED: return "User requested";
            default: return "Process exit";
        }
    }
}
