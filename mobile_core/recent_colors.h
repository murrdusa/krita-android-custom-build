/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QColor>
#include <QSettings>
#include <QStringList>

namespace MobileRecentColors
{
inline QStringList load()
{
    return QSettings().value(QStringLiteral("MobileStudio/recentColors")).toStringList().mid(0,8);
}

inline QStringList remember(const QColor &color)
{
    auto colors=load();
    if(!color.isValid())return colors;
    const auto code=color.name(QColor::HexArgb);
    if(!colors.isEmpty() && colors.first()==code)return colors;
    colors.removeAll(code);
    colors.prepend(code);
    if(colors.size()>8)colors.removeLast();
    QSettings().setValue(QStringLiteral("MobileStudio/recentColors"),colors);
    return colors;
}
}
