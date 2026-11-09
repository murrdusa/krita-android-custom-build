/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Qt/Krita-independent mobile application host wiring. The painting
 * engine remains Krita; only the application host providing document,
 * view and command services is replaceable. This header is safe to
 * include from a future lightweight Qt Android host or fake-host test.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)

#include <QPointer>
#include <QSharedPointer>
#include <QWidget>
#include "document_host_port.h"
#include "view_host_port.h"
#include "command_registry_port.h"

struct MobileHostServices final
{
    QSharedPointer<MobileDocumentHostPort> documents;
    QSharedPointer<MobileViewHostPort> views;
    QSharedPointer<MobileCommandRegistryPort> commands;
    QPointer<QWidget> documentRoot;

    bool ready() const
    {
        return documents && documents->available() &&
               views && commands && documentRoot;
    }
};
#endif
