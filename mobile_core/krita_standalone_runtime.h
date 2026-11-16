/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Opt-in Krita engine bootstrap for a QMainWindow without KisMainWindow.
 * Device-validated for basic painting, Undo/Redo and KRA save/reopen.
 * Optional native-startup and controller interactions require further testing.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QAction>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QSettings>
#include <QGuiApplication>
#include <QMenu>
#include <QPointer>
#include <QScopeGuard>
#include <QSharedPointer>
#include <QToolButton>
#include <QTimer>
#include <kactioncollection.h>
#include <KoToolFactoryBase.h>
#include <KoToolRegistry.h>
#include <KoDocumentInfo.h>
#include "KisPart.h"
#include "KisDocument.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "lightweight_canvas_host.h"
#include "krita_native_view_factory.h"
#include "mobile_draft_recovery.h"
#include "saved_version_archive.h"

class MobileStandaloneKritaActions final : public MobileCommandRegistryPort
{
public:
    MobileStandaloneKritaActions(KisKActionCollection *actions,QMainWindow *host)
        : m_actions(actions),m_host(host) {}
    QAction *action(const QString &id) const override
    {
        return m_actions ? m_actions->action(id) : nullptr;
    }
    QList<QAction*> availableActions() const override
    {
        if(!m_actions)return {};
        auto actions=m_actions->actions();
        for(const char *id:{"view_toggledockers","view_toggledockertitlebars",
                            "lock_toolbars","options_configure_toolbars",
                            "showStatusBar","view_show_canvas_only"}) {
            if(auto *desktop=m_actions->action(QString::fromLatin1(id)))
                actions.removeAll(desktop);
        }
        return actions;
    }
    QList<QAction*> menuRootActions() const override
    {
        if(!m_host)return {};
        const auto *menu=m_host->findChild<QMenu*>(
            QStringLiteral("_mobileNativeSettingsMenu"));
        return menu?menu->actions():QList<QAction*>{};
    }
private:
    QPointer<KisKActionCollection> m_actions;
    QPointer<QMainWindow> m_host;
};

class MobileKritaStandaloneRuntime final : public QObject
{
public:
    static MobileKritaStandaloneRuntime *create(MobileLightweightCanvasHost *host)
    {
        if(!host || !KisPart::instance() || !host->nativeViewPort())
            return nullptr;
        auto *root=dynamic_cast<MobileNativeViewStack*>(host->nativeCanvas());
        if(!root || root->parentWidget()!=host ||
           root->window()!=host->window() ||
           !root->openViews().isEmpty() ||
           host->findChild<QObject*>(
               QStringLiteral("_mobileKritaRuntime"),Qt::FindDirectChildrenOnly))
            return nullptr;

        auto *runtime=new MobileKritaStandaloneRuntime(host,root);
        // Diagnostic collection is opt-in and has no effect on painting
        // or autosave when disabled.
        runtime->m_memoryDiagnosticsEnabled=QSettings().value(
            QStringLiteral("MobileStudio/memoryDiagnosticsV1"),false).toBool();
        if(runtime->m_memoryDiagnosticsEnabled) {
            runtime->reportMemorySnapshot("startup");
            auto *memoryTimer=new QTimer(runtime);
            memoryTimer->setInterval(60000);
            QObject::connect(memoryTimer,&QTimer::timeout,runtime,
                             [runtime]() {
                runtime->reportMemorySnapshot("periodic");
            });
            memoryTimer->start();
        }
        runtime->m_actions=new KisKActionCollection(
            runtime,QStringLiteral("krita-mobile-native"));
        runtime->m_actions->setComponentDisplayName(
            QStringLiteral("Krita Mobile Studio"));
        runtime->m_manager=new KisViewManager(host,runtime->m_actions);
        // KisViewManager(QWidget*,...) does not set its QObject parent.
        runtime->m_manager->setParent(runtime);
        runtime->m_manager->setQtMainWindow(host);
        // Krita's KisMainWindow registers plugin tool actions before
        // KisView::setViewManager() requests the active brush. Without
        // this, KoToolManager can select a tool whose actions were never
        // created for the independent host.
        for(KoToolFactoryBase *factory:KoToolRegistry::instance()->values()) {
            if(factory)factory->createActions(runtime->m_actions.data());
        }
        runtime->m_actions->addAssociatedWidget(host);
        runtime->m_commands=QSharedPointer<MobileStandaloneKritaActions>::create(
            runtime->m_actions,host);
        auto *draftTimer=new QTimer(runtime);
        QObject::connect(draftTimer,&QTimer::timeout,runtime,
                         [runtime]() {runtime->checkpointDrafts();});
        // Android does not guarantee an onDestroy callback when the user
        // swipes the task away or the OS kills the process. Keep a recent
        // completed private KRA checkpoint rather than relying on shutdown
        // to save a named document synchronously.
        draftTimer->start(15000);
        QObject::connect(qGuiApp,&QGuiApplication::applicationStateChanged,runtime,
                         [runtime,active=QGuiApplication::applicationState()==
                                          Qt::ApplicationActive](Qt::ApplicationState state) mutable {
            const bool wasActive=active;
            active=state==Qt::ApplicationActive;
            if(wasActive && !active)runtime->checkpointDrafts();
        });
        QObject::connect(qGuiApp,&QCoreApplication::aboutToQuit,runtime,
                         [runtime]() {
            // Best-effort final checkpoint on an orderly app exit.
            // A forced process kill skips this callback, so the periodic
            // and foreground-loss checkpoints remain authoritative.
            runtime->checkpointDrafts();
        });
        return runtime;
    }

