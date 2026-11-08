#!/usr/bin/env python3
"""Rotate the native Compact UI color diamond, including its touch mapping."""
from pathlib import Path
import sys

def one(s, old, new, label):
    count=s.count(old)
    if count!=1:
        raise RuntimeError(f"Diamond rotation {label}: expected 1 anchor, got {count}")
    return s.replace(old,new,1)

def main(root):
    source=Path(root)/"libs/widgets/KisVisualDiamondSelectorShape.cpp"
    s=source.read_text(encoding="utf-8")
    s=one(s,"#include <QPainter>\n",
          "#include <QPainter>\n#include <QSettings>\n#include <QtMath>\n",
          "cpp includes")
    s=one(s,
'''    return QPointF(x, y);
}

QPointF KisVisualDiamondSelectorShape::convertWidgetCoordinateToShapeCoordinate''',
'''    return m_squareRotation.map(QPointF(x, y));
}

QPointF KisVisualDiamondSelectorShape::convertWidgetCoordinateToShapeCoordinate''',
          "forward mapping")
    s=one(s,
'''QPointF KisVisualDiamondSelectorShape::convertWidgetCoordinateToShapeCoordinate(QPointF coordinate) const
{
    // margin serves''',
'''QPointF KisVisualDiamondSelectorShape::convertWidgetCoordinateToShapeCoordinate(QPointF coordinate) const
{
    coordinate = m_squareRotationInverse.map(coordinate);
    // margin serves''',
          "inverse mapping")
    s=one(s,
'''    return QRegion(maskPoly);
}

QImage KisVisualDiamondSelectorShape::renderAlphaMask()''',
'''    // Match the actual diamond, not the original octagonal mouse mask.
    // Rotating the old beveled eight-point mask clipped all four visible
    // corners when the diamond was rotated into a square.
    QPolygonF trueDiamond;
    trueDiamond << QPointF(0.5*width(),m_margin)
                << QPointF(m_margin,0.5*height())
                << QPointF(0.5*width(),height()-m_margin)
                << QPointF(width()-m_margin,0.5*height());
    return QRegion(m_squareRotation.map(trueDiamond).toPolygon());
}

QImage KisVisualDiamondSelectorShape::renderAlphaMask()''',
          "unclipped rotated touch mask")
    s=one(s,
'''    painter.setPen(Qt::NoPen);
    QPointF diamond[4] = {''',
'''    painter.setPen(Qt::NoPen);
    painter.setWorldTransform(m_squareRotation, true);
    QPointF diamond[4] = {''',
          "rotated alpha mask")
    s=one(s,
'''void KisVisualDiamondSelectorShape::drawCursor(QPainter &painter)''',
'''void KisVisualDiamondSelectorShape::paintEvent(QPaintEvent *event)
{
    const int degrees=QSettings().value(
        QStringLiteral("TabletUi/compact"),false).toBool() ?
        qBound(0,QSettings().value(
            QStringLiteral("TabletUi/colorDiamondRotationDegrees"),315).toInt(),359) : 0;
    // Keep the corners inscribed in the ring at every rotation angle.
    // QWidget clipping uses the pre-rotation square; the visible corners
    // and touch region are mapped consistently by the shape transform.
    if(m_lastSquareRotation!=degrees || m_lastSquareSize!=size()) {
        m_lastSquareRotation=degrees;
        m_lastSquareSize=size();
        // The diamond's corners are already on the inscribed circle.
        // Rotating the square does NOT increase its corner radius.
        // The previous 1/(cos+sin) shrink left a large empty gray ring.
        QTransform transform;
        transform.translate(width()/2.0,height()/2.0);
        transform.rotate(degrees);
        transform.translate(-width()/2.0,-height()/2.0);
        m_squareRotation=transform;
        m_squareRotationInverse=transform.inverted();
        forceImageUpdate();
    }
    KisVisualColorSelectorShape::paintEvent(event);
}

void KisVisualDiamondSelectorShape::drawCursor(QPainter &painter)''',
          "diamond render update")
    source.write_text(s,encoding="utf-8")
    header=Path(root)/"libs/widgets/KisVisualDiamondSelectorShape.h"
    h=header.read_text(encoding="utf-8")
    h=one(h,'#include "KisVisualColorSelectorShape.h"\n',
          '#include "KisVisualColorSelectorShape.h"\n#include <QTransform>\n',
          "header includes")
    h=one(h,
'''protected:
    QImage renderAlphaMask() const override;''',
'''protected:
    void paintEvent(QPaintEvent *event) override;
    QImage renderAlphaMask() const override;''',
          "paint declaration")
    h=one(h,
'''    int m_margin { 5 };
};''',
'''    int m_margin { 5 };
    int m_lastSquareRotation {-1};
    QSize m_lastSquareSize;
    QTransform m_squareRotation;
    QTransform m_squareRotationInverse;
};''',
          "cached transforms")
    header.write_text(h,encoding="utf-8")
    # In Compact UI, use a slimmer hue ring. The diamond then expands
    # toward the ring automatically via getSpaceForSquare(). Normal Krita
    # retains its original ring geometry and minimum slider target.
    visual=Path(root)/"libs/widgets/KisVisualColorSelector.cpp"
    v=visual.read_text(encoding="utf-8")
    v=one(v,"#include <QVector4D>\n",
          "#include <QVector4D>\n#include <QSettings>\n",
          "selector settings header")
    v=one(v,"    const qreal sliderRatio = 0.09;\n",
          '''    const bool compactSelector=QSettings().value(
        QStringLiteral("TabletUi/compact"),false).toBool();
    const qreal sliderRatio=compactSelector ? 0.055 : 0.09;
''',"compact ring ratio")
    visual.write_text(v,encoding="utf-8")
    print("Compact color diamond no longer shrinks when rotated; slimmer hue ring")

if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: apply_diamond_rotation.py KRITA_SOURCE")
    main(sys.argv[1])
