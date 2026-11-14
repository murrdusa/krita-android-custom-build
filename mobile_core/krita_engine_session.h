/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Application-owned Krita engine session for Android.
 *
 * This module is the boundary between the Qt application shell and
 * Krita's real document/view manager, without KisMainWindow or MDI.
 * It does not make its own desktop UI, brush renderer or KRA serializer.
 * The same session can be hosted by V2's embedded canvas or by V3's
 * single top-level MobileLightweightCanvasHost.
 */
#pragma once
#include <QtGlobal>
#ifdef Q_OS_ANDROID
#include <QObject>
#include <QPointer>
#include "host_services.h"
#include "krita_standalone_runtime.h"
#include "krita_standalone_documents.h"

class MobileKritaEngineSession final : public QObject
{
public:
    static MobileKritaEngineSession *attach(
        MobileLightweightCanvasHost *host)
    {
        if(!host || host->centralWidget() || host->nativeViewPort())
            return nullptr;
        // The canvas is newly allocated and belongs to this host, never
        // transferred from a Krita desktop MDI window.
        if(!host->createNativeDocumentRoot())
            return nullptr;

        auto *runtime=MobileKritaStandaloneRuntime::create(host);
        if(!runtime) {
            host->discardEmptyNativeDocumentRoot();
            return nullptr;
        }

        const MobileHostServices ports=
            MobileKritaStandaloneDocumentsBootstrap::create(runtime);
        if(!ports.ready()) {
            // Partial bootstrap owns a manager, action collection and
            // empty canvas root. Release them now rather than leaking
            // them for the lifetime of the failed Android window.
            delete runtime;
            host->discardEmptyNativeDocumentRoot();
            return nullptr;
        }

        // The session belongs to the host; runtime and its native views
        // are also host-owned, with the host enforcing teardown order.
        return new MobileKritaEngineSession(host,runtime,ports);
    }

    MobileHostServices services() const {return m_services;}

    KisViewManager *viewManager() const
    {
        return m_runtime ? m_runtime->viewManager() : nullptr;
    }

private:
    MobileKritaEngineSession(
        MobileLightweightCanvasHost *host,
        MobileKritaStandaloneRuntime *runtime,
        const MobileHostServices &services)
        : QObject(host),m_runtime(runtime),m_services(services)
    {
        setObjectName(QStringLiteral("_mobileKritaEngineSession"));
    }

    QPointer<MobileKritaStandaloneRuntime> m_runtime;
    MobileHostServices m_services;
};
#endif
