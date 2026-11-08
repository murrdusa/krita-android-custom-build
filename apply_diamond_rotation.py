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
'''    return QRegion(m_squareRotation.map(QPolygonF(maskPoly)).toPolygon());
}

QImage KisVisualDiamondSelectorShape::renderAlphaMask()''',
          "rotated touch mask")
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
            QStringLiteral("TabletUi/colorDiamondRotationDegrees"),0).toInt(),359) : 0;
    // The diamond's bounding box is unchanged even for arbitrary angles.
    // Fit its rotated four corners inside the existing selector square.
    if(m_lastSquareRotation!=degrees || m_lastSquareSize!=size()) {
        m_lastSquareRotation=degrees;
        m_lastSquareSize=size();
        const qreal angle=qDegreesToRadians(qreal(degrees));
        const qreal fit=1.0/(qAbs(qCos(angle))+qAbs(qSin(angle)));
        QTransform transform;
        transform.translate(width()/2.0,height()/2.0);
        transform.rotate(degrees);
        transform.scale(fit,fit);
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
    print("Compact color diamond rotation applied to rendering, cursor and touch coordinates")

if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: apply_diamond_rotation.py KRITA_SOURCE")
    main(sys.argv[1])
