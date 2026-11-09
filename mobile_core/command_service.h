/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Krita native action, brush preset and tool selection service.
 * This is the only Mobile Studio module which reads the legacy desktop
 * command registry/menubar; mobile widgets see an engine service facade.
 */
#pragma once
#ifdef Q_OS_ANDROID

#include <QAction>
#include <QList>
#include <QMenu>
#include <QPointer>
#include <QSharedPointer>
#include <QString>
#include <QVector>
#include <QWidget>
#include <algorithm>
#include <brushengine/kis_paintop_preset.h>
#include <KoToolManager.h>

#include "KisViewManager.h"
#include "command_registry_port.h"
#include "kis_paintop_box.h"

class MobileCommandService final
{
public:
    MobileCommandService(QSharedPointer<MobileCommandRegistryPort> registry,
                         KisViewManager *manager)
        : m_registry(std::move(registry)),m_manager(manager) {}

    // Only this adapter knows the temporary desktop backend. Client
    // workspaces see Krita services, not KisMainWindow or its toolbars.
    QAction *action(const QString &id) const {
        return m_registry?m_registry->action(id):nullptr;
    }
    QAction *action(const char *id) const {
        return action(QString::fromLatin1(id));
    }
    QList<QAction*> availableActions() const {
        return m_registry?m_registry->availableActions():QList<QAction*>{};
    }
    // Native menu actions are accessed only through the adapter. The
    // mobile menu browser can preserve Krita's real File/Edit/View/etc.
    // hierarchy without referencing the hidden KisMainWindow itself.
    QList<QAction*> menuRootActions() const {
        return m_registry?m_registry->menuRootActions():QList<QAction*>{};
    }
    bool canRun(const char *id) const {
        const QAction *a=action(id);
        return a && a->isEnabled();
    }
    bool selectBrushPreset(const KisPaintOpPresetSP &preset) const {
        if(!preset || !m_manager || !m_manager->paintOpBox())return false;
        m_manager->paintOpBox()->resourceSelected(preset);
        return true;
    }
    void setEraser(bool enabled) const {
        if(QAction *eraser=action("erase_action"))
            if(eraser->isCheckable() && eraser->isChecked()!=enabled &&
               eraser->isEnabled())
                eraser->trigger();
    }
    bool run(const char *id) const {
        if(QAction *a=action(id)) {
            if(a->isEnabled()){a->trigger();return true;}
        }
        return false;
    }

    struct MobileTool {
        QString id;
        QString label;
        QString description;
        QString section;
        QString iconName;
        int priority=0;
    };
    QVector<MobileTool> tools() const
    {
        QVector<MobileTool> result;
        if(!hasDocument())return result;
        KoToolManager *manager=KoToolManager::instance();
        if(!manager)return result;
        for(KoToolAction *action:manager->toolActionList()) {
            if(!action || action->id().isEmpty())continue;
            const QString label=action->iconText().trimmed().isEmpty()?
                action->toolTip():action->iconText();
            result.append({action->id(),label,action->toolTip(),
                           action->section(),action->iconName(),
                           action->priority()});
        }
        std::stable_sort(result.begin(),result.end(),
            [](const MobileTool &a,const MobileTool &b) {
                if(a.section!=b.section)
                    return a.section.localeAwareCompare(b.section)<0;
                if(a.priority!=b.priority)return a.priority<b.priority;
                return a.label.localeAwareCompare(b.label)<0;
            });
        return result;
    }
    QString activeTool() const
    {
        KoToolManager *manager=KoToolManager::instance();
        return manager?manager->activeToolId():QString();
    }
    bool selectTool(const QString &id) const
    {
        if(!hasDocument() || id.isEmpty())return false;
        KoToolManager *manager=KoToolManager::instance();
        if(!manager)return false;
        const auto actions=manager->toolActionList();
        for(KoToolAction *action:actions) {
            if(action && action->id()==id) {
                action->trigger();
                return true;
            }
        }
        return false;
    }
    void freehandBrush() const {
        selectTool(QStringLiteral("KritaShape/KisToolBrush"));
    }
    QMenu *nativeMenus(QWidget *parent) const {
        auto *menu=new QMenu(parent);
        if(!m_registry)return menu;
        for(QAction *top:m_registry->menuRootActions())
            if(top && top->menu())menu->addMenu(top->menu());
        return menu;
    }

private:
    bool hasDocument() const {
        return m_manager && m_manager->document();
    }
    QSharedPointer<MobileCommandRegistryPort> m_registry;
    KisViewManager *m_manager=nullptr; // Owned by native Krita backend
};
#endif // Q_OS_ANDROID
