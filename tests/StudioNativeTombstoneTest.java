// SPDX-License-Identifier: GPL-2.0-or-later
package org.krita.android;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;

final class StudioNativeTombstoneTest {
    public static void main(String[] args) throws Exception {
        ByteArrayOutputStream trace = new ByteArrayOutputStream();
        number(trace, 6, 123);
        ByteArrayOutputStream signal = new ByteArrayOutputStream();
        bytes(signal, 2, "SIGSEGV".getBytes(StandardCharsets.UTF_8));
        bytes(trace, 10, signal.toByteArray());
        bytes(trace, 16, new byte[30 * 1024]); // The old 24 KiB limit cut off the crash stack.
        String truncated = StudioNativeTombstone.read(new ByteArrayInputStream(trace.toByteArray()));
        if (!truncated.contains("thread 123") || !truncated.contains("thread unavailable"))
            throw new AssertionError(truncated);

        ByteArrayOutputStream frame = new ByteArrayOutputStream();
        number(frame, 1, 0xabcd);
        bytes(frame, 4, "KisCanvas2::updateCanvas".getBytes(StandardCharsets.UTF_8));
        bytes(frame, 6, "/private/install/libkritaui.so".getBytes(StandardCharsets.UTF_8));
        bytes(frame, 8, "example-build-id".getBytes(StandardCharsets.UTF_8));
        ByteArrayOutputStream thread = new ByteArrayOutputStream();
        bytes(thread, 4, frame.toByteArray());
        ByteArrayOutputStream entry = new ByteArrayOutputStream();
        number(entry, 1, 123);
        bytes(entry, 2, thread.toByteArray());
        bytes(trace, 16, entry.toByteArray());
        String report = StudioNativeTombstone.read(new ByteArrayInputStream(trace.toByteArray()));
        if (!report.contains("SIGSEGV") || !report.contains("0xabcd libkritaui.so") ||
            !report.contains("KisCanvas2::updateCanvas") || !report.contains("example-build-id") ||
            report.contains("/private/"))
            throw new AssertionError(report);
    }

    private static void number(ByteArrayOutputStream output, int field, int value) {
        varint(output, field << 3);
        varint(output, value);
    }

    private static void bytes(ByteArrayOutputStream output, int field, byte[] value) {
        varint(output, (field << 3) | 2);
        varint(output, value.length);
        output.write(value, 0, value.length);
    }

    private static void varint(ByteArrayOutputStream output, int value) {
        while (value >= 128) {
            output.write((value & 127) | 128);
            value >>>= 7;
        }
        output.write(value);
    }
}
