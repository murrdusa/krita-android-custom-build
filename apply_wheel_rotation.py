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
        angle += QSettings().value(
            QStringLiteral("TabletUi/colorHueRotationDegrees"),0).toDouble();
    }
    line.setAngle(angle);
""", "forward mapping")
    one("        x = angle/360.0;\n", """        if (isHueControl() &&
            getDimensions() == KisVisualColorSelectorShape::onedimensional &&
            m_type == KisVisualEllipticalSelectorShape::border &&
            QSettings().value(QStringLiteral("TabletUi/compact"),false).toBool()) {
            angle = fmod(angle +
                QSettings().value(QStringLiteral(
                    "TabletUi/colorHueRotationDegrees"),0).toDouble()
                + 720.0, 360.0);
        }
        x = angle/360.0;
""", "inverse mapping")
    path.write_text(src, encoding="utf-8")
    # The Wide Gamut plugin already links against Krita's visual-selector
    # widget library. Keep its refresh here, not in kritaui, to avoid an
    # undefined symbol at the main UI library link stage.
    wg_path = root / "plugins/dockers/widegamutcolorselector/WGColorSelectorDock.cpp"
    wg = wg_path.read_text(encoding="utf-8")
    inc = '#include "KisVisualColorSelector.h"\n'
    if wg.count(inc) != 1:
        raise RuntimeError("wide gamut color selector include anchor")
    wg = wg.replace(inc, inc + '#include "KisVisualColorSelectorShape.h"\n', 1)
    anchor = "    m_selector->setConfiguration(&selectorCfg);\n"
    if wg.count(anchor) != 1:
        raise RuntimeError("wide gamut selector refresh anchor")
    wg = wg.replace(anchor, anchor + """    // Compact hue-ring angular offsets do not change the selector type.
    // Explicitly invalidate the cached gradients on a rotation setting change.
    for(KisVisualColorSelectorShape *shape:
        m_selector->findChildren<KisVisualColorSelectorShape*>()) {
        shape->forceImageUpdate();
        shape->update();
    }
""", 1)
    wg_path.write_text(wg, encoding="utf-8")
    print("Compact hue ring rotation patched")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: apply_wheel_rotation.py KRITA_SOURCE")
    main(Path(sys.argv[1]))
