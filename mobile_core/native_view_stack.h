/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * MDI-independent Qt container for real native painting view widgets.
 * This is NOT a substitute for Krita's KisView/painting engine.
 * Native document closure and resource cleanup are delegated to callers.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QPointer>
#include <QStackedWidget>
#include <QVector>
#include <functional>
#include <utility>
#include "view_host_port.h"

class MobileNativeViewStack final : public QStackedWidget,
                                    public MobileViewHostPort
{
public:
    using CloseRequest = std::function<bool()>;
    explicit MobileNativeViewStack(QWidget *parent=nullptr)
        : QStackedWidget(parent) {}

    ~MobileNativeViewStack() override
    {
        // QWidget deletes child views *after* derived members are gone.
        // Disconnect destruction observers before the Row vector dies,
        // otherwise their destroyed() callbacks would read freed memory.
        for(const Row &row:m_rows) {
            QObject::disconnect(row.viewDestroyed);
            QObject::disconnect(row.documentDestroyed);
        }
        m_rows.clear();
    }

    // Adopt a newly constructed unparented view once. Never transfer an
    // already-active view out of an MDI or another host.
    bool adoptView(QWidget *view,QObject *document,const QString &title,
                   CloseRequest closeRequest)
    {
        if(!view || !document || !closeRequest || view->parentWidget() ||
           view->window()!=view || indexOf(view)>=0)return false;
        // Multiple canvases are separate native documents; registering
        // two views for the same document would create competing close
        // callbacks and ambiguous save/discard ownership.
        for(const Row &row:m_rows)
            if(row.document==document && row.view)return false;
        const quint64 token=++m_nextToken;
        Row row;
        row.token=token;
        row.view=view;
        row.document=document;
        row.title=title;
        row.closeRequest=std::move(closeRequest);
        row.viewDestroyed=QObject::connect(
            view,&QObject::destroyed,this,
            [this,token]() {forgetToken(token);});
        row.documentDestroyed=QObject::connect(
            document,&QObject::destroyed,this,
            [this,token]() {forgetToken(token);});
        m_rows.append(std::move(row));
        // addWidget() can synchronously emit currentChanged for the
        // first tab. Its observers need the registered document then.
        const QPointer<MobileNativeViewStack> host(this);
        const QPointer<QWidget> guardedView(view);
        const QPointer<QObject> guardedDocument(document);
        const int inserted=addWidget(view);
        // currentChanged handlers may synchronously destroy a newly
        // activated view or its document. Never report a stale canvas
        // as successfully registered after those callbacks.
        if(!host)return false;
        const Row *registered=guardedView?
            uniqueRow(QPointer<QObject>(guardedView.data())):nullptr;
        if(inserted>=0 && guardedView && guardedDocument &&
           indexOf(guardedView.data())>=0 && registered &&
           registered->token==token &&
           registered->document==guardedDocument)
            return true;
        forgetToken(token);
        return false;
    }

    bool updateViewState(const QPointer<QObject> &identity,
                         const QString &title,bool modified)
    {
        Row *row=uniqueRow(identity);
        if(!row || !row->document)return false;
        row->title=title;
        row->modified=modified;
        return true;
    }

    QVector<ViewSnapshot> openViews() const override
    {
        QVector<ViewSnapshot> result;
        for(const Row &row:m_rows) {
            if(!row.view || !row.document)continue;
            const int index=indexOf(row.view.data());
            if(index<0)continue;
            result.append({row.title,row.modified,
                           row.view.data()==currentWidget(),index,
                           QPointer<QObject>(row.view.data()),row.document});
        }
        return result;
    }

    QPointer<QObject> currentDocument() const override
    {
        for(const Row &row:m_rows) {
            if(row.view.data()==currentWidget() && row.document)
                return row.document;
        }
        return {};
    }

    bool activateView(const QPointer<QObject> &identity) const override
    {
        const Row *row=uniqueRow(identity);
        if(!row || !row->view || !row->document ||
           indexOf(row->view.data())<0)return false;
        const auto target=row->view;
        const auto document=row->document;
        const quint64 token=row->token;
        auto *self=const_cast<MobileNativeViewStack*>(this);
        const QPointer<MobileNativeViewStack> host(self);
        self->setCurrentWidget(target.data());
        // Selection signals may synchronously remove and re-register
        // the same QWidget under a different document. Validate the
        // original registration, not just a still-live widget pointer.
        if(!host || !target || !document ||
           host->currentWidget()!=target.data())return false;
        const Row *selected=host->uniqueRow(QPointer<QObject>(target.data()));
        return selected && selected->token==token &&
               selected->document==document;
    }

    bool closeView(const QPointer<QObject> &identity) const override
    {
        return const_cast<MobileNativeViewStack*>(this)->closeImpl(identity);
    }

    // Called when a native engine view is destroyed by another route.
    // This never destroys or reparents the remaining painting views.
    bool forgetView(const QPointer<QObject> &identity)
    {
        Row *row=uniqueRow(identity);
        return row && forgetToken(row->token);
    }

private:
    struct Row {
        quint64 token=0;
        QPointer<QWidget> view;
        QPointer<QObject> document;
        QString title;
        bool modified=false;
        CloseRequest closeRequest;
        QMetaObject::Connection viewDestroyed;
        QMetaObject::Connection documentDestroyed;
    };

    Row *uniqueRow(const QPointer<QObject> &identity)
    {
        if(!identity)return nullptr;
        Row *found=nullptr;
        for(Row &row:m_rows) {
            if(row.view.data()!=identity.data())continue;
            if(found)return nullptr;
            found=&row;
        }
        return found;
    }
    const Row *uniqueRow(const QPointer<QObject> &identity) const
    {
        if(!identity)return nullptr;
        const Row *found=nullptr;
        for(const Row &row:m_rows) {
            if(row.view.data()!=identity.data())continue;
            if(found)return nullptr;
            found=&row;
        }
        return found;
    }

    bool forgetToken(quint64 token)
    {
        for(int i=0;i<m_rows.size();++i) {
            if(m_rows[i].token!=token)continue;
            const Row removed=m_rows.at(i);
            QPointer<QWidget> view=removed.view;
            m_rows.removeAt(i);
            QObject::disconnect(removed.viewDestroyed);
            QObject::disconnect(removed.documentDestroyed);
            if(view && indexOf(view.data())>=0) {
                // Removing the selected widget emits currentChanged.
                // Its callback may synchronously destroy the whole
                // stack; never touch the removed view through a dead host.
                const QPointer<MobileNativeViewStack> host(this);
                removeWidget(view.data());
                if(!host)return true;
                if(view)view->hide();
            }
            return true;
        }
        return false;
    }

    bool closeImpl(const QPointer<QObject> &identity)
    {
        if(m_closing)return false;
        Row *row=uniqueRow(identity);
        if(!row || !row->view || !row->document ||
           indexOf(row->view.data())<0 || !row->closeRequest ||
           row->view.data()!=currentWidget())return false;
        // Close callbacks may prompt for Save/Discard. A queued tap
        // targeting an old tab must not close that background painting.
        const quint64 token=row->token;
        const CloseRequest close=row->closeRequest;
        // The native callback can delete the active view, its document,
        // or the entire containing host while a prompt is open. Do not
        // leave an RAII rollback referencing memory after host teardown.
        const QPointer<MobileNativeViewStack> host(this);
        m_closing=true;
        const bool accepted=close();
        if(!host)return accepted;
        m_closing=false;
        if(!accepted)return false;
        // Native callback may already have destroyed the widget.
        forgetToken(token);
        return true;
    }

    QVector<Row> m_rows;
    quint64 m_nextToken=0;
    bool m_closing=false;
};

// A shared backend port may outlive the QMainWindow's central widget,
// especially when Qt menus invoke deferred document commands. Never
// hand out a no-op shared_ptr directly to the engine-owned QWidget.
class MobileNativeViewPortAdapter final : public MobileViewHostPort
{
public:
    explicit MobileNativeViewPortAdapter(MobileNativeViewStack *stack)
        : m_stack(stack) {}
    QVector<ViewSnapshot> openViews() const override
    {
        return m_stack?m_stack->openViews():QVector<ViewSnapshot>{};
    }
    QPointer<QObject> currentDocument() const override
    {
        return m_stack?m_stack->currentDocument():QPointer<QObject>{};
    }
    bool activateView(const QPointer<QObject> &view) const override
    {
        return m_stack && m_stack->activateView(view);
    }
    bool closeView(const QPointer<QObject> &view) const override
    {
        return m_stack && m_stack->closeView(view);
    }
private:
    QPointer<MobileNativeViewStack> m_stack;
};
#endif