    ~MobileKritaStandaloneRuntime() override
    {
        // KisView::~KisView() calls back into KisViewManager; never free
        // the manager or its action collection while a live native view
        // is still attached. The host's central widget may already be gone.
        if(m_root && m_manager) {
            // Include hidden/removed native views as well as those
            // with a still-live document identity. An unregistered
            // QWidget must not outlive the view manager either.
            const auto children=m_root->findChildren<KisView*>();
            QList<QPointer<KisView>> views;
            views.reserve(children.size());
            for(KisView *view:children)views.append(view);
            // Destroying one view can synchronously close another view
            // through KisPart/KisViewManager. Never revisit a dangling
            // pointer from a previously captured QObject child list.
            for(const auto &view:views) {
                if(view && m_manager && view->viewManager()==m_manager.data())
                    delete view.data();
            }
        }
        delete m_manager.data();
        delete m_actions.data();
    }

    KisViewManager *viewManager() const {return m_manager.data();}
    QSharedPointer<MobileCommandRegistryPort> commandPort() const
    {
        return m_commands;
    }
    MobileLightweightCanvasHost *window() const {return m_host.data();}
    MobileNativeViewStack *documentRoot() const {return m_root.data();}

    bool ownsDocument(KisDocument *document) const
    {
        if(!document || !m_root || !m_manager ||
           !KisPart::instance())return false;
        bool registered=false;
        for(const auto &candidate:KisPart::instance()->documents()) {
            if(candidate==document) {registered=true;break;}
        }
        if(!registered)return false;
        // A registered document may belong to a desktop fallback or a
        // second Qt window. Validate ownership against native view IDs.
        for(const auto &view:m_root->openViews()) {
            if(view.document==document && view.nativeView)return true;
        }
        return false;
    }

