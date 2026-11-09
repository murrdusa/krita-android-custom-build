/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Mobile Studio no-reparent prototype assembled from existing mobile
 * controls and Krita engine services. This is intentionally NOT connected
 * to the default launcher until on-device Qt input/EGL testing succeeds.
 * The original Krita centralWidget is never reparented or replaced.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QAction>
#include <QClipboard>
#include <QFrame>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <klocalizedstring.h>

#include "native_canvas_chrome.h"
#include "engine_bridge.h"
#include "brush_workspace.h"
#include "quick_actions.h"
#include "layer_workspace.h"
#include "color_workspace.h"
#include "document_switcher.h"
#include "document_workflow.h"
#include "command_browser.h"
#include "tool_browser.h"
#include "finger_pressure_calibration.h"

class MobileNativeCanvasPilot final
{
public:
    MobileNativeCanvasPilot(QMainWindow *backend,
                            const MobileEngineBridge &bridge)
        : m_backend(backend),m_engine(bridge),m_chrome(backend)
    {
        if(!backend || !backend->centralWidget())return;
        auto *header=new QFrame;
        header->setObjectName(QStringLiteral("MobileNativeDockHeader"));
        auto *headerRow=new QHBoxLayout(header);
        headerRow->setContentsMargins(6,3,6,3);
        headerRow->setSpacing(4);
        header->setStyleSheet(
            "QFrame#MobileNativeDockHeader{background:#25313e;}"
            "QToolButton{background:#405a72;color:white;"
            "border:1px solid #6c8daa;border-radius:7px;"
            "padding:5px;min-height:43px;font-size:13px;}");
        auto *back=makeButton(i18n("Back"),header);
        QObject::connect(back,&QToolButton::clicked,back,[this]() {
            if(m_exit)m_exit();
        });
        headerRow->addWidget(back);
        m_document=makeButton(i18n("Canvases"),header);
        // The document title should not force a wide desktop-sized header
        // on narrow Android phone screens. The full title remains in the
        // tooltip, while the visible button elides to the available space.
        m_document->setMinimumWidth(42);
        m_document->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
        QObject::connect(m_document,&QToolButton::clicked,m_document,[this]() {
            // Return from the Qt button click before opening a new modal
            // QDialog; Android otherwise can stack a native file picker
            // below a still-dispatching toolbar event.
            QTimer::singleShot(0,&m_refresh,[this]() {
                if(!m_backend || !m_chrome.active())return;
                MobileDocumentSwitcher::open(m_engine,m_backend);
                refresh();
            });
        });
        headerRow->addWidget(m_document,1);
        auto *layers=makeButton(i18n("Layers"),header);
        m_layersToggle=layers;
        QObject::connect(layers,&QToolButton::clicked,layers,[this]() {
            if(m_engine.hasDocument() &&
               m_chrome.setSidePanelVisible(!m_chrome.sidePanelVisible()) &&
               m_chrome.sidePanelVisible() && m_layers)
                m_layers->refreshNow();
        });
        headerRow->addWidget(layers);
        auto *color=makeButton(i18n("Color"),header);
        m_colorToggle=color;
        QObject::connect(color,&QToolButton::clicked,color,[this]() {
            if(m_color && m_engine.hasDocument()) {
                m_color->setVisible(!m_color->isVisible());
                if(m_color->isVisible()) {
                    m_color->raise();
                    m_color->refresh();
                }
            }
        });
        headerRow->addWidget(color);
        auto *more=makeButton(i18n("More"),header);
        QObject::connect(more,&QToolButton::clicked,more,[this,more]() {
            showMore(more);
        });
        headerRow->addWidget(more);

        auto *footer=new QFrame;
        footer->setObjectName(QStringLiteral("MobileNativeDockFooter"));
        auto *footerLayout=new QVBoxLayout(footer);
        footerLayout->setContentsMargins(0,0,0,0);
        footerLayout->setSpacing(0);
        m_brush=new KritaMobileBrushWorkspace(m_engine,footer);
        footerLayout->addWidget(m_brush);
        m_quick=new MobileQuickActions(
            m_engine,[this](const QString &id){runQuickAction(id);},footer);
        footerLayout->addWidget(m_quick);

        m_layers=new KritaMobileLayersPanel(m_engine);
        m_layers->onRequestClose=[this]() {
            m_chrome.setSidePanelVisible(false);
        };
        if(!m_chrome.install(header,footer,m_layers)) {
            // No native canvas is touched by a refused dock installation.
            // The adapter owns any panels it successfully installed.
            return;
        }
        m_ready=true;
        // Overlay only the selector itself, not an opaque duplicate canvas.
        // Krita's original widgetStack and MDI views stay directly beneath.
        m_color=new MobileColorWorkspace(m_engine,backend->centralWidget());
        m_color->hide();

        m_refresh.setInterval(450);
        QObject::connect(&m_refresh,&QTimer::timeout,&m_refresh,[this]() {
            if(m_chrome.active())refresh();
        });
    }

