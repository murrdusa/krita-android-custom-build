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
        // Android's system palette can paint popup menu labels white
        // against a white background. Explicitly style both regular
        // items and submenus; no hover should be needed to read text.
        const QString popupStyle=QStringLiteral(
            "QMenu{background-color:#283746;color:#f3f7fc;"
            "border:1px solid #6f90a8;padding:3px;}"
            "QMenu::item{background:transparent;color:#f3f7fc;"
            "padding:11px 17px;min-height:37px;}"
            "QMenu::item:selected{background:#356c8e;color:#ffffff;}"
            "QMenu::item:disabled{color:#a6b3c1;}"
            "QMenu::separator{height:1px;background:#526a80;"
            "margin:5px 8px;}");
        menu->setStyleSheet(popupStyle);
        for(QAction *action:moreActions) {
            if(!action)continue;
            if(QMenu *submenu=action->menu())
                submenu->setStyleSheet(popupStyle);
            menu->addAction(action);
        }
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
