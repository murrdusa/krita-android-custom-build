/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Runtime inventory of legacy desktop widgets retained during
 * independent Android startup. The audit intentionally uses only
 * Qt and QObject meta data, not KisMainWindow, QMdiArea documents,
 * painting canvas ownership or any Krita-specific event hooks.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QDockWidget>
#include <QMainWindow>
#include <QMdiArea>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QString>
#include <QObject>

class MobileNativeBootstrapAudit final
{
public:
    struct Snapshot {
        bool nativeSelected=false;
        bool engineOnlyRoot=false;
        bool nativeHostActive=false;
        int mdiAreas=0;
        int desktopDockers=0;
        int welcomeScrollAreas=0;
        int commandPalettes=0;
        int detachedCanvasWindows=0;
        bool desktopStatusVisible=false;

        QString display() const
        {
            return QStringLiteral(
                "Native %1, engine-only %2, host %3; "
                "MDI %4, dockers %5, welcome scroll %6, "
                "command palette %7, detached canvas %8; "
                "desktop status %9")
                .arg(nativeSelected?QStringLiteral("yes"):QStringLiteral("no"))
                .arg(engineOnlyRoot?QStringLiteral("yes"):QStringLiteral("no"))
                .arg(nativeHostActive?QStringLiteral("active"):
                                      QStringLiteral("inactive"))
                .arg(mdiAreas).arg(desktopDockers).arg(welcomeScrollAreas)
                .arg(commandPalettes).arg(detachedCanvasWindows)
                .arg(desktopStatusVisible?QStringLiteral("shown"):
                                          QStringLiteral("hidden"));
        }
    };

    static Snapshot inspect(QMainWindow *desktop)
    {
        Snapshot result;
        if(!desktop)return result;
        result.engineOnlyRoot=desktop->property(
            "_mobileNativeEngineOnlyRoot").toBool();
        result.nativeSelected=result.engineOnlyRoot || desktop->property(
            "_mobileNativeDesktopDeferred").toBool();
        result.nativeHostActive=desktop->property(
            "_mobileNativeHostActive").toBool();
        result.desktopDockers=desktop->findChildren<QDockWidget*>(
            QString(),Qt::FindDirectChildrenOnly).size();
        result.mdiAreas=desktop->findChildren<QMdiArea*>().size();
        // The original welcome page's scroller belongs to the
        // desktop central widget, not to the native child host.
        // Do not count scroll areas inside Brush/Color overlays.
        QWidget *desktopCenter=desktop->centralWidget();
        result.welcomeScrollAreas=desktopCenter ?
            desktopCenter->findChildren<QScrollArea*>().size() : 0;
        // A desktop command palette is constructed by Krita from Kate;
        // no dependency on KateCommandBar's header is needed here.
        for(QObject *obj:desktop->findChildren<QObject*>()) {
            if(!obj)continue;
            const QString kind=QString::fromLatin1(
                obj->metaObject()->className());
            if(kind==QStringLiteral("KateCommandBar"))
                ++result.commandPalettes;
            else if(kind==QStringLiteral("KisCanvasWindow"))
                ++result.detachedCanvasWindows;
        }
        if(auto *status=desktop->findChild<QStatusBar*>(
               QString(),Qt::FindDirectChildrenOnly))
            result.desktopStatusVisible=status->isVisible();
        return result;
    }

    static void record(QMainWindow *desktop)
    {
        if(!desktop)return;
        const Snapshot data=inspect(desktop);
        QSettings settings;
        settings.setValue(
            QStringLiteral("MobileStudio/nativeDesktopWidgetAudit"),
            data.display());
        settings.sync();
    }
};
#endif
