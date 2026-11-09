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
#include "engine_only_startup_policy.h"
#include "krita_standalone_pilot.h"

bool MobileEngineOnlyBootstrap::launch()
{
    auto *root=new MobileEngineOnlyRoot;
    root->setAttribute(Qt::WA_DeleteOnClose,true);
    root->setWindowTitle(QObject::tr("Krita Mobile Engine (experimental)"));
    root->setCentralWidget(new QWidget(root));
    root->show();
    if(!root->isVisible()) {
        delete root;
        return false;
    }
    QPointer<MobileEngineOnlyRoot> guardedRoot=root;
    QTimer::singleShot(0,root,[guardedRoot]() {
        if(!guardedRoot)return;
        const bool started=MobileKritaStandalonePilot::launch(
            guardedRoot.data(),[guardedRoot](bool success) {
                QSettings settings;
                MobileEngineOnlyStartupPolicy::complete(settings,success);
                if(!success)
                    qWarning() << "Experimental engine-only boot failed;"
                               << "next launch restores original Krita";
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
