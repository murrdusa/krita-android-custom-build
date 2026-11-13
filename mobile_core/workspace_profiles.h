// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QSettings>
#include <QSize>
#include <QUuid>
#include <QVariantMap>

namespace MobileWorkspaceProfiles
{
inline bool portraitDefault(const QSize &size,bool previous)
{
    return size.width()>0 && size.height()*100>size.width()*(previous?112:122);
}

inline int orientation(const QSize &size)
{
    const int delta=size.height()-size.width();
    if(qAbs(delta)<=qMax(16,qMin(size.width(),size.height())/12))return 0;
    return delta>0?1:-1;
}

inline bool matches(const QSize &a,const QSize &b)
{
    return a.isValid() && b.isValid() && orientation(a)==orientation(b) &&
        qAbs(a.width()-b.width())<=qMax(44,b.width()/12) &&
        qAbs(a.height()-b.height())<=qMax(56,b.height()/10);
}

inline int toolbarTarget(int available,int commands)
{
    return qBound(30,(available-2)/qMax(1,commands),44);
}

inline QString group(const QString &id)
{
    return QStringLiteral("MobileStudio/workspaces/")+id+QLatin1Char('/');
}

inline QStringList saved(const QSettings &settings)
{
    return settings.value(QStringLiteral("MobileStudio/workspaceIds")).toStringList();
}

inline QString closest(const QSettings &settings,const QSize &size)
{
    QString best;
    int distance=0;
    for(const auto &id:saved(settings)) {
        const QSize candidate=settings.value(group(id)+QStringLiteral("size")).toSize();
        if(!matches(size,candidate))continue;
        const int difference=qAbs(size.width()-candidate.width())+qAbs(size.height()-candidate.height());
        if(best.isEmpty() || difference<distance) {
            best=id;
            distance=difference;
        }
    }
    return best;
}

inline void remember(QSettings &settings,const QString &id,const QSize &size)
{
    if(id.isEmpty() || !size.isValid())return;
    auto ids=saved(settings);
    if(!ids.contains(id)) {
        ids.append(id);
        settings.setValue(QStringLiteral("MobileStudio/workspaceIds"),ids);
    }
    // Existing profiles also need their latest usable viewport size.
    // Fold-state insets and split-screen dividers can move gradually;
    // retaining the original dimensions creates redundant profiles
    // and leaves the customized layout behind.
    const QString key=group(id)+QStringLiteral("size");
    if(settings.value(key).toSize()!=size)settings.setValue(key,size);
}

inline void replace(QSettings &settings,const QString &target,const QSize &size,const QString &source={})
{
    QVariantMap values;
    if(!source.isEmpty()) {
        settings.beginGroup(group(source));
        for(const auto &key:settings.allKeys())
            if(key!=QStringLiteral("size"))values.insert(key,settings.value(key));
        settings.endGroup();
    }
    settings.remove(group(target).chopped(1));
    auto ids=saved(settings);
    ids.removeAll(target);
    settings.setValue(QStringLiteral("MobileStudio/workspaceIds"),ids);
    if(source.isEmpty())return;
    remember(settings,target,size);
    for(auto it=values.cbegin();it!=values.cend();++it)
        settings.setValue(group(target)+it.key(),it.value());
}
}
