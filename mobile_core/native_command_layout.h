/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Compact command chrome for the independent Krita engine host.
 * Real QAction objects are shared with the already-working native
 * document, tool and workspace callbacks. Never duplicate commands
 * or move the existing paint canvas.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QAction>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>

class MobileNativeCommandLayout final
{
public:
    static bool install(QMainWindow *host,
                        const QList<QAction*> &documentActions,
                        const QList<QAction*> &workspaceActions,
                        const QList<QAction*> &moreActions)
    {
        if(!host || !host->centralWidget() ||
           host->findChild<QToolBar*>(
               QStringLiteral("_mobileNativeDocumentBar"),
               Qt::FindDirectChildrenOnly))
            return false;

        // Each visible row has only a handful of touch targets.
        // In particular, Brushes/Layers/Color/Tools must not be
        // hidden by QToolBar's tiny right-edge overflow chevron.
        auto *documents=new QToolBar(host);
        documents->setObjectName(
            QStringLiteral("_mobileNativeDocumentBar"));
        documents->setMovable(false);
        documents->setFloatable(false);
        documents->setToolButtonStyle(Qt::ToolButtonTextOnly);

        auto *workspace=new QToolBar(host);
        workspace->setObjectName(
            QStringLiteral("_mobileNativeWorkspaceBar"));
        workspace->setMovable(false);
        workspace->setFloatable(false);
        workspace->setToolButtonStyle(Qt::ToolButtonTextOnly);

        for(QAction *action:documentActions)
            if(action)documents->addAction(action);
        for(QAction *action:workspaceActions)
            if(action)workspace->addAction(action);

        auto *moreButton=new QToolButton(documents);
        moreButton->setObjectName(
            QStringLiteral("_mobileNativeMoreButton"));
        moreButton->setText(QObject::tr("More"));
        moreButton->setPopupMode(QToolButton::InstantPopup);
        moreButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
        auto *menu=new QMenu(moreButton);
        for(QAction *action:moreActions)
            if(action)menu->addAction(action);
        moreButton->setMenu(menu);
        documents->addWidget(moreButton);

        host->addToolBar(Qt::TopToolBarArea,documents);
        host->addToolBar(Qt::BottomToolBarArea,workspace);
        return host->centralWidget() &&
               documents->parentWidget()==host &&
               workspace->parentWidget()==host;
    }
};
#endif
