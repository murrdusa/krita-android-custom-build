// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>

namespace MobileMenuStyle
{
inline QString controls()
{
    return QStringLiteral(
        "QLabel,QCheckBox,QRadioButton{color:#f1f2f5;}"
        "QScrollArea,QAbstractItemView{background:#25262b;color:#f1f2f5;border:0;outline:0;}"
        "QAbstractItemView::item{min-height:40px;padding:0 8px;border-radius:6px;}"
        "QAbstractItemView::item:selected{background:#394459;color:#f1f2f5;}"
        "QAbstractItemView::item:disabled{color:#989da9;}"
        "QComboBox,QSpinBox,QDoubleSpinBox,QLineEdit,QPushButton,QToolButton"
        "{background:#30323b;color:#f1f2f5;border:1px solid #44464f;border-radius:7px;"
        "padding:3px 8px;min-height:34px;}"
        "QSpinBox QLineEdit,QDoubleSpinBox QLineEdit{border:0;padding:0;min-height:0;}"
        "QAbstractButton:checked,QAbstractButton:pressed{background:#394459;}"
        "QAbstractButton:disabled{color:#989da9;}"
        "QTabBar::tab{background:#30323b;color:#bfc3ce;padding:0 10px;min-height:40px;}"
        "QTabBar::tab:selected{background:#394459;color:#f1f2f5;}");
}

inline QString panel()
{
    return QStringLiteral(
        "QFrame[_mobileMenuPanel=\"true\"]{background:#25262b;"
        "color:#f1f2f5;border:1px solid #55555c;border-radius:9px;}")+controls();
}

inline QString dialog()
{
    return QStringLiteral("QDialog{background:#25262b;color:#f1f2f5;}")+controls();
}
}