    KisView *attachDocument(KisDocument *document,
                            MobileKritaNativeSaveHost *saver)
    {
        if(!m_host || !m_root || !m_manager || !m_actions ||
           !document || !saver || !m_host->isVisible() ||
           !KisPart::instance())return nullptr;
        // The native view stack supports multiple open documents. The
        // old single-view guard prevented a second canvas from ever
        // attaching, despite the manager already routing tab activation.
        // Refuse only duplicate attachments of the *same* document.
        for(const auto &existing:m_root->openViews())
            if(existing.document==document)return nullptr;
        bool inPart=false;
        for(const auto &registered : KisPart::instance()->documents()) {
            if(registered==document) {inPart=true;break;}
        }
        if(!inPart)return nullptr;
        // A draft-path property update notifies QObject observers before
        // the view factory runs. Capture identities before that callback
        // can close the document, native root, or entire runtime.
        const QPointer<MobileKritaStandaloneRuntime> alive(this);
        const QPointer<KisDocument> liveDocument(document);
        const QPointer<MobileNativeViewStack> root=m_root;
        const QPointer<KisViewManager> manager=m_manager;
        if(document->property("_mobileDraftPath").toString().isEmpty()) {
            const QString title=document->path().isEmpty()?
                document->documentInfo()->aboutInfo(QStringLiteral("title")):
                QFileInfo(document->path()).completeBaseName();
            if(!alive || !liveDocument || !root || !manager)return nullptr;
            document->setProperty("_mobileDraftPath",
                                  MobileDraftRecovery::allocate(title));
        }
        if(!alive || !liveDocument || !root || !manager ||
           alive->m_root!=root || alive->m_manager!=manager ||
           !alive->m_host || !alive->m_host->isVisible())
            return nullptr;
        // The view factory enters Krita loading, canvas and Qt selection
        // callbacks. They can synchronously close this runtime or the
        // document. Revalidate both before registering more callbacks.
        auto *view=MobileKritaNativeViewFactory::attach(
            liveDocument.data(),manager.data(),root.data(),saver);
        if(!alive || !liveDocument || !view)return nullptr;
        // QObject connection setup may notify engine observers during
        // native view initialization. Check lifetime after every hook
        // before using either the runtime or KisDocument again.
        connect(liveDocument.data(),&KisDocument::sigMobileDraftPublished,this,
            [](const QString &draft,const QString &project) {
                MobileSavedVersionArchive::enqueueDraftCheckpoint(
                    draft,project.isEmpty()?draft:project,
                    QGuiApplication::applicationState()!=Qt::ApplicationActive);
            });
        if(!alive || !liveDocument || !root || !manager ||
           alive->m_root!=root || alive->m_manager!=manager)return nullptr;
        connect(liveDocument.data(),&KisDocument::sigCompleteBackgroundSaving,this,
            [alive]() {
                if(alive)alive->scheduleDraftContinuation();
            });
        if(!alive || !liveDocument || !root || !manager ||
           alive->m_root!=root || alive->m_manager!=manager)return nullptr;
        // A tab may be closed while its background save completes.
        // Drop its queued identity immediately rather than retaining
        // a dead entry until another document finishes saving.
        connect(liveDocument.data(),&QObject::destroyed,this,
            [alive,document]() {
                if(!alive)return;
                for(int i=alive->m_pendingDrafts.size()-1;i>=0;--i)
                    if(!alive->m_pendingDrafts.at(i) ||
                       alive->m_pendingDrafts.at(i).data()==document)
                        alive->m_pendingDrafts.removeAt(i);
                // Native view retirement is queued after the document
                // destructor. Sample on the next event-loop turn rather
                // than counting its still-attached GL widget as a leak.
                if(alive->m_memoryDiagnosticsEnabled)
                    QTimer::singleShot(0,alive.data(),[alive]() {
                        if(alive)alive->reportMemorySnapshot("closed");
                    });
            });
        if(!liveDocument || !alive || !root || !manager ||
           alive->m_root!=root || alive->m_manager!=manager)return nullptr;
        if(m_memoryDiagnosticsEnabled) {
            reportMemorySnapshot("attached");
            if(!alive)return nullptr;
        }
        return view;
    }

private:
    // Android/Linux process metrics are advisory: Qt/Krita allocators
    // and GPU drivers may retain caches after a canvas is closed. Never
    // interpret a single RSS increase as proof of a document memory leak.
    void reportMemorySnapshot(const char *phase) const
    {
        if(!m_memoryDiagnosticsEnabled)return;
        QFile status(QStringLiteral("/proc/self/status"));
        if(!status.open(QIODevice::ReadOnly))return;
        const QByteArray contents=status.read(32768);
        const auto kib=[&contents](const QByteArray &name) -> qint64 {
            const QByteArray key=name+':';
            const int start=contents.indexOf(key);
            if(start<0 || (start>0 && contents.at(start-1)!='\n'))
                return -1;
            const int lineEnd=contents.indexOf('\n',start);
            const QByteArray line=contents.mid(
                start+key.size(),lineEnd<0?-1:lineEnd-start-key.size()).trimmed();
            const QByteArray digits=line.split(' ').value(0);
            bool ok=false;
            const qint64 value=digits.toLongLong(&ok);
            return ok?value:-1;
        };
        int nativeViews=0;
        int documents=0;
        if(m_root) {
            const auto views=m_root->openViews();
            nativeViews=views.size();
            for(const auto &view:views)
                if(view.document)++documents;
        }
        qInfo().noquote()
            << "MobileStudioMemory"
            << phase
            << "rssKiB" << kib("VmRSS")
            << "peakRssKiB" << kib("VmHWM")
            << "anonKiB" << kib("RssAnon")
            << "swapKiB" << kib("VmSwap")
            << "nativeViews" << nativeViews
            << "registeredDocuments" << documents;
    }

