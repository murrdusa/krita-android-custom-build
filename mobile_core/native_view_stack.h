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
#include <QScopedValueRollback>
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
        const quint64 token=++m_nextToken;
        if(addWidget(view)<0)return false;
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
        return true;
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
        auto *self=const_cast<MobileNativeViewStack*>(this);
        self->setCurrentWidget(row->view.data());
        return currentWidget()==row->view.data();
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
                // Qt retains ownership until the native engine's
                // close callback has disposed of the view.
                removeWidget(view.data());
                view->hide();
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
           indexOf(row->view.data())<0 || !row->closeRequest)return false;
        const quint64 token=row->token;
        const CloseRequest close=row->closeRequest;
        const QScopedValueRollback<bool> guarded(m_closing,true);
        // The injected native callback must show Krita's own
        // Save/Discard/Cancel dialog. Cancellation leaves state intact.
        if(!close())return false;
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