    ~MobileNativeCanvasPilot()
    {
        m_refresh.stop();
        if(m_chrome.active())m_chrome.deactivate();
        // The color selector is the one overlay parented directly to the
        // existing canvas; the Qt dock adapter owns other mobile panels.
        delete m_color.data();
    }

    void setExitCallback(std::function<void()> exit) {
        m_exit=std::move(exit);
    }
    bool ready() const {return m_ready && m_chrome.ownsUnmovedCanvas();}
    bool active() const {return m_chrome.active();}

    bool enter()
    {
        if(!ready() || !m_chrome.activate())return false;
        // The native Krita canvas is *not* transferred. This separate
        // flag suppresses the old Android window flash only while our
        // dock-based controls are visible.
        if(m_backend)
            m_backend->setProperty("_mobileStudioPresentationActive",true);
        m_refresh.start();
        refresh();
        return true;
    }

    bool leave()
    {
        m_refresh.stop();
        if(m_color)m_color->hide();
        const bool restored=m_chrome.deactivate();
        if(m_backend)
            m_backend->setProperty("_mobileStudioPresentationActive",false);
        return restored;
    }

    // Android system Back should dismiss transient mobile panels before
    // exiting the stationary canvas interface. No native document or
    // painting widget is touched by this operation.
    bool dismissTransientControls()
    {
        if(m_chrome.sidePanelVisible()) {
            m_chrome.setSidePanelVisible(false);
            return true;
        }
        if(m_color && m_color->isVisible()) {
            m_color->hide();
            return true;
        }
        return false;
    }

    void enforceMobileChrome() {m_chrome.enforceMobileChrome();}

private:
    static QToolButton *makeButton(const QString &title,QWidget *parent)
    {
        auto *button=new QToolButton(parent);
        button->setText(title);
        button->setMinimumHeight(43);
        return button;
    }

    void refresh()
    {
        if(!m_ready || !m_chrome.active())return;
        const bool has=m_engine.hasDocument();
        // A blank document workspace should show only navigation/creation,
        // not an empty brush-and-actions dock consuming the canvas height.
        // Opening the first document restores the same controls in place.
        m_chrome.setBottomPanelVisible(has);
        if(m_layersToggle)m_layersToggle->setEnabled(has);
        if(m_colorToggle)m_colorToggle->setEnabled(has);
        if(m_document) {
            const QString title=m_engine.activeDocumentTitle();
            const QString fullTitle=has?
                (title.isEmpty()?i18n("Canvases"):title)+
                (m_engine.documentModified()?QStringLiteral(" *"):QString()):
                i18n("No document");
            const int available=qMax(38,m_document->width()-18);
            const QString fitted=m_document->fontMetrics().elidedText(
                fullTitle,Qt::ElideMiddle,available);
            if(m_document->text()!=fitted)m_document->setText(fitted);
            m_document->setToolTip(fullTitle);
        }
        if(m_brush) {
            m_brush->setVisible(has);
            if(has)m_brush->refresh();
        }
        if(m_quick) {
            m_quick->setVisible(has);
            if(has)m_quick->refresh();
        }
        if(!has) {
            m_chrome.setSidePanelVisible(false);
            if(m_color)m_color->hide();
        }
        if(m_layers && m_chrome.sidePanelVisible())
            m_layers->refreshFromEngine();
        if(m_color && m_color->isVisible()) {
            m_color->raise();
            m_color->refresh();
        }
    }