    void checkpointDrafts()
    {
        const QPointer<MobileNativeViewStack> originalRoot=m_root;
        if(!originalRoot)return;
        if(m_checkpointing) {
            // A foreground-loss or quit request can arrive from inside
            // Krita's nested save event loop. Do not discard that later
            // checkpoint; coalesce it into one follow-up pass.
            m_checkpointAgain=true;
            return;
        }
        m_checkpointing=true;
        const QPointer<MobileKritaStandaloneRuntime> alive(this);
        // slotAutoSave can enter a nested Qt event loop: app background
        // and quit callbacks must not recursively start the same pass.
        // Reset only if its QObject survived the synchronous callback.
        const auto release=qScopeGuard([alive]() {
            if(!alive)return;
            alive->m_checkpointing=false;
            const bool again=alive->m_checkpointAgain;
            alive->m_checkpointAgain=false;
            // Preserve a background/exit request received during a save,
            // but schedule only one additional pass outside this stack.
            if(again)
                QTimer::singleShot(0,alive.data(),[alive]() {
                    if(alive)alive->checkpointDrafts();
                });
            // A background save may have completed inside slotAutoSave's
            // nested event loop. Drain its pending checkpoint after this
            // pass, never recursively while it is traversing native views.
            alive->scheduleDraftContinuation();
        });
        // An inactive canvas can contain unsaved strokes too. Checkpoint
        // every distinct document hosted by this runtime, not just the
        // currently selected tab. Never activate a view to save it.
        QList<QPointer<KisDocument>> seen;
        for(const auto &view:originalRoot->openViews()) {
            if(!alive || !originalRoot ||
               alive->m_root!=originalRoot)return;
            const QPointer<KisDocument> document=
                qobject_cast<KisDocument*>(view.document.data());
            if(!document || seen.contains(document))continue;
            seen.append(document);
            // Ownership queries and document state observers can run
            // synchronous Qt callbacks, including closing this canvas.
            // Recheck the document AND runtime between each operation.
            if(!ownsDocument(document.data())) {
                if(!alive || !originalRoot ||
                   alive->m_root!=originalRoot)return;
                continue;
            }
            if(!alive || !originalRoot || !document ||
               alive->m_root!=originalRoot)return;
            const bool modified=document->isModified();
            if(!alive || !originalRoot || !document ||
               alive->m_root!=originalRoot)return;
            if(!modified)continue;
            const QString draft=document->property(
                "_mobileDraftPath").toString();
            if(!alive || !originalRoot || !document ||
               alive->m_root!=originalRoot)return;
            if(draft.isEmpty())continue;
            const bool saving=document->isSaving();
            if(!alive || !originalRoot || !document ||
               alive->m_root!=originalRoot)return;
            if(saving) {
                if(!m_pendingDrafts.contains(document))
                    m_pendingDrafts.append(document);
            } else {
                m_pendingDrafts.removeAll(document);
                if(document)
                    QMetaObject::invokeMethod(document.data(),"slotAutoSave",
                                              Qt::DirectConnection);
                // A native Save can close the final view/window. Stop
                // iterating immediately if its runtime was torn down.
                if(!alive || !originalRoot ||
                   alive->m_root!=originalRoot)return;
            }
        }
    }

