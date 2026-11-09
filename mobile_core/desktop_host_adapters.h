/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The sole legacy KisMainWindow adapter for mobile document transactions
 * and command discovery. Replace these two small implementations to use
 * a lightweight Android Krita host; keep the same engine, KRA files,
 * Krita native actions and the mobile frontend.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include "document_host_port.h"
#include "command_registry_port.h"
#include <QPointer>
#include <QMenuBar>
#include "KisMainWindow.h"

class MobileDesktopDocumentHost final : public MobileDocumentHostPort
{
public:
    explicit MobileDesktopDocumentHost(KisMainWindow *window)
        : m_window(window) {}

    bool available() const override {return !m_window.isNull();}

    bool attachNewDocument(KisDocument *document) const override
    {
        if(!m_window || !document)return false;
        // Krita continues to create its native KisView and manage the
        // document lifecycle. No mobile-owned canvas/view is created.
        m_window->addViewAndNotifyLoadingCompleted(document);
        return true;
    }

    bool openFile(const QString &path) const override
    {
        return m_window && !path.isEmpty() &&
               m_window->openDocument(path,KisMainWindow::None);
    }

    bool save(KisDocument *document,bool saveAs) const override
    {
        return m_window && document &&
               m_window->saveDocument(document,saveAs,false,false);
    }

private:
    QPointer<KisMainWindow> m_window;
};

class MobileDesktopCommandRegistry final : public MobileCommandRegistryPort
{
public:
    explicit MobileDesktopCommandRegistry(KisMainWindow *window)
        : m_window(window) {}

    QAction *action(const QString &id) const override
    {
        return m_window && m_window->actionCollection()?
            m_window->actionCollection()->action(id):nullptr;
    }

    QList<QAction*> availableActions() const override
    {
        return m_window && m_window->actionCollection()?
            m_window->actionCollection()->actions():QList<QAction*>{};
    }

    QList<QAction*> menuRootActions() const override
    {
        return m_window && m_window->menuBar()?
            m_window->menuBar()->actions():QList<QAction*>{};
    }

private:
    QPointer<KisMainWindow> m_window;
};
#endif // Q_OS_ANDROID
