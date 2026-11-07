#!/usr/bin/env python3
"""Rotate Compact Wide Gamut hue rings without changing selected colors."""
from pathlib import Path
import sys

def main(root: Path):
    path = root / "libs/widgets/KisVisualEllipticalSelectorShape.cpp"
    src = path.read_text(encoding="utf-8")
    def one(old, new, label):
        nonlocal src
        count = src.count(old)
        if count != 1:
            raise RuntimeError(f"wheel rotation {label}: expected 1 anchor, found {count}")
        src = src.replace(old, new, 1)
    one("#include <QPainter>\n", "#include <QPainter>\n#include <QSettings>\n", "include")
    one("    line.setAngle(angle);\n", """    // Rotate the hue ring only, never the independent saturation diamond.
    // The inverse conversion below preserves the matching hue under the pen.
    if (isHueControl() &&
        getDimensions() == KisVisualColorSelectorShape::onedimensional &&
        m_type == KisVisualEllipticalSelectorShape::border &&
        QSettings().value(QStringLiteral("TabletUi/compact"),false).toBool()) {
        angle += m_lastHueRotation < 0 ? 0 : m_lastHueRotation;
    }
    line.setAngle(angle);
""", "forward mapping")
    one("        x = angle/360.0;\n", """        if (isHueControl() &&
            getDimensions() == KisVisualColorSelectorShape::onedimensional &&
            m_type == KisVisualEllipticalSelectorShape::border &&
            QSettings().value(QStringLiteral("TabletUi/compact"),false).toBool()) {
            angle = fmod(angle +
                (m_lastHueRotation < 0 ? 0 : m_lastHueRotation)
                + 720.0, 360.0);
        }
        x = angle/360.0;
""", "inverse mapping")
    path.write_text(src, encoding="utf-8")
    # Invalidate the visual ring's own cached gradient when it gets painted.
    # Doing it in the widgets library avoids using non-exported widget
    # symbols from kritaui or from the Wide Gamut plugin.
    one("void KisVisualEllipticalSelectorShape::setBorderWidth(int width)\n",
        """void KisVisualEllipticalSelectorShape::paintEvent(QPaintEvent *event)
{
    if (isHueControl() &&
        getDimensions() == KisVisualColorSelectorShape::onedimensional &&
        m_type == KisVisualEllipticalSelectorShape::border) {
        const int degrees = QSettings().value(
            QStringLiteral("TabletUi/compact"),false).toBool() ?
            QSettings().value(QStringLiteral(
                "TabletUi/colorHueRotationDegrees"),0).toInt() : 0;
        if (m_lastHueRotation != degrees) {
            m_lastHueRotation = degrees;
            forceImageUpdate();
        }
    }
    KisVisualColorSelectorShape::paintEvent(event);
}

void KisVisualEllipticalSelectorShape::setBorderWidth(int width)
""", "cache invalidation")
    # The CPP was already saved above; write the additional paint override.
    path.write_text(src, encoding="utf-8")
    header = root / "libs/widgets/KisVisualEllipticalSelectorShape.h"
    header_src = header.read_text(encoding="utf-8")
    if header_src.count("protected:\n") != 1 or \
       header_src.count("    bool m_gamutMaskNeedsUpdate;\n") != 1:
        raise RuntimeError("wheel rotation header anchors")
    header_src = header_src.replace(
        "protected:\n", "protected:\n    void paintEvent(QPaintEvent *event) override;\n", 1)
    header_src = header_src.replace(
        "    bool m_gamutMaskNeedsUpdate;\n",
        "    bool m_gamutMaskNeedsUpdate;\n    int m_lastHueRotation {-1};\n", 1)
    header.write_text(header_src, encoding="utf-8")
    print("Compact hue ring rotation patched")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: apply_wheel_rotation.py KRITA_SOURCE")
    main(Path(sys.argv[1]))