    void scheduleDraftContinuation()
    {
        if(m_pendingDrafts.isEmpty() || m_draftContinuationQueued)return;
        m_draftContinuationQueued=true;
        const QPointer<MobileKritaStandaloneRuntime> alive(this);
        QTimer::singleShot(0,this,[alive]() {
            if(!alive)return;
            alive->m_draftContinuationQueued=false;
            // If a nested event loop delivered this while checkpointing,
            // that pass's exit guard will queue the remaining work.
            if(!alive->m_checkpointing)alive->continueDrafts();
        });
    }

    void continueDrafts()
    {
        if(m_checkpointing)return;
        const QPointer<MobileKritaStandaloneRuntime> alive(this);
        const QPointer<MobileNativeViewStack> originalRoot=m_root;
        if(!originalRoot)return;
        // Finish queued checkpoints independently: a busy document must
        // never block another open canvas from being protected.
        const auto pending=m_pendingDrafts;
        for(const auto &document:pending) {
            if(!alive || !originalRoot ||
               alive->m_root!=originalRoot)return;
            if(!document) {
                m_pendingDrafts.removeAll(QPointer<KisDocument>());
                continue;
            }
            const bool saving=document->isSaving();
            if(!alive || !originalRoot || !document ||
               alive->m_root!=originalRoot)return;
            if(saving)continue;
            if(!m_pendingDrafts.removeAll(document))continue;
            const bool owned=ownsDocument(document.data());
            if(!alive || !originalRoot || !document ||
               alive->m_root!=originalRoot)return;
            if(!owned)continue;
            const bool modified=document->isModified();
            if(!alive || !originalRoot || !document ||
               alive->m_root!=originalRoot)return;
            if(!modified)continue;
            const QString draft=document->property(
                "_mobileDraftPath").toString();
            if(!alive || !originalRoot || !document ||
               alive->m_root!=originalRoot)return;
            if(!draft.isEmpty())
                QMetaObject::invokeMethod(document.data(),"slotAutoSave",
                                          Qt::DirectConnection);
            if(!alive || !originalRoot ||
               alive->m_root!=originalRoot)return;
        }
    }

    MobileKritaStandaloneRuntime(MobileLightweightCanvasHost *host,
                                 MobileNativeViewStack *root)
        : QObject(host),m_host(host),m_root(root)
    {
        setObjectName(QStringLiteral("_mobileKritaRuntime"));
    }

    QPointer<MobileLightweightCanvasHost> m_host;
    QPointer<MobileNativeViewStack> m_root;
    QPointer<KisKActionCollection> m_actions;
    QPointer<KisViewManager> m_manager;
    QList<QPointer<KisDocument>> m_pendingDrafts;
    bool m_checkpointing=false;
    bool m_checkpointAgain=false;
    bool m_draftContinuationQueued=false;
    bool m_memoryDiagnosticsEnabled=false;
    QSharedPointer<MobileCommandRegistryPort> m_commands;
};
#endif
