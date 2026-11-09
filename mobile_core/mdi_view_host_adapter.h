/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The existing Krita desktop MDI compatibility adapter. Only this file
 * interprets QMdiArea/QMdiSubWindow identities on behalf of Mobile
 * Studio. A future lightweight host replaces it without changing any
 * mobile document workflow, brush or layer UI.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QFileInfo>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPointer>
#include "KisDocument.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "mdi_view_identity.h"
#include "view_host_port.h"

class MobileMdiViewHostAdapter final : public MobileViewHostPort
{
public:
    MobileMdiViewHostAdapter(QMdiArea *area,KisViewManager *manager)
        : m_area(area),m_manager(manager) {}

    QVector<ViewSnapshot> openViews() const override
    {
        QVector<ViewSnapshot> result;
        if(!m_area)return result;
        const auto windows=m_area->subWindowList(QMdiArea::CreationOrder);
        result.reserve(windows.size());
        const KisDocument *current=m_manager?m_manager->document():nullptr;
        for(int i=0;i<windows.size();++i) {
            QMdiSubWindow *sub=windows.at(i);
            auto *view=sub?qobject_cast<KisView*>(sub->widget()):nullptr;
            KisDocument *document=view?view->document():nullptr;
            if(!document)continue;
            const QString file=document->path();
            QString title=file.isEmpty()?QStringLiteral("Untitled"):
                QFileInfo(file).fileName();
            if(title.isEmpty())title=QStringLiteral("Untitled");
            result.append({title,document->isModified(),false,i,
                MobileMdiViewIdentity::capture(sub),
                QPointer<QObject>(document)});
        }

        // Native MDI selection is authoritative only when it agrees
        // with Krita's active engine document. If MDI focus is lost
        // during canvas presentation, accept a unique current-document
        // view. Never guess between two views of the same document.
        int selected=-1;
        QMdiSubWindow *active=m_area->activeSubWindow();
        auto *activeView=active?qobject_cast<KisView*>(active->widget()):nullptr;
        if(activeView && (!current || activeView->document()==current)) {
            for(int i=0;i<result.size();++i) {
                if(result[i].nativeView==active) {
                    selected=i;
                    break;
                }
            }
        }
        if(selected<0 && current) {
            int candidate=-1;
            bool ambiguous=false;
            for(int i=0;i<result.size();++i) {
                if(result[i].document!=current)continue;
                if(candidate>=0) {
                    ambiguous=true;
                    break;
                }
                candidate=i;
            }
            if(!ambiguous)selected=candidate;
        }
        if(selected<0 && result.size()==1)selected=0;
        if(selected>=0)result[selected].active=true;
        return result;
    }

    bool activateView(const QPointer<QObject> &identity) const override
    {
        if(!m_area || !identity)return false;
        QPointer<QMdiSubWindow> target=
            MobileMdiViewIdentity::resolve(m_area,identity);
        if(!target || !qobject_cast<KisView*>(target->widget()))return false;
        m_area->setActiveSubWindow(target);
        return target && MobileMdiViewIdentity::resolve(m_area,identity);
    }

    bool closeView(const QPointer<QObject> &identity) const override
    {
        if(!m_area || !identity)return false;
        QPointer<QMdiSubWindow> target=
            MobileMdiViewIdentity::resolve(m_area,identity);
        if(!target || !qobject_cast<KisView*>(target->widget()))return false;
        // Krita's last-view closeEvent owns unsaved document prompts.
        m_area->setActiveSubWindow(target);
        if(!target || !MobileMdiViewIdentity::resolve(m_area,identity))
            return false; // A selection signal may have removed it.
        return target->close();
    }

private:
    QPointer<QMdiArea> m_area;
    KisViewManager *m_manager=nullptr; // Native Krita ownership.
};
#endif
