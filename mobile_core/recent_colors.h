/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QColor>
#include <QSettings>
#include <QStringList>

namespace MobileRecentColors
{
constexpr int stripCapacity=8;
constexpr int pickerCapacity=3*stripCapacity;

inline QStringList load(int limit=stripCapacity)
{
    return QSettings().value(QStringLiteral("MobileStudio/strokeColors")).toStringList().mid(0,limit);
}

inline QStringList recordStroke(const QColor &color)
{
    if(!color.isValid() || color.alpha()==0)return {};
    auto colors=load(pickerCapacity);
    const auto code=color.name(QColor::HexArgb);
    if(!colors.isEmpty() && colors.first()==code)return {};
    colors.removeAll(code);
    colors.prepend(code);
    if(colors.size()>pickerCapacity)colors.removeLast();
    QSettings().setValue(QStringLiteral("MobileStudio/strokeColors"),colors);
    return colors.mid(0,stripCapacity);
}
}
