// SPDX-License-Identifier: GPL-2.0-or-later
package org.krita.android;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/** Reads only the signal and crashing stack from Android debuggerd's Tombstone protobuf. */
final class StudioNativeTombstone {
    private static final int MAX_BYTES = 4 * 1024 * 1024;
    private static final int MAX_FRAMES = 32;

    static String read(InputStream stream) throws IOException {
        byte[] bytes = new byte[MAX_BYTES];
        int size = 0;
        while (size < bytes.length) {
            int count = stream.read(bytes, size, bytes.length - size);
            if (count <= 0) break;
            size += count;
        }
        if (size == 0) return "";
        int tid = 0;
        String signal = "";
        Cursor tombstone = new Cursor(bytes, 0, size);
        while (tombstone.next()) {
            if (tombstone.field == 6 && tombstone.wire == 0) tid = (int)tombstone.value;
            if (tombstone.field == 10 && tombstone.wire == 2) {
                Cursor info = tombstone.message();
                while (info.next()) {
                    if (info.field == 2 && info.wire == 2) signal = info.text();
                }
            }
            if (tid != 0 && !signal.isEmpty()) break;
        }
        StringBuilder report = new StringBuilder("Native crash stack");
        if (tid != 0) report.append(" (thread ").append(tid).append(')');
        if (!signal.isEmpty()) report.append(" — ").append(signal);
        report.append('\n');
        if (tid != 0) {
            tombstone = new Cursor(bytes, 0, size);
            while (tombstone.next()) {
                if (tombstone.field != 16 || tombstone.wire != 2) continue;
                Cursor entry = tombstone.message();
                long key = 0;
                Cursor thread = null;
                while (entry.next()) {
                    if (entry.field == 1 && entry.wire == 0) key = entry.value;
                    if (entry.field == 2 && entry.wire == 2) thread = entry.message();
                }
                if (key == tid && thread != null && appendFrames(report, thread))
                    return report.toString();
            }
        }
        return report.append("Crashing thread unavailable in the Android trace.\n").toString();
    }

    private static boolean appendFrames(StringBuilder report, Cursor thread) {
        int frames = 0;
        while (frames < MAX_FRAMES && thread.next()) {
            if (thread.field != 4 || thread.wire != 2) continue;
            Cursor frame = thread.message();
            long pc = 0;
            String file = "", symbol = "", build = "";
            while (frame.next()) {
                if (frame.field == 1 && frame.wire == 0) pc = frame.value;
                if (frame.field == 4 && frame.wire == 2) symbol = frame.text();
                if (frame.field == 6 && frame.wire == 2) file = frame.text();
                if (frame.field == 8 && frame.wire == 2) build = frame.text();
            }
            report.append('#').append(frames++).append(" pc 0x")
                .append(Long.toHexString(pc)).append(' ')
                .append(file.substring(file.lastIndexOf('/') + 1));
            if (!symbol.isEmpty()) report.append(" (").append(symbol).append(')');
            if (!build.isEmpty()) report.append(" [BuildId: ").append(build).append(']');
            report.append('\n');
        }
        return frames > 0;
    }

    private static final class Cursor {
        final byte[] bytes;
        final int end;
        int position, field, wire, start;
        long value;

        Cursor(byte[] bytes, int start, int end) {
            this.bytes = bytes;
            this.position = start;
            this.end = end;
        }

        boolean next() {
            value = 0;
            long tag = varint();
            if (tag <= 0 || tag > 0xffffffffL) return false;
            field = (int)(tag >>> 3);
            wire = (int)(tag & 7);
            if (wire == 0) {
                value = varint();
                return position <= end;
            }
            long length = wire == 2 ? varint() : wire == 1 ? 8 : wire == 5 ? 4 : -1;
            if (length < 0 || length > end - position) return false;
            start = position;
            position += (int)length;
            return true;
        }

        long varint() {
            long result = 0;
            for (int shift = 0; shift < 64 && position < end; shift += 7) {
                int next = bytes[position++] & 0xff;
                result |= (long)(next & 0x7f) << shift;
                if ((next & 0x80) == 0) return result;
            }
            position = end + 1;
            return -1;
        }

        Cursor message() { return new Cursor(bytes, start, position); }

        String text() {
            return new String(bytes, start, Math.min(position - start, 512),
                              StandardCharsets.UTF_8).replace('\n', ' ').replace('\r', ' ');
        }
    }
}
