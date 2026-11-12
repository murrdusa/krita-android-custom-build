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
#include <QGuiApplication>
#include <QMenu>
#include <QPointer>
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
                         [runtime](Qt::ApplicationState state) {
            // Android Home, activity stop and loss of foreground are
            // proactive save opportunities. No modal Save As dialog.
            if(state!=Qt::ApplicationActive)runtime->checkpointDrafts();
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
            const auto views=m_root->findChildren<KisView*>();
            for(KisView *view:views) {
                if(view && view->viewManager()==m_manager)
                    delete view;
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
           !m_root->openViews().isEmpty() ||
           !document || !saver || !m_host->isVisible() ||
           !KisPart::instance())return nullptr;
        bool inPart=false;
        for(const auto &registered : KisPart::instance()->documents()) {
            if(registered==document) {inPart=true;break;}
        }
        if(!inPart)return nullptr;
        if(document->property("_mobileDraftPath").toString().isEmpty()) {
            const QString title=document->path().isEmpty()?
                document->documentInfo()->aboutInfo(QStringLiteral("title")):
                QFileInfo(document->path()).completeBaseName();
            document->setProperty("_mobileDraftPath",
                                  MobileDraftRecovery::allocate(title));
        }
        auto *view=MobileKritaNativeViewFactory::attach(
            document,m_manager,m_root,saver);
        if(view) {
            QPointer<KisDocument> alive(document);
            connect(document,&KisDocument::sigCompleteBackgroundSaving,this,
                [this,alive](const KritaUtils::ExportFileJob &job,
                             KisImportExportErrorCode status,
                             const QString &,const QString &) {
                // Krita's own slotCompleteAutoSaving publishes the .kra
                // via an atomic rename. Run after all signal receivers so
                // the archive never reads an incomplete .pending.kra file.
                if(status.isOk() &&
                   (job.flags & KritaUtils::SaveInAutosaveMode)) {
                    QTimer::singleShot(0,this,[this,alive,job]() {
                        if(!alive || !ownsDocument(alive))return;
                        const QString draft=alive->property(
                            "_mobileDraftPath").toString();
                        if(job.filePath!=draft+QStringLiteral(".pending.kra") ||
                           !MobileDraftRecovery::isDraft(draft))return;
                        const QString project=alive->path();
                        const QString identity=
                            MobileSavedVersionArchive::canonicalIdentity(project).isEmpty()?
                            draft:project;
                        MobileSavedVersionArchive::enqueueDraftCheckpoint(
                            draft,identity,
                            QGuiApplication::applicationState()!=Qt::ApplicationActive);
                    });
                }
                if(!m_pendingDrafts.isEmpty())
                    QTimer::singleShot(0,this,[this]() {continueDrafts();});
            });
        }
        return view;
    }

private:
    void checkpointDrafts()
    {
        if(!m_root)return;
        // A previous draft may still be queued while another canvas
        // finishes saving. Always collect newly dirty documents: an
        // app-background event must not be lost merely because the queue
        // was already nonempty.
        for(const auto &view:m_root->openViews()) {
            auto *document=qobject_cast<KisDocument*>(view.document.data());
            if(document && document->isModified() &&
               !document->property("_mobileDraftPath").toString().isEmpty() &&
               !m_pendingDrafts.contains(document))
                m_pendingDrafts.append(document);
        }
        continueDrafts();
    }

    void continueDrafts()
    {
        if(!m_root || m_pendingDrafts.isEmpty())return;
        for(const auto &view:m_root->openViews()) {
            auto *document=qobject_cast<KisDocument*>(view.document.data());
            if(document && document->isSaving())return;
        }
        while(!m_pendingDrafts.isEmpty()) {
            const auto document=m_pendingDrafts.takeFirst();
            if(!document || !ownsDocument(document) || !document->isModified())
                continue;
            MobileDraftRecovery::setProject(
                document->property("_mobileDraftPath").toString(),document->path());
            QMetaObject::invokeMethod(document,"slotAutoSave",Qt::DirectConnection);
            if(document && document->isSaving())break;
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
    QSharedPointer<MobileCommandRegistryPort> m_commands;
};
#endif
