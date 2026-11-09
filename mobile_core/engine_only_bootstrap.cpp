/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The native painting host is the sole top-level Android window.
 * Krita's resource/plugin engine is initialized before this entrypoint.
 */
#include "engine_only_bootstrap.h"
#ifdef Q_OS_ANDROID
#include <QDebug>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include "lightweight_canvas_host.h"
#include "engine_only_startup_policy.h"
#include "krita_standalone_pilot.h"

bool MobileEngineOnlyBootstrap::launch()
{
    auto *root=new MobileLightweightCanvasHost;
    root->setObjectName(QStringLiteral("_mobileEngineOnlyDirectRoot"));
    root->setProperty("_mobileNativeEngineOnlyRoot",true);
    root->setAttribute(Qt::WA_DeleteOnClose,true);
    root->setWindowTitle(QObject::tr("Krita Mobile Studio"));
    root->show();
    if(!root->isVisible()) {
        qCritical() << "Mobile Studio: drawing window could not show";
        root->deleteLater();
        return false;
    }
    QPointer<MobileLightweightCanvasHost> alive=root;
    QTimer::singleShot(0,root,[alive]() {
        if(!alive)return;
        const bool started=MobileKritaStandalonePilot::launch(
            alive.data(),[](bool success) {
                QSettings settings;
                MobileEngineOnlyStartupPolicy::complete(settings,success);
                if(!success)
                    qCritical() << "Mobile Studio: native canvas initialization failed";
            });
        if(!started) {
            QSettings settings;
            MobileEngineOnlyStartupPolicy::complete(settings,false);
            qCritical() << "Mobile Studio: engine session did not start";
            if(alive)alive->close();
        }
    });
    qInfo() << "Mobile Studio: direct Krita engine window started";
    return true;
}
#endif