    void runQuickAction(const QString &id)
    {
        if(!m_backend || !m_chrome.active())return;

        // Native Android dialogs must be opened after the Quick Action's
        // QToolButton click has returned. A Close action captures the exact
        // native QMdiSubWindow identity NOW; the view may disappear while
        // the dialog is being created or while Qt processes a reentrant
        // event loop. No shifting numeric MDI index fallback is allowed.
        const bool opensDialog=
            id==QStringLiteral("mobile:new") ||
            id==QStringLiteral("mobile:open") ||
            id==QStringLiteral("mobile:save") ||
            id==QStringLiteral("mobile:save_as") ||
            id==QStringLiteral("mobile:canvases") ||
            id==QStringLiteral("mobile:tools") ||
            id==QStringLiteral("mobile:commands") ||
            id==QStringLiteral("mobile:menus") ||
            id==QStringLiteral("mobile:close") ||
            id==QStringLiteral("mobile:more");
        if(opensDialog) {
            QPointer<QObject> selectedView;
            if(id==QStringLiteral("mobile:close")) {
                const auto documents=m_engine.openDocuments();
                for(const auto &document:documents) {
                    if(!document.active)continue;
                    // Ambiguous current view: refuse rather than risk
                    // closing a different modified drawing.
                    if(selectedView || !document.nativeView)return;
                    selectedView=document.nativeView;
                }
                if(!selectedView)return;
            }
            QTimer::singleShot(0,&m_refresh,[this,id,selectedView]() {
                if(!m_backend || !m_chrome.active())return;
                if(id==QStringLiteral("mobile:new"))
                    MobileDocumentWorkflow::create(m_engine,m_backend);
                else if(id==QStringLiteral("mobile:open"))
                    MobileDocumentWorkflow::open(m_engine,m_backend);
                else if(id==QStringLiteral("mobile:save"))
                    MobileDocumentWorkflow::save(m_engine,m_backend);
                else if(id==QStringLiteral("mobile:save_as"))
                    MobileDocumentWorkflow::save(m_engine,m_backend,true);
                else if(id==QStringLiteral("mobile:canvases"))
                    MobileDocumentSwitcher::open(m_engine,m_backend);
                else if(id==QStringLiteral("mobile:tools"))
                    MobileToolBrowser::open(m_engine,m_backend);
                else if(id==QStringLiteral("mobile:commands") ||
                        id==QStringLiteral("mobile:menus"))
                    MobileCommandBrowser::open(m_engine,m_backend);
                else if(id==QStringLiteral("mobile:more"))
                    showMore(m_document);
                else if(id==QStringLiteral("mobile:close") && selectedView)
                    m_engine.closeDocumentView(selectedView);
                refresh();
            });
            return;
        }

        if(id.startsWith(QStringLiteral("native:"))) {
            QAction *action=m_engine.action(id.mid(7));
            if(action && action->isEnabled())action->trigger();
        } else if(id.startsWith(QStringLiteral("tool:"))) {
            m_engine.selectTool(id.mid(5));
        } else if(id==QStringLiteral("mobile:undo")) {
            m_engine.run("edit_undo");
        } else if(id==QStringLiteral("mobile:redo")) {
            m_engine.run("edit_redo");
        } else if(id==QStringLiteral("mobile:layers")) {
            if(m_engine.hasDocument())
                m_chrome.setSidePanelVisible(!m_chrome.sidePanelVisible());
        } else if(id==QStringLiteral("mobile:color")) {
            if(m_color && m_engine.hasDocument())
                m_color->setVisible(!m_color->isVisible());
        }
        refresh();
    }

