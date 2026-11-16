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
#include <QTimer>
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
            [this,token,orphan=QPointer<QWidget>(view)]() {
                // An engine-driven document teardown can destroy the
                // document before its QWidget canvas. forgetToken()
                // detaches the widget but does not own its deletion.
                // Otherwise the hidden GL canvas remains a child of the
                // window until the whole application is closed.
                const QPointer<MobileNativeViewStack> host(this);
                const bool removed=forgetToken(token);
                // An explicit close may already have consumed this
                // registration. Never delete a widget that another
                // owner could have adopted in the meantime.
                // Qt's synchronous currentChanged observers may move
                // the widget into another host or register it again.
                // Only retire a detached canvas still owned by us.
                if(removed && host && orphan)
                    retireDetachedView(host,orphan);
            });
        m_rows.append(std::move(row));
        // Notify listeners only after Qt's insertion finishes so they
        // can safely close the document or the stack during activation.
        const QPointer<MobileNativeViewStack> host(this);
        const QPointer<QWidget> guardedView(view);
        const QPointer<QObject> guardedDocument(document);
        const bool first=currentIndex()<0;
        const bool alreadyBlocked=blockSignals(true);
        const int inserted=addWidget(view);
        if(!host)return false;
        blockSignals(alreadyBlocked);
        if(!alreadyBlocked && first && inserted>=0 && currentWidget()==guardedView)
            Q_EMIT currentChanged(currentIndex());
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
        // A currentChanged observer can cancel adoption without
        // destroying the proposed QWidget. removeWidget() detaches it
        // from the stack but retains its QWidget parent, leaving an
        // unregistered GL canvas alive until host shutdown. Schedule
        // the same ownership-checked retirement used by native Close.
        // A caller may still delete, reparent or re-adopt the view
        // before this queued callback; QPointer and indexOf protect it.
        forgetToken(token);
        if(host && guardedView)
            retireDetachedView(host,guardedView);
        return false;
    }

    // A view can be registered while temporarily outside the Qt
    // layout during a reentrant transfer. Callers must not infer
    // ownership from indexOf() when deciding whether to delete it.
    bool hasViewRegistration(const QPointer<QObject> &identity) const
    {
        const Row *row=uniqueRow(identity);
        return row && row->view && row->document;
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
        QWidget *selected=currentWidget();
        if(!selected)return {};
        for(const Row &row:m_rows) {
            if(row.view && row.document && row.view.data()==selected &&
               indexOf(selected)>=0)
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

    // Explicit engine-side deregistration can happen independently of
    // native Close. If the removed view survives, removeWidget() keeps it
    // parented to this stack; retire that unregistered child after Qt
    // signals unwind. A view re-adopted or transferred in the meantime
    // remains owned by its new registration/host.
    bool forgetView(const QPointer<QObject> &identity)
    {
        Row *row=uniqueRow(identity);
        if(!row)return false;
        const quint64 token=row->token;
        const QPointer<QWidget> original=row->view;
        const QPointer<MobileNativeViewStack> host(this);
        const bool removed=forgetToken(token);
        if(removed && host && original)
            retireDetachedView(host,original);
        return removed;
    }

private:
    // Wait until removeWidget()/selection notification has unwound,
    // then verify ownership at the point of destruction. Calling
    // deleteLater() after this verification would leave a second gap:
    // another host could adopt the still-live canvas before Qt processes
    // DeferredDelete, and the old host would destroy its new owner's view.
    // The zero-delay callback is outside the stack's layout transaction,
    // so synchronous deletion is safe after all ownership checks pass.
    static void retireDetachedView(const QPointer<MobileNativeViewStack> &host,
                                   const QPointer<QWidget> &view)
    {
        if(!host || !view)return;
        QTimer::singleShot(0,host.data(),[host,view]() {
            if(!host || !view || view->parentWidget()!=host.data() ||
               host->indexOf(view.data())>=0)return;
            // A new native registration can exist even while Qt has
            // temporarily removed its QWidget from the layout (for
            // example during a reentrant handoff). Its absence from
            // QStackedWidget is not proof that the view is unowned.
            if(host->uniqueRow(QPointer<QObject>(view.data())))return;
            delete view.data();
        });
    }

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
            const int index=view?indexOf(view.data()):-1;
            if(index>=0) {
                const bool wasCurrent=view==currentWidget();
                const QPointer<MobileNativeViewStack> host(this);
                // Qt's layout is still inside takeAt() when it emits
                // these signals. A synchronous observer may delete the
                // stack and its layout, so notify after removal returns.
                const bool alreadyBlocked=blockSignals(true);
                removeWidget(view.data());
                if(!host)return true;
                blockSignals(alreadyBlocked);
                if(!alreadyBlocked) {
                    if(wasCurrent)Q_EMIT currentChanged(currentIndex());
                    if(!host)return true;
                    Q_EMIT widgetRemoved(index);
                    if(!host)return true;
                }
                // currentChanged may synchronously transfer this widget
                // to another host or re-register it here. Do not hide
                // a canvas that the receiving owner has already shown.
                if(view && view->parentWidget()==host.data() &&
                   host->indexOf(view.data())<0 &&
                   !host->uniqueRow(QPointer<QObject>(view.data())))
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
           indexOf(row->view.data())<0 || !row->closeRequest ||
           row->view.data()!=currentWidget())return false;
        // Close callbacks may prompt for Save/Discard. A queued tap
        // targeting an old tab must not close that background painting.
        const quint64 token=row->token;
        const QPointer<QWidget> closingView=row->view;
        const CloseRequest close=row->closeRequest;
        // The native callback can delete the active view, its document,
        // or the entire containing host while a prompt is open. Do not
        // leave an RAII rollback referencing memory after host teardown.
        const QPointer<MobileNativeViewStack> host(this);
        m_closing=true;
        const bool accepted=close();
        if(!host)return accepted;
        if(!accepted) {
            m_closing=false;
            return false;
        }
        // Keep the close guard active through row removal: removeWidget()
        // emits currentChanged and its observers may try to close the
        // newly selected canvas before this transaction has finished.
        // Native callback usually destroys its KisView. A host adapter
        // can also report an accepted close without doing so: after
        // removing the row, retire only a still-owned, unregistered
        // widget. A reparented canvas belongs to its new host.
        forgetToken(token);
        if(!host)return accepted;
        // The native close callback may already have unregistered its
        // view (for example through an engine-side forgetView()) while
        // leaving the QWidget alive under this stack. Row removal is
        // not proof of widget retirement: check the actual ownership at
        // deferred-delete time, including after that reentrant path.
        // A transferred or re-adopted view is preserved by the helper.
        if(closingView)
            retireDetachedView(host,closingView);
        m_closing=false;
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
