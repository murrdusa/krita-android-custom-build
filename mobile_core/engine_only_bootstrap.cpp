/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Android-only composition root for Krita's independent Qt UI.
 * No desktop KisMainWindow, QMdiArea or dockers are constructed here.
 */
#include "engine_only_bootstrap.h"
#ifdef Q_OS_ANDROID
#include <QDebug>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QWidget>
#include "engine_only_root.h"
#include "lightweight_canvas_host.h"
#include "engine_only_startup_policy.h"
#include "krita_standalone_pilot.h"

bool MobileEngineOnlyBootstrap::launch()
{
    QSettings startupSettings;
    const bool directHost=
        MobileEngineOnlyStartupPolicy::directHostEnabled(startupSettings);
    // V2 retains the tested Qt wrapper; V3 uses the painting host itself
    // as the Android application root, eliminating the second QMainWindow.
    QWidget *root=directHost ?
        static_cast<QWidget*>(new MobileLightweightCanvasHost) :
        static_cast<QWidget*>(new MobileEngineOnlyRoot);
    root->setAttribute(Qt::WA_DeleteOnClose,true);
    if(directHost) {
        root->setObjectName(QStringLiteral("_mobileEngineOnlyDirectRoot"));
        root->setProperty("_mobileNativeEngineOnlyRoot",true);
        root->setWindowTitle(QObject::tr("Krita Mobile Studio"));
    } else {
        auto *wrapper=static_cast<MobileEngineOnlyRoot*>(root);
        wrapper->setWindowTitle(
            QObject::tr("Krita Mobile Engine (experimental)"));
        wrapper->setCentralWidget(new QWidget(wrapper));
    }
    root->show();
    if(!root->isVisible()) {
        delete root;
        return false;
    }
    QPointer<QWidget> guardedRoot=root;
    QTimer::singleShot(0,root,[guardedRoot]() {
        if(!guardedRoot)return;
        const bool started=MobileKritaStandalonePilot::launch(
            guardedRoot.data(),[guardedRoot](bool success) {
                QSettings settings;
                MobileEngineOnlyStartupPolicy::complete(settings,success);
                if(!success)
                    qWarning() << "Independent engine boot failed;"
                               << "next launch restores verified V2 or desktop";
            });
        if(!started) {
            QSettings settings;
            MobileEngineOnlyStartupPolicy::complete(settings,false);
            if(guardedRoot)guardedRoot->close();
        }
    });
    qInfo() << "Experimental engine-only boot: KisMainWindow skipped";
    return true;
}
#endif
