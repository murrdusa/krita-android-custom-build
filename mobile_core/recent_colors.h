/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QColor>
#include <QSettings>
#include <QStringList>

namespace MobileRecentColors
{
inline QStringList load()
{
    return QSettings().value(QStringLiteral("MobileStudio/strokeColors")).toStringList().mid(0,8);
}

inline QStringList recordStroke(const QColor &color)
{
    if(!color.isValid() || color.alpha()==0)return {};
    auto colors=load();
    const auto code=color.name(QColor::HexArgb);
    if(!colors.isEmpty() && colors.first()==code)return {};
    colors.removeAll(code);
    colors.prepend(code);
    if(colors.size()>8)colors.removeLast();
    QSettings().setValue(QStringLiteral("MobileStudio/strokeColors"),colors);
    return colors;
}
}