    void showMore(QWidget *anchor)
    {
        if(!m_backend)return;
        QMenu menu(anchor?anchor:m_backend.data());
        auto *browser=menu.addAction(i18n("Menus — File, Edit, View and more"));
        QObject::connect(browser,&QAction::triggered,browser,[this]() {
            QTimer::singleShot(0,&m_refresh,[this]() {
                if(m_backend)MobileCommandBrowser::open(m_engine,m_backend);
            });
        });
        auto *tools=menu.addAction(i18n("Tools — Krita registry"));
        QObject::connect(tools,&QAction::triggered,tools,[this]() {
            QTimer::singleShot(0,&m_refresh,[this]() {
                if(m_backend)MobileToolBrowser::open(m_engine,m_backend);
            });
        });
        menu.addSeparator();
        auto *nativeMode=menu.addAction(i18n(
            "Experimental stationary canvas UI (next restart)"));
        nativeMode->setCheckable(true);
        nativeMode->setChecked(QSettings().value(
            QStringLiteral("MobileStudio/nativeDockCanvasV1"),false).toBool());
        QObject::connect(nativeMode,&QAction::toggled,nativeMode,[](bool on) {
            QSettings settings;
            settings.setValue(QStringLiteral("MobileStudio/nativeDockCanvasV1"),on);
            settings.sync();
        });
        auto *pressure=menu.addAction(i18n("Experimental finger pressure"));
        pressure->setCheckable(true);
        pressure->setChecked(QSettings().value(
            QStringLiteral("MobileStudio/fingerContactPressureV1"),false)
            .toBool());
        QObject::connect(pressure,&QAction::toggled,pressure,[](bool on) {
            QSettings settings;
            settings.setValue(
                QStringLiteral("MobileStudio/fingerContactPressureV1"),on);
            settings.sync();
        });
        auto *calibrate=menu.addAction(i18n(
            "Finger pressure calibration and live diagnostics"));
        QObject::connect(calibrate,&QAction::triggered,calibrate,[this]() {
            QTimer::singleShot(0,&m_refresh,[this]() {
                if(!m_backend)return;
                MobileFingerPressureCalibrationDialog dialog(m_backend);
                dialog.exec();
            });
        });
        menu.addSeparator();
        auto *diagnostics=menu.addAction(
            i18n("Copy stationary canvas diagnostics"));
        QObject::connect(diagnostics,&QAction::triggered,diagnostics,[this]() {
            QTimer::singleShot(0,&m_refresh,[this]() {
                if(!m_backend || !QGuiApplication::clipboard())return;
                const QString report=QStringList({
                    QStringLiteral("Krita Mobile Studio stationary canvas v1"),
                    QStringLiteral("Native central widget unchanged: %1")
                        .arg(m_chrome.ownsUnmovedCanvas()),
                    QStringLiteral("Mobile chrome active: %1")
                        .arg(m_chrome.active()),
                    QStringLiteral("Mobile layers dock visible: %1")
                        .arg(m_chrome.sidePanelVisible()),
                    QStringLiteral("Mobile layers dock location: %1")
                        .arg(m_chrome.sidePanelAtBottom()?
                             QStringLiteral("bottom (portrait)"):
                             QStringLiteral("side (wide)")),
                    QStringLiteral("Native window: %1 x %2 logical px")
                        .arg(m_backend->width()).arg(m_backend->height()),
                    QStringLiteral("Mobile brush dock visible: %1")
                        .arg(m_chrome.bottomPanelVisible()),
                    QStringLiteral("Mobile color overlay visible: %1")
                        .arg(m_color && m_color->isVisible()),
                    QStringLiteral("Open Krita document views: %1")
                        .arg(m_engine.openDocuments().size()),
                    QStringLiteral("Finger pressure simulation enabled: %1")
                        .arg(QSettings().value(
                            QStringLiteral("MobileStudio/fingerContactPressureV1"),
                            false).toBool()),
                    QStringLiteral("Response gamma: %1")
                        .arg(MobileFingerPressureSettings::readResponse(
                            QSettings()).gamma,0,'f',2),
                    QStringLiteral("Stability: %1%")
                        .arg(MobileFingerPressureSettings::readResponse(
                            QSettings()).stability*100.0,0,'f',0),
                    QStringLiteral("Raw Android preferred: %1")
                        .arg(MobileFingerPressureSettings::readResponse(
                            QSettings()).useRawAndroid),
                    QStringLiteral("Last brush contact source: %1")
                        .arg(qApp->property(
                            "_mobileFingerAreaLastSource").toString()),
                    QStringLiteral("Brush touch levels: %1")
                        .arg(qApp->property(
                            "_mobileFingerAreaLastUniqueSteps").toInt()),
                    QStringLiteral("Latest Android raw contact: %1 logical px")
                        .arg(qApp->property(
                            "_mobileFingerRawDiameter").toDouble(),0,'f',1)
                }).join(QLatin1Char('\n'));
                QGuiApplication::clipboard()->setText(report);
            });
        });
        const QPoint at=anchor?
            anchor->mapToGlobal(QPoint(0,anchor->height())):
            m_backend->mapToGlobal(QPoint(0,50));
        menu.exec(at);
    }

    QPointer<QMainWindow> m_backend;
    MobileEngineBridge m_engine;
    MobileNativeCanvasChrome m_chrome;
    QPointer<QToolButton> m_document;
    QPointer<QToolButton> m_layersToggle;
    QPointer<QToolButton> m_colorToggle;
    QPointer<KritaMobileBrushWorkspace> m_brush;
    QPointer<MobileQuickActions> m_quick;
    QPointer<KritaMobileLayersPanel> m_layers;
    QPointer<MobileColorWorkspace> m_color;
    QTimer m_refresh;
    std::function<void()> m_exit;
    bool m_ready=false;
};
#endif // Q_OS_ANDROID
