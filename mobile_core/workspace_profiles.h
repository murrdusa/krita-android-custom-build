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

// Save the actual viewport after an in-profile resize, but do not
// overwrite an old profile with a different fold/orientation before
// the delayed workspace switch can complete.
inline QSize sizeForSave(const QSize &current,const QSize &profile,bool portrait)
{
    return current.width()>0 && current.height()>0 &&
           matches(current,profile) &&
           portraitDefault(current,portrait)==portrait?current:profile;
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
// Named presets capture behavior and the current screen's layout only.
inline bool presetPreference(const QString &key)
{
    static const QStringList groups{
        QStringLiteral("MobileStudio/touchGestures/"),
        QStringLiteral("MobileStudio/keyboardShortcuts/"),
        QStringLiteral("MobileStudio/lightingSphere/"),
        QStringLiteral("MobileStudio/fingerPressure")
    };
    for(const auto &prefix:groups)
        if(key.startsWith(prefix))return true;
    static const QStringList keys{
        QStringLiteral("MobileStudio/keepLayersOpen"),
        QStringLiteral("MobileStudio/keepColorOpen"),
        QStringLiteral("MobileStudio/fadePanels"),
        QStringLiteral("MobileStudio/idlePanelOpacity"),
        QStringLiteral("MobileStudio/panelFadeDelay"),
        QStringLiteral("MobileStudio/protectCanvasBack"),
        QStringLiteral("MobileStudio/eyedropperActiveLayer"),
        QStringLiteral("MobileStudio/mirrorAtPen"),
        QStringLiteral("MobileStudio/colorWheelHistory"),
        QStringLiteral("MobileStudio/toolbarColorHistory"),
        QStringLiteral("MobileStudio/toolbarDrag/sample"),
        QStringLiteral("MobileStudio/toolbarVerticalSlider/opacity"),
        QStringLiteral("MobileStudio/toolbarVerticalSlider/size"),
        QStringLiteral("TabletUi/doubleTapMs"),
        QStringLiteral("TabletUi/quickPinch"),
        QStringLiteral("TabletUi/pinchMs"),
        QStringLiteral("TabletUi/referenceFingerDrag"),
        QStringLiteral("TabletUi/referenceTwoFinger"),
        QStringLiteral("TabletUi/referenceDoubleTapEnabled"),
        QStringLiteral("TabletUi/referenceDoubleTapMs"),
        QStringLiteral("TabletUi/volumeKeys"),
        QStringLiteral("TabletUi/volumeDownShort"),
        QStringLiteral("TabletUi/volumeUpShort"),
        QStringLiteral("TabletUi/volumeDownLong"),
        QStringLiteral("TabletUi/volumeUpLong"),
        QStringLiteral("TabletUi/volumeLongMs")
    };
    return keys.contains(key);
}

inline QVariantMap capturePreset(const QSettings &settings,const QString &screen)
{
    QVariantMap preferences,layout;
    const QString prefix=group(screen);
    for(const auto &key:settings.allKeys()) {
        if(presetPreference(key))preferences.insert(key,settings.value(key));
        if(!screen.isEmpty() && key.startsWith(prefix) && key!=prefix+QStringLiteral("size"))
            layout.insert(key.mid(prefix.size()),settings.value(key));
    }
    return {{QStringLiteral("preferences"),preferences},{QStringLiteral("layout"),layout}};
}

// Factory layout is independent of saved user presets and never overwrites them.
inline QVariantMap twoToolbarPreset()
{
    const QVariantMap top{
        {QStringLiteral("name"),QObject::tr("Top toolbar")},
        {QStringLiteral("commands"),QStringList{QStringLiteral("reference-images"),QStringLiteral("layers")}},
        {QStringLiteral("edge"),0},{QStringLiteral("alignment"),2},
        {QStringLiteral("opacity"),0},{QStringLiteral("colorHistory"),false},
        {QStringLiteral("settingsBeforeCommands"),true},
        {QStringLiteral("homeOpacity"),100}
    };
    const QVariantMap left{
        {QStringLiteral("name"),QObject::tr("Drawing tools")},
        {QStringLiteral("commands"),QStringList{
            QStringLiteral("paint"),QStringLiteral("erase"),QStringLiteral("blend"),
            QStringLiteral("tool-settings"),QStringLiteral("color"),
            QStringLiteral("eyedropper"),QStringLiteral("selection"),QStringLiteral("transform"),
            QStringLiteral("liquify"),QStringLiteral("tools")}},
        {QStringLiteral("edge"),2},{QStringLiteral("colorHistory"),false}
    };
    return {
        {QStringLiteral("preferences"),QVariantMap{
            {QStringLiteral("MobileStudio/toolbarColorHistory"),false},
            {QStringLiteral("MobileStudio/colorWheelHistory"),false}}},
        {QStringLiteral("layout"),QVariantMap{
            {QStringLiteral("toolbars"),QVariantList{top,left}},
            {QStringLiteral("layersHorizontal"),false},
            {QStringLiteral("layerCorner"),1}}}
    };
}

inline void applyPreset(QSettings &settings,const QString &screen,const QSize &size,
                        const QVariantMap &preset)
{
    const QVariantMap preferences=preset.value(QStringLiteral("preferences")).toMap();
    const QVariantMap layout=preset.value(QStringLiteral("layout")).toMap();
    const QString prefix=group(screen);
    for(const auto &key:settings.allKeys()) {
        if(presetPreference(key) ||
           (!screen.isEmpty() && key.startsWith(prefix) && key!=prefix+QStringLiteral("size")))
            settings.remove(key);
    }
    for(auto it=preferences.cbegin();it!=preferences.cend();++it)
        if(presetPreference(it.key()))settings.setValue(it.key(),it.value());
    if(screen.isEmpty())return;
    if(!layout.isEmpty())remember(settings,screen,size);
    for(auto it=layout.cbegin();it!=layout.cend();++it)
        if(it.key()!=QStringLiteral("size"))settings.setValue(prefix+it.key(),it.value());
}
}
