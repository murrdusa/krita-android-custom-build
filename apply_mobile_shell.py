#!/usr/bin/env python3
"""Install the opt-in independent mobile WINDOW into the Krita Android tree.

This is an incremental migration path, not yet a standalone executable.
Run AFTER existing patches. The window hosts Krita's native MDI canvas
while desktop KisMainWindow temporarily remains the engine service.
All edits are source-anchored and reversible by switching the mode off.
"""
from pathlib import Path
import hashlib
import re
import sys

def one(source, old, new, label):
    count=source.count(old)
    if count!=1:
        raise RuntimeError(f"Mobile shell {label}: expected 1 anchor, got {count}")
    return source.replace(old,new,1)

def redirect_android_engine_method(source:str, return_type:str, name:str, android_body:str):
    """Move Android resource/registry setup into a standalone engine TU.

    Keep the original upstream body for non-Android builds; the Android
    compiler only uses MobileKritaEngineInitialization's implementation.
    """
    signature=f"{return_type} KisApplication::{name}()"
    if source.count(signature)!=1:
        raise RuntimeError(f"Expected one Krita {name} function")
    at=source.index(signature)
    brace=source.index("{",at+len(signature))
    if source[at+len(signature):brace].strip():
        raise RuntimeError(f"Unexpected Krita {name} function syntax")
    depth=0
    end=brace
    while end<len(source):
        if source[end]=="{": depth+=1
        elif source[end]=="}":
            depth-=1
            if depth==0: break
        end+=1
    if depth!=0:
        raise RuntimeError(f"Unbalanced Krita {name} body")
    body=source[brace+1:end]
    old=source[at:end+1]
    replacement=(signature+"\n{\n#ifdef Q_OS_ANDROID\n"+
                 android_body+"\n#else"+body+"#endif\n}")
    return one(source,old,replacement,f"Android engine {name} extraction")


def install_phone_brush_editor(root: Path):
    resource_dir = root / "libs/resources"
    protection = Path(__file__).resolve().parent / "mobile_core/factory_brush_preset.h"
    (resource_dir / protection.name).write_text(protection.read_text(encoding="utf-8"), encoding="utf-8")
    resource_model = resource_dir / "KisResourceModel.cpp"
    resource_source = resource_model.read_text(encoding="utf-8")
    resource_source = one(resource_source, '#include "KisResourceModel.h"',
                          '#include "KisResourceModel.h"\n#ifdef Q_OS_ANDROID\n#include "factory_brush_preset.h"\n#endif',
                          "factory brush protection include")
    for signature, guard in [
        ("bool KisAllResourcesModel::setResourceActive(const QModelIndex &index, bool value)",
         "if (!value && index.isValid() && !MobileFactoryBrushPreset::sourcePath(resourceForIndex(index)).isEmpty()) return false;"),
        ("bool KisAllResourcesModel::renameResource(KoResourceSP resource, const QString &name)",
         "if (!MobileFactoryBrushPreset::sourcePath(resource).isEmpty()) return false;"),
    ]:
        anchor = signature + "\n{\n"
        resource_source = one(resource_source, anchor,
                              anchor + "#ifdef Q_OS_ANDROID\n    " + guard + "\n#endif\n",
                              "protect factory brush preset")
    resource_model.write_text(resource_source, encoding="utf-8")

    paintop_box = root / "libs/ui/kis_paintop_box.cc"
    source = paintop_box.read_text(encoding="utf-8")
    source = one(source, "    m_presetsEditor = new KisPaintOpPresetsEditor(",
                 """#ifdef Q_OS_ANDROID
    const bool mobileBrushEditor = !m_viewManager->mainWindow();
    m_brushEditorPopupButton->setProperty("_mobileBrushEditor", mobileBrushEditor);
#endif
    m_presetsEditor = new KisPaintOpPresetsEditor(""", "mark standalone brush editor")
    source = one(source, "    m_brushEditorPopupButton->setPopupWidgetDetached(cfg.paintopPopupDetached());",
                 """#ifdef Q_OS_ANDROID
    m_brushEditorPopupButton->setPopupWidgetDetached(mobileBrushEditor || cfg.paintopPopupDetached());
#else
    m_brushEditorPopupButton->setPopupWidgetDetached(cfg.paintopPopupDetached());
#endif""", "use a dialog for the mobile brush editor")
    paintop_box.write_text(source, encoding="utf-8")

    editor = root / "libs/ui/widgets/kis_paintop_presets_editor.cpp"
    source = editor.read_text(encoding="utf-8")
    source = one(source, "#include <QSplitter>",
                 """#include <QSplitter>
#ifdef Q_OS_ANDROID
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>
#include <KisResourceItemListView.h>
#include "touch_scroll.h"
#endif""", "mobile brush editor includes")
    source = one(source, "    QSplitter* horzSplitter {0};",
                 """    QSplitter* horzSplitter {0};
#ifdef Q_OS_ANDROID
    QTabBar *mobileTabs = nullptr;
    QMetaObject::Connection mobileScreenConnection;
#endif""", "mobile brush editor state")
    source = one(source, "    // Responsive Layout\n", """#ifdef Q_OS_ANDROID
    if (parent && parent->property("_mobileBrushEditor").toBool()) {
        auto &ui = m_d->uiWdgPaintOpPresetSettings;
        ui.gridLayout->setContentsMargins(6, 6, 6, 6);
        ui.gridLayout->setSpacing(4);
        ui.brushEditorDashboard->setDirection(QBoxLayout::TopToBottom);
        ui.brushEditorDashboard->setSpacing(3);
        ui.presetThumbnailicon->hide();
        ui.liveBrushPreviewView->setMinimumWidth(0);
        ui.liveBrushPreviewView->setMaximumWidth(QWIDGETSIZE_MAX);
        ui.liveBrushPreviewView->setFixedHeight(42);
        ui.liveBrushPreviewView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        ui.currentBrushNameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        ui.currentBrushNameLabel->setWordWrap(true);
        ui.currentBrushEngineLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        ui.verticalLayout_5->setDirection(QBoxLayout::LeftToRight);
        for (auto *button : {ui.saveNewBrushPresetButton, ui.saveBrushPresetButton}) {
            button->setMinimumHeight(38);
            button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        }
        ui.showPresetsButton->hide();
        ui.showScratchpadButton->hide();
        ui.viewOptionButton->hide();

        auto *behavior = new QWidget(this);
        auto *behaviorLayout = new QVBoxLayout(behavior);
        ui.brushEditorLayout->removeItem(ui.horizontalLayout_3);
        ui.horizontalLayout_3->setParent(nullptr);
        ui.horizontalLayout_3->setDirection(QBoxLayout::TopToBottom);
        behaviorLayout->addLayout(ui.horizontalLayout_3);
        behaviorLayout->addStretch(1);

        m_d->mobileTabs = new QTabBar(this);
        m_d->mobileTabs->setStyleSheet(QStringLiteral("QTabBar::tab{min-height:39px;padding:0 8px;}"));
        auto *pages = new QStackedWidget(this);
        const auto addPage = [this, pages](QWidget *page, const QString &label) {
            page->setMinimumSize(0, 0);
            page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
            pages->addWidget(page);
            m_d->mobileTabs->addTab(label);
        };
        addPage(ui.brushEditorSettingsControls, i18n("Brush"));
        addPage(ui.scratchpadControls, i18n("Test"));
        addPage(ui.presetsContainer, i18n("Presets"));
        auto *behaviorScroll = new QScrollArea(this);
        behaviorScroll->setFrameShape(QFrame::NoFrame);
        behaviorScroll->setWidgetResizable(true);
        behaviorScroll->setWidget(behavior);
        MobileTouchScroll::configure(behaviorScroll);
        addPage(behaviorScroll, i18n("Behavior"));
        MobileTouchScroll::configureList(ui.presetWidget->itemChooser()->itemView());
        connect(m_d->mobileTabs, &QTabBar::currentChanged, pages, &QStackedWidget::setCurrentIndex);
        auto *done = new QPushButton(i18n("Done"), this);
        done->setMinimumHeight(42);
        connect(done, &QPushButton::clicked, this, [this]() {
            if (parentWidget()) parentWidget()->hide();
        });
        ui.gridLayout->addWidget(m_d->mobileTabs, 1, 0, 1, 3);
        ui.gridLayout->addWidget(pages, 2, 0, 1, 3);
        ui.gridLayout->addWidget(done, 3, 0, 1, 3);
        ui.gridLayout->setRowStretch(2, 1);
        return;
    }
#endif

    // Responsive Layout
""", "tabbed native mobile brush editor")
    source = one(source, """        if (m_d->settingsWidget->supportScratchBox() && cfg.scratchpadVisible()) {""",
                 """#ifdef Q_OS_ANDROID
        if (m_d->mobileTabs) {
            const bool canTest = m_d->settingsWidget->supportScratchBox();
            if (!canTest && m_d->mobileTabs->currentIndex() == 1)
                m_d->mobileTabs->setCurrentIndex(0);
            m_d->mobileTabs->setTabEnabled(1, canTest);
        }
#endif
        if (m_d->settingsWidget->supportScratchBox() && cfg.scratchpadVisible()) {""",
                 "update scratchpad availability on preset switch")
    source = one(source, "void KisPaintOpPresetsEditor::hideEvent(QHideEvent *event)\n{",
                 """void KisPaintOpPresetsEditor::hideEvent(QHideEvent *event)
{
#ifdef Q_OS_ANDROID
    if (m_d->mobileTabs) {
        disconnect(m_d->mobileScreenConnection);
        QWidget::hideEvent(event);
        return;
    }
#endif""", "keep mobile editor out of desktop geometry persistence")
    source = one(source, "void KisPaintOpPresetsEditor::showEvent(QShowEvent *)\n{",
                 """void KisPaintOpPresetsEditor::showEvent(QShowEvent *)
{
#ifdef Q_OS_ANDROID
    if (m_d->mobileTabs) {
        const auto fit = [this]() {
            auto *frame = parentWidget();
            if (!frame || !frame->screen()) return;
            const QRect area = frame->screen()->availableGeometry().adjusted(6, 6, -6, -6);
            const QSize size(qMin(540, area.width()), qMin(710, area.height()));
            if (size.isEmpty()) return;
            frame->setFixedSize(size);
            frame->move(area.center() - QPoint(size.width()/2, size.height()/2));
        };
        fit();
        if (parentWidget() && parentWidget()->screen())
            m_d->mobileScreenConnection = connect(parentWidget()->screen(),
                &QScreen::availableGeometryChanged, this, fit);
        Q_EMIT brushEditorShown();
        return;
    }
#endif""", "fit mobile editor to its screen")
    source = one(source, "    QWidget::resizeEvent(event);\n    if (parentWidget()) {",
                 """    QWidget::resizeEvent(event);
#ifdef Q_OS_ANDROID
    if (m_d->mobileTabs) return;
#endif
    if (parentWidget()) {""", "keep desktop popup placement separate")
    for slot in ("slotSwitchScratchpad", "slotSwitchShowPresets"):
        anchor = f"void KisPaintOpPresetsEditor::{slot}(bool visible)\n{{"
        source = one(source, anchor, anchor + """
#ifdef Q_OS_ANDROID
    if (m_d->mobileTabs) return;
#endif""", f"keep mobile tabs independent of {slot}")
    editor.write_text(source, encoding="utf-8")

    settings = root / "libs/ui/kis_paintop_settings_widget.cpp"
    source = settings.read_text(encoding="utf-8")
    source = one(source, "#include <QStackedWidget>",
                 """#include <QStackedWidget>
#ifdef Q_OS_ANDROID
#include <QScrollArea>
#include <QPainter>
#include <QStyleOptionButton>
#include <QPersistentModelIndex>
#include "touch_scroll.h"
#endif""", "mobile brush setting includes")
    source = one(source, "struct KisPaintOpSettingsWidget::Private",
                 """#ifdef Q_OS_ANDROID
class MobileBrushOptionDelegate final : public KisCategorizedItemDelegate
{
public:
    using KisCategorizedItemDelegate::KisCategorizedItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option,const QModelIndex &index) const override
    {
        auto size = KisCategorizedItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(43, size.height()));
        return size;
    }
    void paint(QPainter *painter,const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        if(!(index.flags() & Qt::ItemIsUserCheckable)) {
            KisCategorizedItemDelegate::paint(painter,option,index);
            return;
        }
        painter->save();
        auto *style=QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem,&option,painter);
        QStyleOptionButton checkbox;
        checkbox.rect=QRect(option.rect.left()+6,
                            option.rect.center().y()-13,26,26);
        checkbox.state=QStyle::State_Enabled |
            (index.data(Qt::CheckStateRole).toInt()==Qt::Checked?
                QStyle::State_On:QStyle::State_Off);
        style->drawPrimitive(QStyle::PE_IndicatorCheckBox,&checkbox,painter);
        const QRect caption=option.rect.adjusted(40,0,-6,0);
        painter->setPen(option.state&QStyle::State_Selected?
                        option.palette.highlightedText().color():
                        option.palette.text().color());
        painter->drawText(caption,Qt::AlignVCenter|Qt::AlignLeft,
            option.fontMetrics.elidedText(index.data().toString(),
                                          Qt::ElideRight,caption.width()));
        painter->restore();
    }
};

// The checkbox is a direct action; opening the remainder of the row
// continues to navigate to the native detailed configuration page.
class MobileBrushOptionTapFilter final : public QObject
{
public:
    explicit MobileBrushOptionTapFilter(KisCategorizedListView *view)
        : QObject(view->viewport()),m_view(view) {}
protected:
    bool eventFilter(QObject *,QEvent *event) override
    {
        const auto type=event->type();
        if(type==QEvent::MouseButtonPress) {
            auto *mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()!=Qt::LeftButton)return false;
            const QModelIndex index=m_view->indexAt(mouse->pos());
            const QRect row=m_view->visualRect(index);
            if(!index.isValid() ||
               !(index.flags()&Qt::ItemIsUserCheckable) ||
               mouse->pos().x()>row.left()+40)return false;
            m_pressed=index;
            m_start=mouse->pos();
            return true;
        }
        if(!m_pressed.isValid())return false;
        if(type==QEvent::MouseMove)return true;
        if(type==QEvent::MouseButtonRelease) {
            auto *mouse=static_cast<QMouseEvent*>(event);
            const QModelIndex index=m_view->indexAt(mouse->pos());
            if(index==m_pressed &&
               (mouse->pos()-m_start).manhattanLength()<
                   QApplication::startDragDistance()) {
                const bool checked=index.data(Qt::CheckStateRole).toInt()==Qt::Checked;
                m_view->model()->setData(index,
                    checked?Qt::Unchecked:Qt::Checked,Qt::CheckStateRole);
            }
            m_pressed=QPersistentModelIndex();
            return true;
        }
        return false;
    }
private:
    KisCategorizedListView *m_view=nullptr;
    QPersistentModelIndex m_pressed;
    QPoint m_start;
};
#endif

struct KisPaintOpSettingsWidget::Private""", "give brush options finger-sized rows")
    source = one(source, "    m_d->optionsList->setItemDelegate(new KisCategorizedItemDelegate(m_d->optionsList));",
                 """#ifdef Q_OS_ANDROID
    m_d->optionsList->setItemDelegate(new MobileBrushOptionDelegate(m_d->optionsList));
#else
    m_d->optionsList->setItemDelegate(new KisCategorizedItemDelegate(m_d->optionsList));
#endif""", "select the mobile brush option delegate")
    source = one(source, "    QStackedWidget*             optionsStack;",
                 """    QStackedWidget*             optionsStack;
#ifdef Q_OS_ANDROID
    QStackedWidget *mobilePages = nullptr;
    QPushButton *mobileBack = nullptr;
#endif""", "mobile brush setting navigation state")
    source = one(source, "    QHBoxLayout* layout = new QHBoxLayout(this);",
                 """#ifdef Q_OS_ANDROID
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_d->mobileBack = new QPushButton(tr("‹ Brush options"), this);
    m_d->mobileBack->setMinimumHeight(40);
    m_d->mobileBack->hide();
    layout->addWidget(m_d->mobileBack);
    m_d->mobilePages = new QStackedWidget(this);
    m_d->optionsList->setMinimumWidth(0);
    m_d->optionsList->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    MobileTouchScroll::configureList(m_d->optionsList);
    m_d->optionsList->viewport()->installEventFilter(
        new MobileBrushOptionTapFilter(m_d->optionsList));
    m_d->optionsStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_d->mobilePages->addWidget(m_d->optionsList);
    m_d->mobilePages->addWidget(m_d->optionsStack);
    layout->addWidget(m_d->mobilePages, 1);
    connect(m_d->mobileBack, &QPushButton::clicked, this, [this]() {
        m_d->mobilePages->setCurrentWidget(m_d->optionsList);
        m_d->mobileBack->hide();
    });
#else
    QHBoxLayout* layout = new QHBoxLayout(this);""", "use a full-width native category list")
    source = one(source, "    layout->setStretch(1, 1);",
                 "    layout->setStretch(1, 1);\n#endif", "end desktop brush options layout")
    source = one(source, "    m_d->optionsList->viewport()->installEventFilter(this);",
                 """#ifndef Q_OS_ANDROID
    m_d->optionsList->viewport()->installEventFilter(this);
#endif""", "use mobile touch routing for brush options")
    source = one(source, "    m_d->optionsStack->addWidget(option->configurationPage());",
                 """#ifdef Q_OS_ANDROID
    auto *scroll = new QScrollArea(m_d->optionsStack);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    scroll->setWidget(option->configurationPage());
    MobileTouchScroll::configure(scroll);
    m_d->optionsStack->addWidget(scroll);
#else
    m_d->optionsStack->addWidget(option->configurationPage());
#endif""", "scroll native brush settings on small screens")
    source = one(source, "        m_d->optionsStack->setCurrentIndex(info.index);",
                 """        m_d->optionsStack->setCurrentIndex(info.index);
#ifdef Q_OS_ANDROID
        m_d->mobileBack->show();
        m_d->mobilePages->setCurrentWidget(m_d->optionsStack);
#endif""", "open selected brush setting on mobile")
    settings.write_text(source, encoding="utf-8")


def main(root:Path):
    root=root.resolve()
    install_phone_brush_editor(root)
    own=Path(__file__).resolve().parent
    native_chrome=own/"mobile_core/native_canvas_chrome.h"
    lightweight=own/"mobile_core/lightweight_canvas_host.h"
    native_view_stack=own/"mobile_core/native_view_stack.h"
    native_krita_save_host=own/"mobile_core/native_krita_save_host.h"
    native_krita_factory=own/"mobile_core/krita_native_view_factory.h"
    canvas_resume_observer=own/"mobile_core/canvas_resume_observer.h"
    standalone_krita_runtime=own/"mobile_core/krita_standalone_runtime.h"
    standalone_krita_documents=own/"mobile_core/krita_standalone_documents.h"
    saved_version_archive=own/"mobile_core/saved_version_archive.h"
    open_document_destination_policy=own/"mobile_core/open_document_destination_policy.h"
    standalone_engine_session=own/"mobile_core/krita_engine_session.h"
    standalone_engine_pilot=own/"mobile_core/krita_standalone_pilot.h"
    engine_only_policy=own/"mobile_core/engine_only_startup_policy.h"
    engine_only_bootstrap_header=own/"mobile_core/engine_only_bootstrap.h"
    engine_only_bootstrap_source=own/"mobile_core/engine_only_bootstrap.cpp"
    engine_init_header=own/"mobile_core/krita_engine_initialization.h"
    engine_init_source=own/"mobile_core/krita_engine_initialization.cpp"
    app_entry_header=own/"mobile_core/krita_application_entry.h"
    app_entry_source=own/"mobile_core/krita_application_entry.cpp"
    android_runtime_header=own/"mobile_core/krita_android_runtime.h"
    android_runtime_source=own/"mobile_core/krita_android_runtime.cpp"
    android_intents_header=own/"mobile_core/android_document_intents.h"
    android_intents_source=own/"mobile_core/android_document_intents.cpp"
    native_workspace=own/"mobile_core/krita_native_workspace.h"
    native_reorder_policy=own/"mobile_core/native_layer_reorder_policy.h"
    native_command_layout=own/"mobile_core/native_command_layout.h"
    toolbar_drag_gesture=own/"mobile_core/toolbar_drag_gesture.h"
    deferred_view_command=own/"mobile_core/deferred_view_command.h"
    document_navigation=own/"mobile_core/document_navigation.h"
    switcher=own/"mobile_core/document_switcher.h"
    bridge=own/"mobile_core/engine_bridge.h"
    document_service=own/"mobile_core/document_service.h"
    native_document_rollback=own/"mobile_core/native_document_rollback.h"
    native_document_path_policy=own/"mobile_core/native_document_path_policy.h"
    document_port=own/"mobile_core/document_host_port.h"
    view_port=own/"mobile_core/view_host_port.h"
    view_guard=own/"mobile_core/view_identity_guard.h"
    command_port=own/"mobile_core/command_registry_port.h"
    host_services=own/"mobile_core/host_services.h"
    command_service=own/"mobile_core/command_service.h"
    brush=own/"mobile_core/brush_workspace.h"
    color=own/"mobile_core/color_workspace.h"
    layers=own/"mobile_core/layer_workspace.h"
    commands=own/"mobile_core/command_browser.h"
    pressure_calibration=own/"mobile_core/finger_pressure_calibration.h"
    pressure_settings=own/"mobile_core/finger_pressure_settings.h"
    tools=own/"mobile_core/tool_browser.h"
    documents=own/"mobile_core/document_workflow.h"
    if not all(p.is_file() for p in
               (standalone_krita_runtime,standalone_krita_documents,native_krita_factory)):
        raise RuntimeError("Missing Mobile Studio engine module")

    # The preexisting Android reference-image gesture patch (installed by
    # apply_reference_touch_fix.py earlier in local-build.sh) still assumed
    # that the painting canvas belonged to KisPart::currentMainwindow().
    # Independent Mobile Studio has no KisMainWindow; its native
    # KisInputManager already knows the active KisCanvas2 directly.
    # Reuse the *same* hit-tested touch gestures and undo transactions,
    # replacing only desktop-specific canvas/document lookup.
    reference_input=root/"libs/ui/input/kis_input_manager.cpp"
    reference_source=reference_input.read_text(encoding="utf-8")
    reference_source=one(reference_source,
        """        if (QSettings().value(QStringLiteral("TabletUi/referenceFingerDrag"), true).toBool()
            && !referenceTouch->touchPoints().isEmpty() && KisPart::exists()) {
            KisMainWindow *window = KisPart::instance()->currentMainwindow();
            KisCanvas2 *canvas = window && window->viewManager()
                ? window->viewManager()->canvasBase() : nullptr;
            if (canvas && window->activeView() &&
                canvas->referenceImagesDecoration() &&
                canvas->referenceImagesDecoration()->visible()) {
                KisReferenceImagesLayerSP refs =
                    window->activeView()->document()->referenceImagesLayer();""",
        """        if ((QSettings().value(
                 QStringLiteral("TabletUi/referenceFingerDrag"),true).toBool() ||
             QSettings().value(
                 QStringLiteral("TabletUi/referenceDoubleTapEnabled"),true).toBool() ||
             QSettings().value(
                 QStringLiteral("TabletUi/referenceTwoFinger"),true).toBool()) &&
            !referenceTouch->touchPoints().isEmpty()) {
            KisCanvas2 *canvas = this->canvas();
            KisView *activeView = canvas ? canvas->imageView().data() : nullptr;
            if (canvas && activeView && activeView->document() &&
                canvas->referenceImagesDecoration() &&
                canvas->referenceImagesDecoration()->visible()) {
                KisReferenceImagesLayerSP refs =
                    activeView->document()->referenceImagesLayer();""",
        "route reference-image drag/resize/double-tap through active independent canvas")
    reference_input.write_text(reference_source,encoding="utf-8")

    # Studio widgets use the host-neutral engine and document ports.
    mobile_frontend=(standalone_engine_session,standalone_engine_pilot,engine_only_policy,
                     engine_only_bootstrap_header,engine_only_bootstrap_source,engine_init_header,
                     engine_init_source,app_entry_header,app_entry_source,android_runtime_header,
                     android_runtime_source,android_intents_header,android_intents_source,
                     native_command_layout,native_workspace,native_reorder_policy,canvas_resume_observer,
                     deferred_view_command,document_navigation,bridge,host_services,document_service,
                     native_document_rollback,native_document_path_policy,document_port,view_port,
                     view_guard,command_service,command_port,switcher,brush,color,layers,commands,
                     pressure_calibration,pressure_settings,saved_version_archive,
                      open_document_destination_policy,tools,documents,
                     native_chrome,lightweight,native_view_stack,native_krita_save_host)
    for module in mobile_frontend:
        contents=module.read_text(encoding="utf-8")
        if ('#include "KisMainWindow.h"' in contents or
            '#include <KisMainWindow.h>' in contents or
            'legacyHost()' in contents):
            raise RuntimeError(
                f"Mobile Studio desktop UI dependency leaked into {module.name}")
    # Host-neutral native KisView teardown and close-time Save seams.
    # Changes are Android-only. The desktop Krita path is untouched.
    viewheader=root/"libs/ui/KisView.h"
    vh=viewheader.read_text(encoding="utf-8")
    vh=one(vh,"class KisViewManager;","class KisViewManager;\n#ifdef Q_OS_ANDROID\nclass MobileKritaNativeSaveHost;\n#endif",
           "KisView native save host forward declaration")
    vh=one(vh,"    KisViewManager *viewManager() const;",
           "    KisViewManager *viewManager() const;\n#ifdef Q_OS_ANDROID\n    void setMobileNativeSaveHost(MobileKritaNativeSaveHost *host);\n    MobileKritaNativeSaveHost *mobileNativeSaveHost() const;\n#endif",
           "KisView native save host access")
    viewheader.write_text(vh,encoding="utf-8")

    viewcpp=root/"libs/ui/KisView.cpp"
    vc=viewcpp.read_text(encoding="utf-8")
    vc=one(vc,'#include "KisViewManager.h"',
           '#include "KisViewManager.h"\n#ifdef Q_OS_ANDROID\n#include <KoCanvasSupervisor.h>\n#include "native_krita_save_host.h"\n#include "document_close_prompt.h"\n#endif',
           "KisView native save host include")
    vc=one(vc,'''        , canvasController(_q, viewManager->mainWindow(), viewManager->actionCollection())''',
           '''#ifdef Q_OS_ANDROID
        , canvasController(_q,
              dynamic_cast<KoCanvasSupervisor*>(viewManager->qtMainWindow()),
              viewManager->actionCollection())
#else
        , canvasController(_q, viewManager->mainWindow(), viewManager->actionCollection())
#endif''',
           "KisView Qt-native canvas supervisor")
    vc=one(vc,'    KisViewManager *viewManager {nullptr};',
           '    KisViewManager *viewManager {nullptr};\n#ifdef Q_OS_ANDROID\n    QPointer<MobileKritaNativeSaveHost> mobileNativeSaveHost;\n#endif',
           "KisView guarded native save port")
    vc=one(vc,'        d->viewManager->mainWindow()->notifyChildViewDestroyed(this);',
           '        // An independent QMainWindow has no KisMainWindow hooks.\n        if (KisMainWindow *desktop=d->viewManager->mainWindow())\n            desktop->notifyChildViewDestroyed(this);',
           "KisView destructor desktop notification")
    vc=one(vc,
           '        if (KisMainWindow *desktop=d->viewManager->mainWindow())\n            desktop->notifyChildViewDestroyed(this);',
           '''        if (KisMainWindow *desktop=d->viewManager->mainWindow()) {
            desktop->notifyChildViewDestroyed(this);
        }
#ifdef Q_OS_ANDROID
        else {
            // Match native Krita's tracked canvas removal when a
            // lightweight QWidget owns a KisView instead of MDI.
            d->viewManager->inputManager()->removeTrackedCanvas(&d->canvas);
            if (d->viewManager->canvasBase()==&d->canvas)
                d->viewManager->setCurrentView(nullptr);
        }
#endif''',
           "KisView native input teardown")
    vc=one(vc,'KisMainWindow * KisView::mainWindow() const',
           '''#ifdef Q_OS_ANDROID
void KisView::setMobileNativeSaveHost(MobileKritaNativeSaveHost *host)
{
    d->mobileNativeSaveHost=host;
}
MobileKritaNativeSaveHost *KisView::mobileNativeSaveHost() const
{
    return d->mobileNativeSaveHost.data();
}
#endif

KisMainWindow * KisView::mainWindow() const''',
           "KisView native save host methods")
    vc=one(vc,'''        int res = QMessageBox::warning(this,''',
           '''        int res;
#ifdef Q_OS_ANDROID
        if (!viewManager()->mainWindow())
            res = mobileConfirmDocumentClose(this, name);
        else
#endif
        res = QMessageBox::warning(this,''',
           "KisView mobile close prompt")
    vc=one(vc,'''            if (!viewManager()->mainWindow()->saveDocument(document(), !isNative, false))
                return false;''',
           '''#ifdef Q_OS_ANDROID
            if (!viewManager()->mainWindow()) {
                // Without a live injected host, refuse Save and retain
                // the modified painting. Never silently discard it.
                if (!d->mobileNativeSaveHost ||
                    !d->mobileNativeSaveHost->save(document(), !isNative &&
                        document()->mimeType() != "application/x-krita-archive") ||
                    document()->isModified())
                    return false;
            } else
#endif
            if (!viewManager()->mainWindow()->saveDocument(document(), !isNative, false))
                return false;''',
           "KisView native close Save")
    vc=one(vc,'''        case QMessageBox::No : {
            KisImageSP image = document()->image();''',
           '''        case QMessageBox::No : {
#ifdef Q_OS_ANDROID
            if (!viewManager()->mainWindow())
                document()->waitForSavingToComplete();
#endif
            KisImageSP image = document()->image();''',
           "KisView wait for pending checkpoint before discard")
    vc=one(vc,'''void KisView::dropEvent(QDropEvent *event)
{
    dbgUI << Q_FUNC_INFO;''',
           '''void KisView::dropEvent(QDropEvent *event)
{
#ifdef Q_OS_ANDROID
    if (!mainWindow()) {
        // Desktop drop operations directly manipulate KisMainWindow.
        // Refuse those operations in the independent host until they
        // route through its document/service ports.
        event->ignore();
        return;
    }
#endif
    dbgUI << Q_FUNC_INFO;''',
           "KisView guarded native drop")
    vc=one(vc,'''void KisView::closeView()
{
    d->subWindow->close();
}''',
           '''void KisView::closeView()
{
#ifdef Q_OS_ANDROID
    if (!d->subWindow) {
        // Native stack does not create desktop QMdiSubWindow wrappers.
        // The view still runs its own Save/Discard/Cancel closeEvent.
        if (close()) deleteLater();
        return;
    }
#endif
    d->subWindow->close();
}''',
           "KisView native close action")
    vc=one(vc,'''    config.setProperty("window", mainWindow()->windowStateConfig().name());''',
           '''#ifdef Q_OS_ANDROID
    if (KisMainWindow *desktop=mainWindow())
        config.setProperty("window", desktop->windowStateConfig().name());
#else
    config.setProperty("window", mainWindow()->windowStateConfig().name());
#endif''',
           "KisView standalone view state")
    viewcpp.write_text(vc,encoding="utf-8")

    partcpp=root/"libs/ui/KisPart.cpp"
    pc=partcpp.read_text(encoding="utf-8")
    pc=one(pc,'#include "KisView.h"',
           '#include "KisView.h"\n#ifdef Q_OS_ANDROID\n#include "native_krita_save_host.h"\n#endif',
           "KisPart native host include")
    pc=one(pc,'    KIS_ASSERT_RECOVER_RETURN(!view->mainWindow()->hackIsSaving());',
           '''    if (KisMainWindow *desktop=view->mainWindow()) {
        KIS_ASSERT_RECOVER_RETURN(!desktop->hackIsSaving());
    }
#ifdef Q_OS_ANDROID
    else if (auto *host=view->mobileNativeSaveHost()) {
        KIS_ASSERT_RECOVER_RETURN(!host->saving());
    }
#endif''',
           "KisPart host-neutral view removal guard")
    partcpp.write_text(pc,encoding="utf-8")

    managercpp=root/"libs/ui/KisViewManager.cpp"
    mc=managercpp.read_text(encoding="utf-8")
    mc=one(mc,'''QWidget *KisViewManager::mainWindowAsQWidget() const
{
    return mainWindow();
}''',
           '''QWidget *KisViewManager::mainWindowAsQWidget() const
{
    // The view manager already supports a plain QMainWindow host.
    // Do not lose the dialog parent by forcing KisMainWindow here.
    return qtMainWindow();
}''',
           "Qt-native lightweight dialog parent")
    mc=one(mc,'''int KisViewManager::viewCount() const
{
    KisMainWindow *mw  = qobject_cast<KisMainWindow*>(d->mainWindow);
    if (mw) {
        return mw->viewCount();
    }
    return 0;
}''',
           '''int KisViewManager::viewCount() const
{
    KisMainWindow *mw  = qobject_cast<KisMainWindow*>(d->mainWindow);
    if (mw) {
        return mw->viewCount();
    }
#ifdef Q_OS_ANDROID
    // A native Qt window has no MDI; count only registered Krita
    // engine views belonging to this specific view manager.
    int count=0;
    if (KisPart::instance()) {
        for (const QPointer<KisView> &candidate : KisPart::instance()->views()) {
            if (candidate && candidate->viewManager()==this)
                ++count;
        }
    }
    return count;
#else
    return 0;
#endif
}''',
           "Krita native view count")
    mc=one(mc,
           '    bool toggled = actionCollection()->action("view_show_canvas_only")->isChecked();',
           """    if (!mainWindow()) {
        d->currentImageView->canvasController()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        d->currentImageView->canvasController()->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        return;
    }
    bool toggled = actionCollection()->action("view_show_canvas_only")->isChecked();""",
           "Hide canvas scrollbars in Studio")
    # Keep diagnostic checkpoints inside KisViewManager as well as the
    # launcher. A crash in any of these initialization stages should not
    # require Android logcat to distinguish from the earlier null statusbar.
    mc=one(mc,'#include <QPrinter>',
           '#include <QPrinter>\n#ifdef Q_OS_ANDROID\n#include <QSettings>\n#endif',
           "KisViewManager Android diagnostic include")
    mc=one(mc,
           '''    createActions();
    setupManagers();''',
           '''#ifdef Q_OS_ANDROID
    auto mobileInitStage=[this](const char *name) {
        if (mainWindow()) return; // Desktop engine is unchanged.
        QSettings settings;
        settings.setValue(QStringLiteral("MobileStudio/standaloneLastPhase"),
                          QString::fromLatin1(name));
        settings.sync();
    };
    mobileInitStage("KisViewManager: creating actions");
#endif
    createActions();
#ifdef Q_OS_ANDROID
    mobileInitStage("KisViewManager: setting up managers");
#endif
    setupManagers();''',
           "KisViewManager isolated creation checkpoints")
    mc=one(mc,'    d->statusBar.setup();',
           '''#ifdef Q_OS_ANDROID
    mobileInitStage("KisViewManager: setting up status bar");
#endif
    d->statusBar.setup();''',
           "KisViewManager statusbar checkpoint")
    mc=one(mc,'    d->controlFrame.setup(parent);',
           '''#ifdef Q_OS_ANDROID
    mobileInitStage("KisViewManager: setting up brush controls");
#endif
    d->controlFrame.setup(parent);''',
           "KisViewManager brush control checkpoint")
    mc=one(mc,'    KoToolManager::instance()->initializeToolActions();',
           '''#ifdef Q_OS_ANDROID
    mobileInitStage("KisViewManager: initializing tool actions");
#endif
    KoToolManager::instance()->initializeToolActions();''',
           "KisViewManager tool action checkpoint")
    mc=one(mc,'    KisInputProfileManager::instance()->loadProfiles();',
           '''#ifdef Q_OS_ANDROID
    mobileInitStage("KisViewManager: loading input profiles");
#endif
    KisInputProfileManager::instance()->loadProfiles();''',
           "KisViewManager input profile checkpoint")
    mc=one(mc,'    new ImageSize(this);',
           '''    new ImageSize(this);
#ifdef Q_OS_ANDROID
    mobileInitStage("KisViewManager: constructor complete");
#endif''',
           "KisViewManager finished checkpoint")
    managercpp.write_text(mc,encoding="utf-8")

    # KisStatusBar::setup() runs inside KisViewManager's constructor.
    # It dereferences KisMainWindow, which is intentionally absent in
    # our independent QMainWindow host. Android uses the Qt-native
    # host's own status bar; desktop Krita keeps its original code.
    statusbarfile=root/"libs/ui/kis_statusbar.cc"
    sb=statusbarfile.read_text(encoding="utf-8")
    sb=one(sb,'#include <QStatusBar>',
           '#include <QStatusBar>\n#ifdef Q_OS_ANDROID\n#include <QMainWindow>\n#include <QDebug>\n#endif',
           "KisStatusBar Qt host includes")
    sb=one(sb,
           '    m_statusBar = m_viewManager->mainWindow()->statusBar();',
           '''#ifdef Q_OS_ANDROID
    // mainWindow() returns a KisMainWindow* and is null for the
    // independent native canvas. qtMainWindow() retains QMainWindow.
    QMainWindow *qtHost=m_viewManager->qtMainWindow();
    if (!qtHost) {
        qWarning() << "Krita native status bar: no Qt window";
        return;
    }
    m_statusBar = qtHost->statusBar();
#else
    m_statusBar = m_viewManager->mainWindow()->statusBar();
#endif''',
           "KisStatusBar native Qt host")
    sb=one(sb,
           '    connect(m_viewManager->mainWindow(), SIGNAL(themeChanged()), this, SLOT(updateSelectionIcon()));',
           '''#ifdef Q_OS_ANDROID
    // themeChanged is a desktop KisMainWindow signal, not a
    // QMainWindow signal. Do not connect to a null desktop host.
    if (m_viewManager->mainWindow())
        connect(m_viewManager->mainWindow(), SIGNAL(themeChanged()), this, SLOT(updateSelectionIcon()));
#else
    connect(m_viewManager->mainWindow(), SIGNAL(themeChanged()), this, SLOT(updateSelectionIcon()));
#endif''',
           "KisStatusBar desktop-only signal guard")
    statusbarfile.write_text(sb,encoding="utf-8")

    # Krita 6.0.4 canvas uses a desktop window for color-management
    # operations, although its manager already stores QMainWindow.
    canvasfile=root/"libs/ui/canvas/kis_canvas2.cpp"
    canvas=canvasfile.read_text(encoding="utf-8")
    canvas=one(canvas,
               '    connect(mainWindow, SIGNAL(guiLoadingFinished()), SLOT(bootstrapFinished()));',
               '''    if (mainWindow)
        connect(mainWindow, SIGNAL(guiLoadingFinished()), SLOT(bootstrapFinished()));''',
               "KisCanvas2 independent host deferred bootstrap")
    canvas=one(canvas,
               'QWindow *mainWindowNativeWindow = m_d->view->mainWindow()->windowHandle();',
               '''QWidget *nativeHost=m_d->view->viewManager()->
            mainWindowAsQWidget();
        QWindow *mainWindowNativeWindow=nativeHost?
            nativeHost->windowHandle():nullptr;''',
               "native canvas color-managed surface parent")
    canvas=one(canvas,
               'QWidget *mainWindow = m_d->view->mainWindow();',
               '''QWidget *mainWindow = m_d->view->viewManager()->
        mainWindowAsQWidget();''',
               "native canvas config parent")
    canvasfile.write_text(canvas,encoding="utf-8")

    # EXPERIMENTAL engine-only startup: after KisApplication has
    # initialized its global engines, brush resources, plugin registries
    # and KisPart, it can create a plain Android QMainWindow instead of
    # calling KisPart::startBlankSession() -> KisMainWindow.
    # The existing V1 desktop-backed native startup is unchanged, and
    # engine-only mode requires a separate explicit opt-in.
    appfile=root/"libs/ui/KisApplication.cpp"
    app=appfile.read_text(encoding="utf-8")
    app=one(app,
        '''#include "KisAndroidDonations.h"''',
        '''#include "KisAndroidDonations.h"
#include "krita_engine_initialization.h"
#include "krita_application_entry.h"
#include "android_document_intents.h"''',
        "Android engine-only application bootstrap headers")
    # Preserve Android display-density scaling for the independent
    # process startup. Its singleton pointer lives on KisApplication;
    # expose only the small initialization operation to the new entry.
    appheader=root/"libs/ui/KisApplication.h"
    ah=appheader.read_text(encoding="utf-8")
    ah=one(ah,"class KisApplicationArguments;",
           "class KisApplicationArguments;\nclass KisConfig;",
           "Android scaling config forward declaration")
    ah=one(ah,
        "    KisAndroidScaling *androidScaling();",
        "    KisAndroidScaling *androidScaling();\n    void initializeAndroidScaling(KisConfig &cfg);",
        "Android scaling bootstrap API")
    appheader.write_text(ah,encoding="utf-8")
    scaling_anchor='''    d->androidScaling = new KisAndroidScaling(cfg, this);'''
    app=one(app,scaling_anchor,
        "    initializeAndroidScaling(cfg);",
        "use shared Android scaling init")
    marker='''KisAndroidScaling *KisApplication::androidScaling()'''
    if app.count(marker)!=1:
        raise RuntimeError("Missing Android scaling getter")
    start=app.index(marker)
    brace=app.index("{",start)
    depth=0
    end=brace
    while end<len(app):
        if app[end]=="{": depth+=1
        elif app[end]=="}":
            depth-=1
            if depth==0: break
        end+=1
    if depth!=0: raise RuntimeError("Unbalanced Android scaling getter")
    new_scaling='''

void KisApplication::initializeAndroidScaling(KisConfig &cfg)
{
#if KRITA_QT_HAS_ANDROID_QPLATFORMSCREEN_DENSITY_ADJUSTMENT
    if (!d->androidScaling)
        d->androidScaling = new KisAndroidScaling(cfg, this);
#else
    Q_UNUSED(cfg);
#endif
}
'''
    app=app[:end+1]+new_scaling+app[end+1:]
    # Engine registries are now compiled in their own Android module,
    # not as methods implemented by the desktop application's class.
    app=redirect_android_engine_method(
        app,"void","addResourceTypes",
        "    MobileKritaEngineInitialization::addResourceTypes();")
    app=redirect_android_engine_method(
        app,"void","loadPlugins",
        "    MobileKritaEngineInitialization::loadPlugins();")
    app=redirect_android_engine_method(
        app,"bool","registerResources",
        "    const bool ready=MobileKritaEngineInitialization::registerResources();\n"
        "    connect(KisResourceLocator::instance(), SIGNAL(progressMessage(const QString&)),\n"
        "            this, SLOT(setSplashScreenLoadingText(const QString&)));\n"
        "    return ready;")
    app=one(app,
        '''    KisPart *kisPart = KisPart::instance();
    if (needsMainWindow) {''',
        '''    KisPart *kisPart = KisPart::instance();
#ifdef Q_OS_ANDROID
    // Native Android startup is owned by the application entry module.
    const bool engineOnlyBoot=MobileKritaApplicationEntry::start();
#else
    const bool engineOnlyBoot=false;
#endif
    if (needsMainWindow && !engineOnlyBoot) {''',
        "replace desktop KisPart session bootstrap when engine-only selected")
    app=one(app,
        '''    if (!d->batchRun) {
        checkAutosaveFiles();
    }''',
        '''    if (!d->batchRun && !engineOnlyBoot) {
        checkAutosaveFiles();
    }''',
        "defer desktop autosave manager while engine-only")
    # A native Android document intent might be queued after the
    # eligibility check. Never dereference a null desktop mainWindow.
    app=one(app,
        '''            d->mainWindow->openDocument(fileName, QFlags<KisMainWindow::OpenFlag>());''',
        '''            if (d->mainWindow)
                d->mainWindow->openDocument(
                    fileName,QFlags<KisMainWindow::OpenFlag>());
            else
                qWarning() << "Engine-only file intent requires native"
                           << " routing; reopen this file through Open:" << fileName;''',
        "guard late Android open intent without desktop window")
    # Android's MainActivity forwards onNewIntent() through
    # KisApplication::fileOpenRequested. Send it directly to the native
    # Krita document host rather than an absent desktop main window.
    app=one(app,
        '''void KisApplication::fileOpenRequested(const QString &url)
{''',
        '''void KisApplication::fileOpenRequested(const QString &url)
{
#ifdef Q_OS_ANDROID
    if (MobileAndroidDocumentIntents::enqueue(url))
        return;
#endif''',
        "route Android file open to native engine")
    # The existing QtSingleApplication signal delivers files to an
    # already-running instance through remoteArguments().
    app=one(app,
        '''void KisApplication::remoteArguments(const QString &message)
{''',
        '''void KisApplication::remoteArguments(const QString &message)
{
#ifdef Q_OS_ANDROID
    if (MobileAndroidDocumentIntents::active()) {
        QByteArray serialized=QByteArray::fromBase64(
            message.toLatin1());
        const KisApplicationArguments incoming=
            KisApplicationArguments::deserialize(serialized);
        if(incoming.filenames().isEmpty())
            qWarning() << "Mobile Studio: remote request has no file paths";
        for(const QString &path:incoming.filenames())
            MobileAndroidDocumentIntents::enqueue(path);
        return;
    }
#endif''',
        "route remote file launch to native engine")
    app=one(app,
        '''    Q_FOREACH(const QByteArray &message, d->earlyRemoteArguments) {
        executeRemoteArguments(message, d->mainWindow);
    }''',
        '''    Q_FOREACH(const QByteArray &message, d->earlyRemoteArguments) {
        if (d->mainWindow)
            executeRemoteArguments(message,d->mainWindow);
        else
            qWarning() << "Engine-only remote intent requires native"
                       << " routing; use the independent Open command";
    }''',
        "guard late remote document intent without desktop window")
    appfile.write_text(app,encoding="utf-8")

    # Studio starts directly in its own host. Leave Krita's main-window
    # constructor and compact toolbar free of the obsolete embedded pilot.
    filter_file=root/"libs/ui/kis_filter_manager.cc"
    filters=filter_file.read_text(encoding="utf-8")
    filters=one(filters,
        "    if (!d->view->activeNode()->isEditable()) {",
        """    const KisNodeSP node = d->view->activeNode();
    if (!node || !d->view->image()) return;
    if (!node->isEditable()) {""",
        "guard native filters without an active drawing")
    filters=filters.replace("d->view->mainWindow()", "d->view->mainWindowAsQWidget()")
    filter_file.write_text(filters,encoding="utf-8")

    (root/"libs/ui/native_canvas_chrome.h").write_text(
        native_chrome.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/lightweight_canvas_host.h").write_text(
        lightweight.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_view_stack.h").write_text(
        native_view_stack.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_krita_save_host.h").write_text(
        native_krita_save_host.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_native_view_factory.h").write_text(
        native_krita_factory.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/canvas_resume_observer.h").write_text(
        canvas_resume_observer.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_standalone_runtime.h").write_text(
        standalone_krita_runtime.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_standalone_documents.h").write_text(
        standalone_krita_documents.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/saved_version_archive.h").write_text(
        saved_version_archive.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/open_document_destination_policy.h").write_text(
        open_document_destination_policy.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_engine_session.h").write_text(
        standalone_engine_session.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_standalone_pilot.h").write_text(
        standalone_engine_pilot.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/engine_only_startup_policy.h").write_text(
        engine_only_policy.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/engine_only_bootstrap.h").write_text(
        engine_only_bootstrap_header.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/engine_only_bootstrap.cpp").write_text(
        engine_only_bootstrap_source.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_engine_initialization.h").write_text(
        engine_init_header.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_engine_initialization.cpp").write_text(
        engine_init_source.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_application_entry.h").write_text(
        app_entry_header.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_application_entry.cpp").write_text(
        app_entry_source.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_android_runtime.h").write_text(
        android_runtime_header.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_android_runtime.cpp").write_text(
        android_runtime_source.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/android_document_intents.h").write_text(
        android_intents_header.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/android_document_intents.cpp").write_text(
        android_intents_source.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/krita_native_workspace.h").write_text(
        native_workspace.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_layer_reorder_policy.h").write_text(
        native_reorder_policy.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_command_layout.h").write_text(
        native_command_layout.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/toolbar_drag_gesture.h").write_text(
        toolbar_drag_gesture.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/canvas_layout_editor.h").write_text(
        (own/"mobile_core/canvas_layout_editor.h").read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/context_tool_bar.h").write_text(
        (own/"mobile_core/context_tool_bar.h").read_text(encoding="utf-8"),
        encoding="utf-8")
    (root/"libs/ui/deferred_view_command.h").write_text(
        deferred_view_command.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_navigation.h").write_text(
        document_navigation.read_text(encoding="utf-8"),encoding="utf-8")
    # Compile the live Studio entry and its engine into kritaui on Android.
    ui_cmake_file=root/"libs/ui/CMakeLists.txt"
    ui_cmake=ui_cmake_file.read_text(encoding="utf-8")
    ui_cmake=one(ui_cmake,
        '''if(ANDROID)
    list(APPEND kritaui_LIB_SRCS
        KisAndroidDonations.cpp''',
        '''if(ANDROID)
    list(APPEND kritaui_LIB_SRCS
        engine_only_bootstrap.cpp
        krita_engine_initialization.cpp
        krita_application_entry.cpp
        krita_android_runtime.cpp
        android_document_intents.cpp
        KisAndroidDonations.cpp''',
        "real Android kritaui engine compilation gate")
    ui_cmake_file.write_text(ui_cmake,encoding="utf-8")
    (root/"libs/ui/document_switcher.h").write_text(
        switcher.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_close_prompt.h").write_text(
        (own/"mobile_core/document_close_prompt.h").read_text(encoding="utf-8"),
        encoding="utf-8")
    (root/"libs/ui/project_preview_cache.h").write_text(
        (own/"mobile_core/project_preview_cache.h").read_text(encoding="utf-8"),
        encoding="utf-8")
    (root/"libs/ui/document_home.h").write_text(
        (own/"mobile_core/document_home.h").read_text(encoding="utf-8"),
        encoding="utf-8")
    (root/"libs/ui/mobile_draft_recovery.h").write_text(
        (own/"mobile_core/mobile_draft_recovery.h").read_text(encoding="utf-8"),
        encoding="utf-8")
    (root/"libs/ui/native_layer_identity.h").write_text(
        (own/"mobile_core/native_layer_identity.h").read_text(encoding="utf-8"),
        encoding="utf-8")
    (root/"libs/ui/engine_bridge.h").write_text(
        bridge.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_service.h").write_text(
        document_service.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_document_rollback.h").write_text(
        native_document_rollback.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/native_document_path_policy.h").write_text(
        native_document_path_policy.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_host_port.h").write_text(
        document_port.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/view_host_port.h").write_text(
        view_port.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/view_identity_guard.h").write_text(
        view_guard.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/command_registry_port.h").write_text(
        command_port.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/host_services.h").write_text(
        host_services.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/command_service.h").write_text(
        command_service.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/brush_workspace.h").write_text(
        brush.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/color_workspace.h").write_text(
        color.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/overlay_placement.h").write_text(
        (own/"mobile_core/overlay_placement.h").read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/layer_rail_geometry.h").write_text(
        (own/"mobile_core/layer_rail_geometry.h").read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/layer_workspace.h").write_text(
        layers.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/command_browser.h").write_text(
        commands.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/finger_pressure_calibration.h").write_text(
        pressure_calibration.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/finger_pressure_settings.h").write_text(
        pressure_settings.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/touch_action_picker.h").write_text(
        (own/"mobile_core/touch_action_picker.h").read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/touch_scroll.h").write_text(
        (own/"mobile_core/touch_scroll.h").read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/workspace_profiles.h").write_text(
        (own/"mobile_core/workspace_profiles.h").read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/recent_colors.h").write_text(
        (own/"mobile_core/recent_colors.h").read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/transient_panel.h").write_text(
        (own/"mobile_core/transient_panel.h").read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/tool_browser.h").write_text(
        tools.read_text(encoding="utf-8"),encoding="utf-8")
    (root/"libs/ui/document_workflow.h").write_text(
        documents.read_text(encoding="utf-8"),encoding="utf-8")

    xmlfile=root/"krita/krita5.xmlgui"
    xml=xmlfile.read_text(encoding="utf-8")
    xml=one(xml,
        '    <Action name="tablet_compact_ui"/>',
        '''    <Action name="tablet_compact_ui"/>
    <Action name="tablet_mobile_engine_pilot"/>''',
        "native toolbar action")
    # The normal toolbar can be hidden by a saved Krita workspace.
    # Keep Mobile Studio directly recoverable in Settings after a
    # previous failed/disabled automatic launch.
    xml=one(xml,
        '    <Menu name="settings">\n      <text>Setti&amp;ngs</text>',
        '''    <Menu name="settings">
      <text>Setti&amp;ngs</text>
      <Action name="tablet_mobile_engine_pilot"/>
      <Action name="tablet_standalone_engine_test"/>
      <Action name="tablet_standalone_engine_status"/>
      <Action name="tablet_native_engine_autostart"/>
      <Separator/>''',
        "Settings menu mobile recovery launcher")
    xmlfile.write_text(xml,encoding="utf-8")
    cmakefile=root/"krita/CMakeLists.txt"
    cmake=cmakefile.read_text(encoding="utf-8")
    cmake=one(cmake,
        'set(KRITA5_XMLGUI_VERSION 548)',
        'set(KRITA5_XMLGUI_VERSION 553)',
        "XMLGUI version")
    oldhash=re.findall(r'set\(krita5_xmlgui_hash_expected "[0-9a-f]+"\)',cmake)
    if len(oldhash)!=1:
        raise RuntimeError(f"XMLGUI checksum anchor: {len(oldhash)} matches")
    digest=hashlib.md5(''.join(xml.split()).encode()).hexdigest()
    cmake=cmake.replace(oldhash[0],
        f'set(krita5_xmlgui_hash_expected "{digest}")',1)
    # Android gets its own real native application target and entry file.
    # KDE's Android deploy tool names the entry JNI shared library using
    # the native CMake target. Keep target, library, manifest and APK
    # packaging folder all aligned to mobile_studio. The Java package
    # namespace stays org.krita for the existing JNI bridge.
    cmake=one(cmake,
        'set(krita_SRCS main.cc)',
        '''if(ANDROID AND QT_MAJOR_VERSION STREQUAL "5")
    set(krita_SRCS mobile_studio_main.cc)
    set(KRITA_APPLICATION_TARGET mobile_studio)
    set(QTANDROID_EXPORTED_TARGET mobile_studio CACHE STRING
        "Android native application target" FORCE)
    # Parent CMake creates create-apk after add_subdirectory(krita).
    # CACHE FORCE does not replace an inherited normal-scope variable.
    set(QTANDROID_EXPORTED_TARGET mobile_studio PARENT_SCOPE)
else()
    set(krita_SRCS main.cc)
    set(KRITA_APPLICATION_TARGET krita)
endif()''',
        "Native Android entrypoint source target")
    cmake=one(cmake,
        '''if (ANDROID AND QT_MAJOR_VERSION STREQUAL "5")
    kis_add_library(krita SHARED ${krita_SRCS})
    target_link_libraries(krita PRIVATE Qt5::AndroidExtras)
elseif (WIN32)''',
        '''if (ANDROID AND QT_MAJOR_VERSION STREQUAL "5")
    kis_add_library(mobile_studio SHARED ${krita_SRCS})
    target_link_libraries(mobile_studio PRIVATE Qt5::AndroidExtras)
elseif (WIN32)''',
        "Native Mobile Studio Android build target")
    cmake=one(cmake,
        '''target_link_libraries(krita
    PRIVATE''',
        '''target_link_libraries(${KRITA_APPLICATION_TARGET}
    PRIVATE''',
        "Link real native application target")
    cmake=one(cmake,
        '''    install(TARGETS krita ${INSTALL_TARGETS_DEFAULT_ARGS})
endif()
install(FILES org.kde.krita.desktop''',
        '''    install(TARGETS ${KRITA_APPLICATION_TARGET} ${INSTALL_TARGETS_DEFAULT_ARGS})
endif()
install(FILES org.kde.krita.desktop''',
        "Install native Mobile Studio artifact")
    cmakefile.write_text(cmake,encoding="utf-8")
    # The Android process now enters Mobile Studio's own startup
    # routine directly from main(), skipping KisApplication::start on
    # ordinary launches. Krita's Qt event dispatcher remains in place.
    mainfile=root/"krita/main.cc"
    entry=mainfile.read_text(encoding="utf-8")
    entry=one(entry,
        '#include <KisApplication.h>',
        '''#include <KisApplication.h>
#include <krita_android_runtime.h>''',
        "Mobile-only process bootstrap include")
    entry=one(entry,
        '''    if (!app.start(args)) {
        KisUsageLogger::log("Could not start Krita Application");
        return 1;
    }''',
        '''    // Compiled only by the native mobile_studio Android target.
    // Never enter the original desktop session or recovery path.
    if (MobileKritaAndroidRuntime::launch(app,args) !=
        MobileKritaAndroidRuntime::Result::Started) {
        KisUsageLogger::log("Could not start Mobile Studio");
        return 1;
    }''',
        "Mobile-only native entry without desktop fallback")
    # The original desktop main.cc remains untouched; Android's native
    # library compiles an independent app-specific translation unit.
    (root/"krita/mobile_studio_main.cc").write_text(entry,encoding="utf-8")

    # The original KDE packaging script hardcodes the default CMake
    # export directory "krita_build_apk". Our actual native app target
    # creates "mobile_studio_build_apk" instead.
    packagefile=root/"build-tools/ci-scripts/build-android-package.py"
    package=packagefile.read_text(encoding="utf-8")
    package=one(package,
        "repackagePath = os.path.join(buildPath, 'krita_build_apk')",
        "repackagePath = os.path.join(buildPath, 'mobile_studio_build_apk')",
        "Use independent Android deploy/Gradle directory")
    packagefile.write_text(package,encoding="utf-8")

    # Package Mobile Studio as an independently installable Android app.
    # Keep Krita's Qt/Java namespace; changing only applicationId avoids
    # rewriting the JNI bindings and Java activity class paths.
    gradlefile=root/"packaging/android/apk/build.gradle"
    gradle=gradlefile.read_text(encoding="utf-8")
    gradle=one(gradle,
        '    exclude "mlt-7/**"\n',
        '    exclude "mlt-7/**", "libkrita_${abi}.so", '
        '"libgettextlib*.so", "libgettextsrc*.so"\n'
        '    // The versioned OpenEXR libraries provide the runtime SONAMEs.\n'
        '    exclude "libOpenEXR.so", "libOpenEXRCore.so", "libOpenEXRUtil.so",\n'
        '            "libIex.so", "libIlmThread.so", "libImath.so"\n',
        "Skip obsolete or redundant native libraries")
    gradle=one(gradle,
        '''    from "$installPrefix/share/"
    into 'assets/'
    include '**'
''',
        '''    from "$installPrefix/share/"
    into 'assets/'
    include '**'
    // Host-side tool data and manuals are unused by the Android app.
    exclude 'ECM/**', 'aclocal/**', 'gettext/**', 'gettext-*/**',
            'info/**', 'man/**', 'locale/**/LC_MESSAGES/gettext-*.mo'
    def dependencyDocs = ['expat', 'gettext', 'libasprintf', 'libjpeg-turbo',
                          'libtextstyle', 'ogg', 'OpenEXR', 'tiff', 'zlib']
    exclude { entry ->
        def parts = entry.relativePath.segments
        if (entry.directory || parts.length < 3 || parts[0] != 'doc' ||
            !dependencyDocs.contains(parts[1])) {
            return false
        }
        def name = entry.name.toLowerCase(java.util.Locale.ROOT)
        return !['license', 'licence', 'copying', 'copyright', 'notice', 'readme']
            .any { name.contains(it) }
    }
''',
        "Skip bundled host build tools and third-party manuals")
    gradle=one(gradle,
        '''    from "$installPrefix/translations"
    into 'assets/locale/'
    include '**'
''',
        '''    from "$installPrefix/translations"
    into 'assets/locale/'
    include '**'
    exclude '**/LC_MESSAGES/gettext-*.mo'
''',
        "Skip command-line gettext catalogs in the locale copy")
    gradle=one(gradle,
        '    defaultConfig {\n        compileSdk androidCompileSdkVersion.toInteger()',
        '    defaultConfig {\n        applicationId "org.krita.mobilestudio"\n'
        '        compileSdk androidCompileSdkVersion.toInteger()',
        "Mobile Studio app identifier")
    gradlefile.write_text(gradle,encoding="utf-8")

    manifestfile=root/"packaging/android/apk/AndroidManifest.xml"
    manifest=manifestfile.read_text(encoding="utf-8")
    manifest=one(manifest,
        'android:label="Krita"\n                 android:icon=',
        'android:label="Krita Mobile Studio"\n                 android:icon=',
        "Mobile Studio application label")
    manifest=one(manifest,
        'android:value="krita"/>',
        'android:value="mobile_studio"/>',
        "Qt Android loads independent entrypoint shared library")
    manifest=one(manifest,
        'android:label="Krita"\n                  android:screenOrientation=',
        'android:label="Krita Mobile Studio"\n                  android:screenOrientation=',
        "Mobile Studio activity label")
    manifest=one(manifest,
        'android:name=".android.DocumentSaverService"',
        'android:name="org.krita.android.DocumentSaverService"',
        "fully qualified save service")
    # Android ACTION_VIEW/EDIT uses Intent.getData(); sharing through
    # ACTION_SEND uses EXTRA_STREAM. Enable both without new desktop UI.
    manifest=one(manifest,
        '''            <intent-filter>
                <action android:name="com.samsung.android.support.REMOTE_ACTION" />''',
        '''            <intent-filter>
                <action android:name="android.intent.action.SEND" />
                <category android:name="android.intent.category.DEFAULT" />
                <data android:mimeType="image/*" />
                <data android:mimeType="application/x-krita" />
                <data android:mimeType="application/octet-stream" />
            </intent-filter>
            <intent-filter>
                <action android:name="com.samsung.android.support.REMOTE_ACTION" />''',
        "Android Share single-document intent filter")
    manifestfile.write_text(manifest,encoding="utf-8")

    # The older quick-pinch recognizer still runs in Krita's input manager,
    # but its view-reset callback only searched for a desktop KisMainWindow.
    # Keep the exact gesture thresholds and native desktop behavior, while
    # routing reset to the standalone view manager when no desktop window
    # exists. Input events remain owned by Krita's existing recognizer.
    pinchfile=root/"libs/ui/input/kis_input_manager.cpp"
    pinch_source=pinchfile.read_text(encoding="utf-8")
    pinch_source=one(pinch_source,
        "#include <KisMainWindow.h>\n",
        '#include <KisMainWindow.h>\n#include "krita_android_runtime.h"\n',
        "standalone quick pinch include")
    pinch_source=one(pinch_source,
        """                KisMainWindow *window = KisPart::instance()->currentMainwindow();
                if (!window) return;
                QAction *rotation = window->actionCollection()->action(""",
        """                KisMainWindow *window = KisPart::instance()->currentMainwindow();
                if (!window) {
                    MobileKritaAndroidRuntime::resetCanvasView();
                    return;
                }
                QAction *rotation = window->actionCollection()->action(""",
        "standalone quick pinch reset")
    # Gesture preferences override touch bindings only; mouse, stylus and
    # keyboard shortcuts continue to come from Krita's active profile.
    migration_start=pinch_source.index(
        '#ifdef Q_OS_ANDROID\n        QSettings tabletSettings;',
        pinch_source.index("void KisInputManager::profileChanged()"))
    migration_end=pinch_source.index("        const QList<KisShortcutConfiguration*> shortcuts", migration_start)
    pinch_source=pinch_source[:migration_start]+"""#ifdef Q_OS_ANDROID
        QSettings touchSettings;
        touchSettings.beginGroup(QStringLiteral("MobileStudio/touchGestures"));
        bool profileHasDoubleTap = false;
        bool profileHasCustomTwoFingerTap = false;
#endif
"""+pinch_source[migration_end:]
    pinch_source=one(pinch_source,
        """            case KisShortcutConfiguration::GestureType:
                if (!d->addNativeGestureShortcut""",
        """            case KisShortcutConfiguration::GestureType:
#ifdef Q_OS_ANDROID
                if (touchSettings.contains(QString::number(shortcut->gesture()))) break;
                if (shortcut->gesture()==KisShortcutConfiguration::OneFingerTap &&
                    shortcut->action()->id()==QStringLiteral("Show Popup Widget")) break;
                if (shortcut->gesture()==KisShortcutConfiguration::TwoFingerTap) {
                    if (shortcut->action()->id()==QStringLiteral("Touch Gestures") &&
                        shortcut->mode()==KisTouchGestureAction::UndoActionShortcut) break;
                    profileHasCustomTwoFingerTap=true;
                }
                profileHasDoubleTap |= shortcut->gesture() == KisShortcutConfiguration::OneFingerDoubleTap;
#endif
                if (!d->addNativeGestureShortcut""","apply Studio overrides to gesture bindings only")
    profile_start=pinch_source.index("void KisInputManager::profileChanged()")
    profile_end=pinch_source.index('    }\n    else {\n        dbgInput << "No Input Profile Found',profile_start)
    pinch_source=pinch_source[:profile_end]+"""#ifdef Q_OS_ANDROID
        const auto actions = KisInputProfileManager::instance()->actions();
        for (int index = 1; index < KisShortcutConfiguration::MaxGesture; ++index) {
            const auto gesture = static_cast<KisShortcutConfiguration::GestureAction>(index);
            const QString key = QString::number(index);
            QStringList binding = touchSettings.value(key).toStringList();
            if (!touchSettings.contains(key) &&
                gesture == KisShortcutConfiguration::OneFingerDoubleTap && !profileHasDoubleTap) {
                binding = QStringList{QStringLiteral("Touch Gestures"),
                           QString::number(KisTouchGestureAction::MirrorView)};
            }
            if (!touchSettings.contains(key) &&
                gesture == KisShortcutConfiguration::TwoFingerTap && !profileHasCustomTwoFingerTap) {
                binding = QStringList{QStringLiteral("Touch Gestures"),
                           QString::number(KisTouchGestureAction::UndoActionShortcut)};
            }
            if (binding.size() != 2) continue;
            bool validMode = false;
            const int mode = binding.at(1).toInt(&validMode);
            if (!validMode) continue;
            for (auto *action : actions) {
                if (action->id() == binding.first() && action->shortcutIndexes().values().contains(mode)) {
                    d->addTouchShortcut(action, mode, gesture);
                    break;
                }
            }
        }
#endif
"""+pinch_source[profile_end:]
    pinch_source=one(pinch_source,
        'QSettings().value(QStringLiteral("TabletUi/quickPinch"), true)',
        'QSettings().value(QStringLiteral("TabletUi/quickPinch"), false)',
        "default to Krita pinch handling without the optional quick reset")
    pinchfile.write_text(pinch_source,encoding="utf-8")

    gesture_header=root/"libs/ui/input/KisTouchGestureAction.h"
    gesture=gesture_header.read_text(encoding="utf-8")
    gesture=one(gesture,"    int priority() const override;",
        """    int priority() const override;
#ifdef Q_OS_ANDROID
    void markMobileTap() {m_mobileTap=true;}
#endif""","mark actual double taps without changing other Mirror bindings")
    gesture=one(gesture,"    int m_shortcut{-1};",
        """    int m_shortcut{-1};
#ifdef Q_OS_ANDROID
    bool m_mobileTap=false;
#endif""","track double-tap dispatch for the mirror action")
    gesture_header.write_text(gesture,encoding="utf-8")

    matcher_file=root/"libs/ui/input/kis_shortcut_matcher.cpp"
    matcher=matcher_file.read_text(encoding="utf-8")
    matcher=one(matcher,"#include <QTabletEvent>",
        '#include <QTabletEvent>\n#include "KisTouchGestureAction.h"',
        "identify the native mirror gesture after matching a double tap")
    matcher=one(matcher,"            doubleTap->action()->end(event);",
        """#ifdef Q_OS_ANDROID
            if (auto *gesture=dynamic_cast<KisTouchGestureAction*>(doubleTap->action()))
                gesture->markMobileTap();
#endif
            doubleTap->action()->end(event);""","carry the matched double-tap location into gesture dispatch")
    matcher=matcher.replace('QStringLiteral("TabletUi/doubleTapMs"), 150',
                            'QStringLiteral("TabletUi/doubleTapMs"), 250')
    a=matcher.index("void KisShortcutMatcher::fireReadyTouchShortcut")
    b=matcher.index("        goodCandidate->action()->end(event);",a)
    matcher=matcher[:b]+"""#ifdef Q_OS_ANDROID
        if (goodCandidate->matchTapType(event))
            if (auto *gesture=dynamic_cast<KisTouchGestureAction*>(goodCandidate->action()))
                gesture->markMobileTap();
#endif
"""+matcher[b:]
    matcher_file.write_text(matcher,encoding="utf-8")

    gesture_file=root/"libs/ui/input/KisTouchGestureAction.cpp"
    gestures=gesture_file.read_text(encoding="utf-8")
    gestures=one(gestures,'#include <QAction>',
        """#include <QAction>
#include <QPointer>
#include <QTimer>
#include <KisViewManager.h>
#include <kis_canvas2.h>
#include <kis_canvas_controller.h>
#include <QTouchEvent>
#include <optional>
#include "kis_input_manager.h" """.rstrip(),
        "resolve gesture commands through the event canvas")
    gestures=one(gestures,
        """    if (actionName.isEmpty()) {
        qWarning("KisTouchGestureAction: Unhandled shortcut %d", m_shortcut);
    } else {
        KisKActionCollection *actionCollection = KisPart::instance()->currentMainwindow()->actionCollection();
        QAction *action = actionCollection->action(actionName);
        if (action) {
            action->trigger();
        } else {
            qWarning("KisTouchGestureAction: unable to find action '%s' for shortcut %d",
                     qUtf8Printable(actionName),
                     m_shortcut);
        }
    }""",
        """    // A drag may send end() on finger release and again on TouchEnd.
#ifdef Q_OS_ANDROID
    const int shortcut = m_shortcut;
    const bool anchorAtTap=m_mobileTap;
    m_mobileTap=false;
    std::optional<QPointF> mirrorTap;
    if (anchorAtTap && shortcut==MirrorView && event) {
        const auto *touch=static_cast<QTouchEvent*>(event);
        if (!touch->touchPoints().isEmpty()) {
            QPointF center;
            for (const auto &point:touch->touchPoints()) center+=point.pos();
            mirrorTap=center/touch->touchPoints().size();
        }
    }
#endif
    m_shortcut = -1;
    if (actionName.isEmpty()) return;

    auto *manager = inputManager();
    QPointer<KisCanvas2> canvas = manager ? manager->canvas() : nullptr;
    auto *view = canvas ? canvas->viewManager() : nullptr;
    if (!view) return;
#ifdef Q_OS_ANDROID
    if (!view->mainWindow()) {
        switch (shortcut) {
        case UndoActionShortcut: actionName = QStringLiteral("mobile_gesture_undo"); break;
        case RedoActionShortcut: actionName = QStringLiteral("mobile_gesture_redo"); break;
        case ToggleCanvasOnlyShortcut: actionName = QStringLiteral("mobile_canvas_only"); break;
        default: break;
        }
    }
#endif
    // Undo, layer changes and tool switches can reenter the input matcher.
    // Dispatch after it finishes the current gesture and verify the canvas.
    QTimer::singleShot(0, view, [view, canvas, actionName
#ifdef Q_OS_ANDROID
                                , mirrorTap
#endif
                               ]() {
        if (!canvas || view->canvasBase() != canvas.data()) return;
#ifdef Q_OS_ANDROID
        if (mirrorTap && !view->mainWindow()) {
            if (auto *controller=dynamic_cast<KisCanvasController*>(canvas->canvasController())) {
                controller->mobileMirrorAtTap(*mirrorTap);
                return;
            }
        }
#endif
        auto *collection = view->actionCollection();
        auto *action = collection->action(actionName);
        if (!action) {
            const QString family = actionName.section(QLatin1Char('/'), -1);
            if (family.startsWith(QStringLiteral("KisTool"))) {
                for (auto *candidate : collection->actions()) {
                    if (candidate->objectName().section(QLatin1Char('/'), -1) == family) {
                        action = candidate;
                        break;
                    }
                }
            }
        }
        if (action && action->isEnabled()) action->trigger();
    });""","dispatch touch commands without a desktop window or input reentry")
    gesture_file.write_text(gestures,encoding="utf-8")

    popup_file=root/"libs/ui/input/KisPopupWidgetAction.cpp"
    popup=one(popup_file.read_text(encoding="utf-8"),"#include <QMenu>",
              "#include <QMenu>\n#include <QPointer>","keep a guarded reference to the touch menu")
    popup=one(popup,
        """    if (QMenu *popupMenu = inputManager()->toolProxy()->popupActionsMenu()) { // Handle popup menus...""",
        """    if (!inputManager() || !inputManager()->toolProxy()) return;
    if (event && (event->type() == QEvent::TouchBegin ||
                  event->type() == QEvent::TouchUpdate ||
                  event->type() == QEvent::TouchEnd ||
                  event->type() == QEvent::TouchCancel) &&
        static_cast<QTouchEvent *>(event)->touchPoints().isEmpty()) return;
    if (QPointer<QMenu> popupMenu = inputManager()->toolProxy()->popupActionsMenu()) {""",
        "guard touch popup position and lifetime")
    popup=one(popup,"                popupMenu->clear();",
        "                if (popupMenu) popupMenu->clear();","guard popup after its modal loop")
    popup_file.write_text(popup,encoding="utf-8")

    # Preserve MainActivity's native Qt/JNI intent delivery; only widen
    # URI extraction to include Android's single-file Share operation.
    activityfile=root/"packaging/android/apk/src/org/krita/android/MainActivity.java"
    activity=activityfile.read_text(encoding="utf-8")
    activity=one(activity,
        '''        super.onCreate(savedInstanceState);
        tabletPermissionActivity = this;''',
        '''        android.view.WindowManager.LayoutParams windowParams = getWindow().getAttributes();
        if (Build.VERSION.SDK_INT >= 28) {
            windowParams.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= 30
                ? android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                : android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(windowParams);
        }
        super.onCreate(savedInstanceState);
        tabletPermissionActivity = this;''',
        "Allow the Android drawing surface to extend around the camera cutout")
    activity=one(activity,
        '''        if (keyCode == KeyEvent.KEYCODE_BACK) {
            if (!JNIWrappers.hasMainWindowLoaded()) {''',
        '''        if (keyCode == KeyEvent.KEYCODE_BACK) {
            if (protectCanvasBack) return true;
            if (!JNIWrappers.hasMainWindowLoaded()) {''',
        "Consume legacy Android Back while canvas protection is active")
    activity=one(activity,
        "    public boolean isInFullScreen() {",
        '''    public static long studioCutoutInsets() {
        MainActivity activity = tabletPermissionActivity;
        if (activity == null || Build.VERSION.SDK_INT < 28) return 0;
        android.view.WindowInsets insets = activity.getWindow().getDecorView().getRootWindowInsets();
        android.view.DisplayCutout cutout = insets == null ? null : insets.getDisplayCutout();
        if (cutout == null) return 0;
        return ((long) cutout.getSafeInsetLeft() << 48)
            | ((long) cutout.getSafeInsetTop() << 32)
            | ((long) cutout.getSafeInsetRight() << 16)
            | cutout.getSafeInsetBottom();
    }

    private boolean protectCanvasBack = false;
    private Object canvasBackCallback;

    public static void setCanvasBackProtection(boolean enabled) {
        MainActivity activity = tabletPermissionActivity;
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            if (activity.protectCanvasBack == enabled) return;
            activity.protectCanvasBack = enabled;
            if (Build.VERSION.SDK_INT >= 33) activity.updateCanvasBackCallback(enabled);
        });
    }

    @RequiresApi(api = 33)
    private void updateCanvasBackCallback(boolean enabled) {
        if (enabled) {
            android.window.OnBackInvokedCallback callback = () -> {};
            canvasBackCallback = callback;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, callback);
        } else if (canvasBackCallback != null) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(
                (android.window.OnBackInvokedCallback) canvasBackCallback);
            canvasBackCallback = null;
        }
    }

    @Override
    public void onBackPressed() {
        if (!protectCanvasBack) super.onBackPressed();
    }

    public boolean isInFullScreen() {''',
        "Optionally keep Android Back from exiting the drawing canvas")
    activity=one(activity,
        '''            Uri fileUri = intent.getData();
            if (fileUri != null) {''',
        '''            Uri fileUri = intent.getData();
            if (fileUri == null && Intent.ACTION_SEND.equals(intent.getAction())) {
                Object stream = intent.getParcelableExtra(Intent.EXTRA_STREAM);
                if (stream instanceof Uri) fileUri = (Uri) stream;
            }
            if (fileUri != null) {''',
        "Android single Share URI extraction")
    # SAF URIs are not filesystem paths. Extract only Krita's embedded
    # preview PNG from a document stream, instead of asking the desktop
    # welcome page to open the entire project for a thumbnail.
    activity=one(activity,
        "    private static final String TAG",
        r'''    public static String tabletProjectRoots() {
        org.json.JSONArray roots = new org.json.JSONArray();
        roots.put(Environment.getExternalStorageDirectory().getAbsolutePath());
        MainActivity activity = tabletPermissionActivity;
        if (activity != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            android.os.storage.StorageManager storage =
                activity.getSystemService(android.os.storage.StorageManager.class);
            if (storage != null) for (android.os.storage.StorageVolume volume : storage.getStorageVolumes()) {
                java.io.File directory = volume.getDirectory();
                if (directory != null && !volume.isPrimary()) roots.put(directory.getAbsolutePath());
            }
        }
        return roots.toString();
    }

    public static void tabletRememberProjectAccess(String location) {
        MainActivity activity = tabletPermissionActivity;
        if (activity == null) return;
        Uri uri = Uri.parse(location);
        if (!android.provider.DocumentsContract.isDocumentUri(activity, uri)) return;
        android.content.ContentResolver resolver = activity.getContentResolver();
        try {
            resolver.takePersistableUriPermission(uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        } catch (SecurityException readOnly) {
            try {
                resolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException temporary) {
                // Some providers offer only temporary access.
            }
        }
    }

    public static String tabletProjectName(String location) {
        MainActivity activity = tabletPermissionActivity;
        if (activity == null) return "";
        Uri uri = Uri.parse(location);
        String column = android.provider.OpenableColumns.DISPLAY_NAME;
        try (android.database.Cursor row = activity.getContentResolver().query(
                uri, new String[]{column}, null, null, null)) {
            if (row != null && row.moveToFirst() && !row.isNull(0)) return row.getString(0);
        } catch (RuntimeException ignored) {
            // A provider can disappear or revoke access.
        }
        return "";
    }

    public static long tabletProjectModified(String location) {
        MainActivity activity = tabletPermissionActivity;
        if (activity == null) return 0;
        Uri uri = Uri.parse(location);
        String column = DocumentsContract.Document.COLUMN_LAST_MODIFIED;
        try (android.database.Cursor row = activity.getContentResolver().query(
                uri, new String[]{column}, null, null, null)) {
            return row != null && row.moveToFirst() ? row.getLong(0) : 0;
        } catch (RuntimeException ignored) {
            return 0;
        }
    }

    public static boolean tabletDeleteProject(String location) {
        MainActivity activity = tabletPermissionActivity;
        if (activity == null) return false;
        Uri uri = Uri.parse(location);
        try {
            return android.provider.DocumentsContract.isDocumentUri(activity, uri) &&
                android.provider.DocumentsContract.deleteDocument(activity.getContentResolver(), uri);
        } catch (java.io.FileNotFoundException | RuntimeException ignored) {
            return false;
        }
    }

    public static boolean tabletRememberProjectFolder(Intent result) {
        MainActivity activity = tabletPermissionActivity;
        if (activity == null || result == null || result.getData() == null) return false;
        Uri tree = result.getData();
        if (!DocumentsContract.isTreeUri(tree)) return false;
        int flags = result.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        if ((flags & Intent.FLAG_GRANT_READ_URI_PERMISSION) == 0) return false;
        try {
            activity.getContentResolver().takePersistableUriPermission(tree, flags);
            return true;
        } catch (SecurityException denied) {
            return false;
        }
    }

    private static boolean tabletIsFolderGrant(Uri uri) {
        return DocumentsContract.isTreeUri(uri) && uri.getPathSegments().size() == 2;
    }

    public static String tabletProjectFolders() {
        org.json.JSONArray folders = new org.json.JSONArray();
        MainActivity activity = tabletPermissionActivity;
        if (activity == null) return folders.toString();
        for (android.content.UriPermission grant : activity.getContentResolver().getPersistedUriPermissions()) {
            Uri uri = grant.getUri();
            if (!grant.isReadPermission() || !tabletIsFolderGrant(uri)) continue;
            try {
                org.json.JSONObject folder = new org.json.JSONObject();
                folder.put("uri", uri.toString());
                folder.put("name", DocumentsContract.getTreeDocumentId(uri) + "  (" + uri.getAuthority() + ")");
                folders.put(folder);
            } catch (Exception ignored) {
                // Stale grant.
            }
        }
        return folders.toString();
    }

    public static boolean tabletForgetProjectFolder(String location) {
        MainActivity activity = tabletPermissionActivity;
        if (activity == null || location == null) return false;
        Uri tree = Uri.parse(location);
        if (!tabletIsFolderGrant(tree)) return false;
        android.content.ContentResolver resolver = activity.getContentResolver();
        for (android.content.UriPermission grant : resolver.getPersistedUriPermissions()) {
            if (!tree.equals(grant.getUri())) continue;
            int flags = 0;
            if (grant.isReadPermission()) flags |= Intent.FLAG_GRANT_READ_URI_PERMISSION;
            if (grant.isWritePermission()) flags |= Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
            try {
                resolver.releasePersistableUriPermission(tree, flags);
                return true;
            } catch (SecurityException denied) {
                return false;
            }
        }
        return false;
    }

    private static final String[] tabletProjectColumns = {
        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE,
        DocumentsContract.Document.COLUMN_LAST_MODIFIED
    };

    private static void tabletAppendProject(org.json.JSONArray projects,
            java.util.Set<String> seen, Uri uri, android.database.Cursor row,
            String folder) throws org.json.JSONException {
        String name = row.getString(1);
        if (name == null) return;
        String lower = name.toLowerCase(java.util.Locale.ROOT);
        if (!lower.matches(".*[.]kr[az]( \\(\\d+\\))?") &&
            !"application/x-krita".equals(row.getString(2))) return;
        if (!seen.add(uri.getAuthority() + "/" + row.getString(0))) return;
        org.json.JSONObject item = new org.json.JSONObject();
        item.put("uri", uri.toString());
        item.put("name", name);
        item.put("folder", folder);
        item.put("modified", row.getLong(3));
        if ("com.android.externalstorage.documents".equals(uri.getAuthority())) {
            String id = row.getString(0);
            int colon = id.indexOf(':');
            if (colon > 0) {
                String volume = id.substring(0, colon);
                java.io.File root = "primary".equals(volume) ?
                    Environment.getExternalStorageDirectory() : new java.io.File("/storage", volume);
                item.put("localPath", new java.io.File(root, id.substring(colon + 1)).getPath());
            }
        }
        projects.put(item);
    }

    private static void tabletScanProjectTree(android.content.ContentResolver resolver,
            Uri tree, org.json.JSONArray projects, java.util.Set<String> seen) {
        java.util.ArrayDeque<android.util.Pair<String, String>> folders = new java.util.ArrayDeque<>();
        java.util.HashSet<String> visited = new java.util.HashSet<>();
        String rootId = DocumentsContract.getTreeDocumentId(tree);
        String rootName = tree.getAuthority();
        Uri root = DocumentsContract.buildDocumentUriUsingTree(tree, rootId);
        try (android.database.Cursor row = resolver.query(root, tabletProjectColumns, null, null, null)) {
            if (row != null && row.moveToFirst() && row.getString(1) != null) rootName = row.getString(1);
        } catch (Exception ignored) {}
        folders.add(new android.util.Pair<>(rootId, rootName));
        while (!folders.isEmpty()) {
            android.util.Pair<String, String> folder = folders.removeFirst();
            if (!visited.add(folder.first)) continue;
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, folder.first);
            try (android.database.Cursor rows = resolver.query(children, tabletProjectColumns, null, null, null)) {
                if (rows == null) continue;
                while (rows.moveToNext()) {
                    String id = rows.getString(0);
                    if (id == null) continue;
                    if (DocumentsContract.Document.MIME_TYPE_DIR.equals(rows.getString(2))) {
                        String name = rows.getString(1);
                        if (!".krita-gallery-trash".equals(name))
                            folders.add(new android.util.Pair<>(id, folder.second + "/" + name));
                    } else {
                        tabletAppendProject(projects, seen,
                            DocumentsContract.buildDocumentUriUsingTree(tree, id), rows, folder.second);
                    }
                }
            } catch (Exception ignored) {
                // An unavailable child folder does not invalidate the whole tree.
            }
        }
    }

    public static String tabletGrantedProjects() {
        org.json.JSONArray projects = new org.json.JSONArray();
        MainActivity activity = tabletPermissionActivity;
        if (activity == null) return projects.toString();
        android.content.ContentResolver resolver = activity.getContentResolver();
        java.util.Set<String> seen = new java.util.HashSet<>();
        for (android.content.UriPermission permission : resolver.getPersistedUriPermissions()) {
            if (!permission.isReadPermission()) continue;
            Uri uri = permission.getUri();
            try {
                if (tabletIsFolderGrant(uri)) {
                    tabletScanProjectTree(resolver, uri, projects, seen);
                } else if (DocumentsContract.isDocumentUri(activity, uri)) {
                    try (android.database.Cursor row = resolver.query(uri, tabletProjectColumns, null, null, null)) {
                        if (row != null && row.moveToFirst())
                            tabletAppendProject(projects, seen, uri, row, uri.getAuthority());
                    }
                }
            } catch (Exception ignored) {
                // Grants may be stale or the provider may be offline.
            }
        }
        return projects.toString();
    }

    public static byte[] tabletKraPreview(String location) {
        MainActivity activity=tabletPermissionActivity;
        if(activity==null || location==null ||
           !location.startsWith("content://"))return null;
        try(java.io.InputStream input=activity.getContentResolver()
                .openInputStream(Uri.parse(location))) {
            if(input==null)return null;
            try(java.util.zip.ZipInputStream zip=new java.util.zip.ZipInputStream(
                    new java.io.BufferedInputStream(input,32768))) {
                for(int entries=0;entries<96;++entries) {
                    java.util.zip.ZipEntry entry=zip.getNextEntry();
                    if(entry==null)return null;
                    String name=entry.getName();
                    if("preview.png".equals(name) ||
                       "Thumbnails/thumbnail.png".equals(name)) {
                        java.io.ByteArrayOutputStream preview=
                            new java.io.ByteArrayOutputStream(32768);
                        byte[] buffer=new byte[16384];
                        int length;
                        while((length=zip.read(buffer))!=-1) {
                            if(preview.size()+length>8*1024*1024)return null;
                            preview.write(buffer,0,length);
                        }
                        return preview.toByteArray();
                    }
                    // Do not decompress huge merged-image entries just to
                    // reach a later thumbnail. Preview is normally early.
                    if(entry.getSize()>24L*1024*1024)return null;
                    zip.closeEntry();
                }
            }
        }catch(Exception ignored) {
            // URI access can be revoked or providers can expose bad ZIPs.
        }
        return null;
    }

    private static final String TAG''',
        "Android SAF embedded KRA thumbnail helper")
    if "import android.provider.DocumentsContract;" not in activity:
        activity=one(activity,"import android.net.Uri;",
            "import android.net.Uri;\nimport android.provider.DocumentsContract;",
            "Android document tree import")
    activityfile.write_text(activity,encoding="utf-8")

    debugManifest=root/"packaging/android/apk/flavors/debug/AndroidManifest.xml"
    debugXml=debugManifest.read_text(encoding="utf-8")
    debugXml=one(debugXml,'android:label="Krita Debug"',
                 'android:label="Krita Mobile Studio"',
                 "Mobile Studio debug label")
    debugManifest.write_text(debugXml,encoding="utf-8")
    text_tool_file=root/"plugins/tools/svgtexttool/SvgTextTool.cpp"
    text_tool=text_tool_file.read_text(encoding="utf-8")
    text_tool=one(text_tool,"#include <QMenuBar>",
        "#include <QMenuBar>\n#include <QPushButton>\n#include <QVBoxLayout>",
        "mobile SVG text controls includes")
    text_tool=one(text_tool,
        '    if (canvas2) {\n        canvas2->setCurrentShapeManagerOwnerShape(nullptr);',
        '    if (canvas2 && canvas2->viewManager()) {\n        canvas2->setCurrentShapeManagerOwnerShape(nullptr);',
        "SVG text view manager guard")
    text_tool=one(text_tool,
        '        QDockWidget *docker = canvas2->viewManager()->mainWindow()->dockWidget("TextProperties");\n        if (docker && m_optionManager) {\n            m_optionManager->setTextPropertiesOpen(docker->isVisible());\n        }',
        '        auto *mainWindow = canvas2->viewManager()->mainWindow();\n        auto *docker = mainWindow ? mainWindow->dockWidget("TextProperties") : nullptr;\n        if (m_optionManager) {\n            m_optionManager->setTextPropertiesOpen(docker && docker->isVisible());\n        }',
        "SVG text activation without a desktop window")
    text_tool=one(text_tool,
        '    if (optionWidget->errors().isEmpty()) {\n        optionWidget->rootObject()->setProperty("manager",',
        '    if (optionWidget->rootObject()) {\n        optionWidget->rootObject()->setProperty("manager",',
        "SVG text options QML root guard")
    text_tool=one(text_tool,
        '    if (canvas2 && canvas2->viewManager()->mainWindow()) {',
        '    if (canvas2 && canvas2->viewManager() && canvas2->viewManager()->mainWindow()) {',
        "SVG text advanced desktop controls guard")
    text_tool=one(text_tool,
        '    slotTextTypeUpdated();\n\n\n    return optionWidget;',
        '''    slotTextTypeUpdated();

#ifdef Q_OS_ANDROID
    if (canvas2 && canvas2->viewManager() && !canvas2->viewManager()->mainWindow()) {
        auto *panel = new QWidget;
        panel->setObjectName(toolId() + " option widget");
        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QPushButton(i18n("Edit selected text"), panel);
        edit->setObjectName(QStringLiteral("mobileEditSvgText"));
        edit->setMinimumHeight(44);
        connect(edit, &QPushButton::clicked, this, &SvgTextTool::showEditor);
        layout->addWidget(edit);
        auto *glyphs = new QPushButton(i18n("Glyphs"), panel);
        glyphs->setObjectName(QStringLiteral("mobileSvgGlyphs"));
        glyphs->setMinimumHeight(44);
        connect(glyphs, &QPushButton::clicked, this, &SvgTextTool::showGlyphPalette);
        layout->addWidget(glyphs);
        if (optionWidget->rootObject()) {
            layout->addWidget(optionWidget);
        } else {
            delete optionWidget;
        }
        return panel;
    }
#endif
    return optionWidget;''',
        "standalone SVG text native editor controls")
    text_tool_file.write_text(text_tool,encoding="utf-8")

    text_dock_file=root/"plugins/dockers/textproperties/TextPropertiesDock.cpp"
    text_dock=text_dock_file.read_text(encoding="utf-8")
    text_dock=one(text_dock,
        '    if (!m_quickWidget->errors().empty()) {',
        '    setProperty("_mobileTextFormattingReady", m_quickWidget->rootObject() != nullptr);\n    if (!m_quickWidget->rootObject()) {',
        "text formatting QML root guard")
    text_dock_file.write_text(text_dock,encoding="utf-8")

    text_observer_file=root/"plugins/dockers/textproperties/TextPropertiesCanvasObserver.cpp"
    text_observer=text_observer_file.read_text(encoding="utf-8")
    text_observer=one(text_observer,
        "        // This initializes the docker to an empty entry;",
        '''        if (!kisview->mainWindow()) {
            // Studio opens the formatting panel after text may already be selected.
            // Copy the current values instead of writing an empty selection back.
            slotCanvasTextPropertiesChanged();
            slotCanvasCharacterPropertiesChanged();
            return;
        }
        // This initializes the docker to an empty entry;''',
        "initialize standalone text formatting without changing the selection")
    text_observer_file.write_text(text_observer,encoding="utf-8")
    measure_file=root/"plugins/tools/basictools/kis_tool_measure.cc"
    measure=measure_file.read_text(encoding="utf-8")
    for variable,name in (("m_distanceLabel","mobileMeasureDistance"),
                          ("m_angleLabel","mobileMeasureAngle"),
                          ("unitBox","mobileMeasureUnits")):
        widget="KComboBox" if variable=="unitBox" else "QLabel"
        anchor=f"{variable} = new {widget}(this);"
        measure=one(measure,anchor,anchor+f'\n    {variable}->setObjectName("{name}");',
                    f"Studio Measure {variable}")
    measure_file.write_text(measure,encoding="utf-8")

    dyna_file=root/"plugins/tools/tool_dyna/kis_tool_dyna.cpp"
    dyna=dyna_file.read_text(encoding="utf-8")
    for variable,name in (("m_massSPBox","mobileDynaMass"),("m_dragSPBox","mobileDynaDrag")):
        anchor=f"{variable} = new KisDoubleSliderSpinBox(optionsWidget);"
        dyna=one(dyna,anchor,anchor+f'\n    {variable}->setObjectName("{name}");',
                 f"Studio Dynamic Brush {variable}")
    dyna_file.write_text(dyna,encoding="utf-8")

    controller_header=root/"libs/ui/canvas/kis_canvas_controller.h"
    controller=controller_header.read_text(encoding="utf-8")
    controller=one(controller,"    void mirrorCanvas(bool enable);",
        """    void mirrorCanvas(bool enable);
#ifdef Q_OS_ANDROID
    void mobileMirrorView(bool enable);
    void mobileMirrorAtTap(const QPointF &widgetPoint);
    void mobileRememberPenPoint(const QPointF &documentPoint);
#endif"""," expose native view mirroring with an explicit anchor")
    controller=one(controller,"    void mirrorCanvasImpl(const std::optional<KoViewTransformStillPoint> &stillPoint, bool enable);",
        """    void mirrorCanvasImpl(const std::optional<KoViewTransformStillPoint> &stillPoint, bool enable);
#ifdef Q_OS_ANDROID
    std::optional<QPointF> m_mobilePenPoint;
#endif""","keep the pen anchor local to each canvas")
    controller_header.write_text(controller,encoding="utf-8")

    controller_source=root/"libs/ui/canvas/kis_canvas_controller.cpp"
    controller=controller_source.read_text(encoding="utf-8")
    controller=one(controller,"#include <QMouseEvent>","#include <QMouseEvent>\n#include <QSettings>",
                   "read the mobile mirror preference")
    controller=one(controller,"void KisCanvasController::mirrorCanvasAroundCursor(bool enable)",
        """#ifdef Q_OS_ANDROID
void KisCanvasController::mobileRememberPenPoint(const QPointF &documentPoint)
{
    m_mobilePenPoint=documentPoint;
}

void KisCanvasController::mobileMirrorView(bool enable)
{
    if (m_mobilePenPoint && QSettings().value(QStringLiteral("MobileStudio/mirrorAtPen"),true).toBool()) {
        const auto point=m_d->coordinatesConverter->makeDocStillPoint(*m_mobilePenPoint);
        const auto *widget=m_d->view->canvasBase()->canvasWidget();
        if (widget && widget->rect().contains(m_d->coordinatesConverter->documentToWidget(*m_mobilePenPoint).toPoint())) {
            mirrorCanvasImpl(point,enable);
            return;
        }
    }
    mirrorCanvas(enable);
}

void KisCanvasController::mobileMirrorAtTap(const QPointF &widgetPoint)
{
    const bool enable=!m_d->coordinatesConverter->xAxisMirrored();
    if (QSettings().value(QStringLiteral("MobileStudio/mirrorAtPen"),true).toBool())
        mirrorCanvasImpl(m_d->coordinatesConverter->makeWidgetStillPoint(widgetPoint),enable);
    else
        mirrorCanvas(enable);
}
#endif

void KisCanvasController::mirrorCanvasAroundCursor(bool enable)""","reuse Krita's still-point mirroring")
    controller_source.write_text(controller,encoding="utf-8")
    mirror_file=root/"libs/ui/kis_mirror_manager.cpp"
    mirror=mirror_file.read_text(encoding="utf-8")
    mirror=one(mirror,
        "        connect(m_mirrorCanvas, SIGNAL(toggled(bool)), dynamic_cast<KisCanvasController*>(m_imageView->canvasController()), SLOT(mirrorCanvas(bool)));",
        """        auto *controller=dynamic_cast<KisCanvasController*>(m_imageView->canvasController());
#ifdef Q_OS_ANDROID
        if (!m_imageView->viewManager()->mainWindow())
            connect(m_mirrorCanvas,&QAction::toggled,controller,&KisCanvasController::mobileMirrorView);
        else
#endif
            connect(m_mirrorCanvas,&QAction::toggled,controller,&KisCanvasController::mirrorCanvas);""",
        "anchor the independent Studio mirror action without changing native reset")
    mirror_file.write_text(mirror,encoding="utf-8")

    provider_header=root/"libs/ui/kis_canvas_resource_provider.h"
    provider=provider_header.read_text(encoding="utf-8")
    provider=one(provider,"    void sigFGColorChanged(const KoColor &);",
        """    void sigFGColorChanged(const KoColor &);
#ifdef Q_OS_ANDROID
    void sigMobileStrokeColorsChanged(const QStringList &colors);
#endif""","refresh both Studio history surfaces after a brush stroke")
    provider_header.write_text(provider,encoding="utf-8")

    freehand_header=root/"libs/ui/tool/kis_tool_freehand.h"
    freehand=freehand_header.read_text(encoding="utf-8")
    freehand=one(freehand,"    bool trySampleByPaintOp(KoPointerEvent *event, AlternateAction action);",
        """    bool trySampleByPaintOp(KoPointerEvent *event, AlternateAction action,
                            bool pickCloneSource = false);""",
        "Clone source sampling flag")
    freehand=one(freehand,"    Q_OBJECT",
        "    Q_OBJECT\n    Q_PROPERTY(bool mobileGesturePending READ mobileGesturePending)",
        "cancel Clone source picking when Android interrupts the pointer")
    freehand=one(freehand,"    int flags() const override;",
        """    int flags() const override;
    Q_INVOKABLE bool mobileArmCloneSource();
    bool mobileGesturePending() const {return m_mobileCloneSourceTap;}
    void requestStrokeCancellation() override;
    void requestStrokeEnd() override;""","Studio Clone source command")
    freehand=one(freehand,"    bool m_paintopBasedSamplingInAction {false};",
        """    bool m_paintopBasedSamplingInAction {false};
    KisPaintOpPresetSP m_mobileClonePreset;
    bool m_mobileCloneSourceTap {false};""","one-shot Clone source state")
    freehand=one(freehand,"#include <kis_distance_information.h>",
        "#include <kis_distance_information.h>\n#include <QColor>","store the foreground color at stroke start")
    freehand=one(freehand,"    bool m_mobileCloneSourceTap {false};",
        """    bool m_mobileCloneSourceTap {false};
#ifdef Q_OS_ANDROID
    QColor m_mobileStrokeColor;
#endif""","capture the color used when a native brush stroke starts")
    freehand_header.write_text(freehand,encoding="utf-8")

    freehand_file=root/"libs/ui/tool/kis_tool_freehand.cc"
    freehand=freehand_file.read_text(encoding="utf-8")
    freehand=one(freehand,"void KisToolFreehand::deactivate()\n{",
        """void KisToolFreehand::deactivate()
{
    m_mobileClonePreset.clear();
    m_mobileCloneSourceTap = false;""","clear Clone source state on tool change")
    freehand=one(freehand,"void KisToolFreehand::beginPrimaryAction(KoPointerEvent *event)\n{",
        """bool KisToolFreehand::mobileArmCloneSource()
{
    const auto preset = currentPaintOpPreset();
    m_mobileClonePreset = mode()==HOVER_MODE && preset &&
        preset->paintOp().id()==QStringLiteral("duplicate")?preset:KisPaintOpPresetSP();
    return bool(m_mobileClonePreset);
}

void KisToolFreehand::requestStrokeCancellation()
{
    m_mobileCloneSourceTap = false;
    m_mobileClonePreset.clear();
    KisToolPaint::requestStrokeCancellation();
}

void KisToolFreehand::requestStrokeEnd()
{
    m_mobileCloneSourceTap = false;
    m_mobileClonePreset.clear();
    KisToolPaint::requestStrokeEnd();
}

void KisToolFreehand::beginPrimaryAction(KoPointerEvent *event)
{
    m_mobileCloneSourceTap = m_mobileClonePreset && m_mobileClonePreset==currentPaintOpPreset();
    if (m_mobileCloneSourceTap) return;
    m_mobileClonePreset.clear();
""","use native Clone sampling on the next canvas tap")
    freehand=one(freehand,"void KisToolFreehand::continuePrimaryAction(KoPointerEvent *event)\n{",
        """void KisToolFreehand::continuePrimaryAction(KoPointerEvent *event)
{
    if (m_mobileCloneSourceTap) return;""","avoid painting while picking a Clone source")
    freehand=one(freehand,"void KisToolFreehand::endPrimaryAction(KoPointerEvent *event)\n{",
        """void KisToolFreehand::endPrimaryAction(KoPointerEvent *event)
{
    if (m_mobileCloneSourceTap) {
        const auto preset=m_mobileClonePreset;
        m_mobileCloneSourceTap=false;
        m_mobileClonePreset.clear();
        if (event && preset && preset==currentPaintOpPreset() &&
            trySampleByPaintOp(event, SampleFgImage, true))
            requestUpdateOutline(event->point, event);
        return;
    }""","sample only on release and never leave an accidental paint mark")
    freehand=one(freehand,
        "bool KisToolFreehand::trySampleByPaintOp(KoPointerEvent *event, AlternateAction action)",
        """bool KisToolFreehand::trySampleByPaintOp(KoPointerEvent *event, AlternateAction action,
                                           bool pickCloneSource)""","receive Clone source flag")
    freehand=one(freehand,"                                                                                   event->modifiers(),",
        """                                                                                   pickCloneSource ? Qt::KeyboardModifiers(Qt::ControlModifier) : event->modifiers(),""",
        "reuse native Clone engine origin selection")
    freehand=one(freehand,'#include "canvas/kis_canvas2.h"',
        """#include "canvas/kis_canvas2.h"
#ifdef Q_OS_ANDROID
#include "canvas/kis_canvas_controller.h"
#include "recent_colors.h"
#endif""","native stroke color and mirror anchor")
    freehand=one(freehand,"    m_helper->initPaint(event,",
        """#ifdef Q_OS_ANDROID
    m_mobileStrokeColor=QColor();
    if (auto *native=dynamic_cast<KisCanvas2*>(canvas())) {
        if (auto *controller=dynamic_cast<KisCanvasController*>(native->canvasController()))
            controller->mobileRememberPenPoint(event->point);
        auto *view=native->viewManager();
        const auto preset=currentPaintOpPreset();
        if (!view->mainWindow() && preset && preset->paintOp().id()!=QStringLiteral("duplicate") &&
            !isEraser()) {
            auto *resources=view->canvasResourceProvider();
            if (resources->opacity()>0) m_mobileStrokeColor=resources->fgColor().toQColor();
        }
    }
#endif
    m_helper->initPaint(event,""","record only colors used to begin real paint strokes")
    freehand=one(freehand,"    m_helper->paintEvent(event);",
        """#ifdef Q_OS_ANDROID
    if (auto *controller=dynamic_cast<KisCanvasController*>(canvas()->canvasController()))
        controller->mobileRememberPenPoint(event->point);
#endif
    m_helper->paintEvent(event);""","track the last painted document point")
    freehand=one(freehand,"    m_helper->endPaint();",
        """#ifdef Q_OS_ANDROID
    const auto color=m_mobileStrokeColor;
    m_mobileStrokeColor=QColor();
    const bool painted=m_helper->isRunning();
#endif
    m_helper->endPaint();
#ifdef Q_OS_ANDROID
    if (painted && color.isValid()) {
        if (auto *native=dynamic_cast<KisCanvas2*>(canvas())) {
            const auto colors=MobileRecentColors::recordStroke(color);
            if (!colors.isEmpty())
                Q_EMIT native->viewManager()->canvasResourceProvider()->sigMobileStrokeColorsChanged(colors);
        }
    }
#endif""","publish brush color history after the native stroke ends")
    freehand=one(freehand,"void KisToolFreehand::requestStrokeCancellation()\n{",
        """void KisToolFreehand::requestStrokeCancellation()
{
#ifdef Q_OS_ANDROID
    m_mobileStrokeColor=QColor();
#endif""","exclude cancelled strokes from color history")
    freehand_file.write_text(freehand,encoding="utf-8")

    fill_dir=root/"plugins/tools/basictools"
    (fill_dir/"fill_tolerance_drag.h").write_text(
        (own/"mobile_core/fill_tolerance_drag.h").read_text(encoding="utf-8"),
        encoding="utf-8")
    (fill_dir/"fill_tolerance_preview.h").write_text(
        (own/"mobile_core/fill_tolerance_preview.h").read_text(encoding="utf-8"),
        encoding="utf-8")
    fill_header=fill_dir/"kis_tool_fill.h"
    fill=fill_header.read_text(encoding="utf-8")
    fill=one(fill,"#include <QVector>",
        "#include <QVector>\n#ifdef Q_OS_ANDROID\n#include <QPointer>\n#endif\nclass MobileFillTolerancePreview;",
        "Fill tolerance preview widget")
    fill=one(fill,"    Q_OBJECT",
        """    Q_OBJECT
#ifdef Q_OS_ANDROID
    Q_PROPERTY(bool mobileGesturePending READ mobileGesturePending)
#endif""","expose pending Fill gesture to the native input proxy")
    fill=one(fill,"    QWidget* createOptionWidget() override;",
        """    QWidget* createOptionWidget() override;
#ifdef Q_OS_ANDROID
    bool mobileGesturePending() const {return bool(m_mobileFillTarget);}
    void requestStrokeCancellation() override;
    void requestStrokeEnd() override;
#endif""","discard incomplete Fill gestures through native stroke requests")
    fill=one(fill,"    FillMode m_fillMode {FillMode_FillContiguousRegion};",
        """#ifdef Q_OS_ANDROID
    void clearMobileFillTolerance();
    void updateMobileFillTolerance(int widgetX);
    bool m_mobileDragTolerance = false;
    int m_mobileInitialTolerance = 8;
    int m_mobilePreviewTolerance = 8;
    KisNodeSP m_mobileFillTarget;
    QPoint m_mobileFillSeed;
    QPointer<MobileFillTolerancePreview> m_mobileTolerancePreview;
#endif
    FillMode m_fillMode {FillMode_FillContiguousRegion};""","store the Fill seed and target until release")
    fill_header.write_text(fill,encoding="utf-8")
    fill_source=fill_dir/"kis_tool_fill.cc"
    fill=fill_source.read_text(encoding="utf-8")
    fill=one(fill,'#include "kis_tool_fill.h"',
        '''#include "kis_tool_fill.h"
#ifdef Q_OS_ANDROID
#include "fill_tolerance_preview.h"
#include <QScopedValueRollback>
#include <KisView.h>
#endif''',"Fill tolerance calculation")
    fill=one(fill,"KisToolFill::~KisToolFill()\n{\n}",
        """KisToolFill::~KisToolFill()
{
#ifdef Q_OS_ANDROID
    delete m_mobileTolerancePreview.data();
#endif
}""","destroy any remaining Fill preview")
    fill=one(fill,"void KisToolFill::deactivate()\n{",
        """void KisToolFill::deactivate()
{
#ifdef Q_OS_ANDROID
    clearMobileFillTolerance();
#endif""","cancel pending Fill when switching tools")
    fill=one(fill,"    m_seedPoints.append(lastImagePosition);",
        """#ifdef Q_OS_ANDROID
    if (m_mobileDragTolerance && m_effectiveFillMode == FillMode_FillContiguousRegion) {
        m_mobileFillTarget = currentNode();
        m_mobileFillSeed = lastImagePosition;
        m_mobileInitialTolerance = m_threshold;
        setMode(KisTool::PAINT_MODE);
        if (auto *kisCanvas=dynamic_cast<KisCanvas2*>(canvas())) {
            const auto selection=kisCanvas->imageView()?kisCanvas->imageView()->selection():KisSelectionSP();
            const auto source=image()->wrapAroundModePermitted()?KisPaintDeviceSP():
                m_reference==Reference_CurrentLayer?currentNode()->paintDevice():
                m_reference==Reference_AllLayers?currentImage()->projection():KisPaintDeviceSP();
            MobileFillTolerancePreview::Options options;
            options.spread=m_opacitySpread;
            options.closeGap=m_closeGap;
            options.boundaryFill=m_contiguousFillMode==ContiguousFillMode_BoundaryFill;
            options.boundaryColor=m_contiguousFillBoundaryColor;
            options.selectionBoundary=m_useSelectionAsBoundary;
            options.clipToSelection=true;
            options.antiAlias=m_antiAlias;
            options.grow=m_sizemod;
            options.feather=m_feather;
            options.stopGrowingAtDarkest=m_stopGrowingAtDarkestPixel;
            m_mobileTolerancePreview=new MobileFillTolerancePreview(kisCanvas,currentImage(),source,
                selection?selection->projection():KisPixelSelectionSP(),lastImagePosition,options);
            updateMobileFillTolerance(event->pos().x());
        }
        return;
    }
#endif
    m_seedPoints.append(lastImagePosition);""","defer the native contiguous Fill until release")
    fill=one(fill,"void KisToolFill::continuePrimaryAction(KoPointerEvent *event)\n{",
        """void KisToolFill::continuePrimaryAction(KoPointerEvent *event)
{
#ifdef Q_OS_ANDROID
    if (mobileGesturePending()) {
        updateMobileFillTolerance(event->pos().x());
        return;
    }
#endif""","preview tolerance without scheduling flood-fill jobs")
    fill=one(fill,"void KisToolFill::endPrimaryAction(KoPointerEvent *)\n{",
        """#ifdef Q_OS_ANDROID
void KisToolFill::updateMobileFillTolerance(int widgetX)
{
    m_mobilePreviewTolerance = mobileFillTolerance(
        m_mobileInitialTolerance,widgetX - m_fillStartWidgetPosition.x());
    if (m_mobileTolerancePreview) m_mobileTolerancePreview->setThreshold(m_mobilePreviewTolerance);
}

void KisToolFill::clearMobileFillTolerance()
{
    if (!mobileGesturePending()) return;
    m_mobileFillTarget.clear();
    delete m_mobileTolerancePreview.data();
    setMode(KisTool::HOVER_MODE);
}

void KisToolFill::requestStrokeCancellation()
{
    clearMobileFillTolerance();
    KisToolPaint::requestStrokeCancellation();
}

void KisToolFill::requestStrokeEnd()
{
    clearMobileFillTolerance();
    KisToolPaint::requestStrokeEnd();
}
#endif

void KisToolFill::endPrimaryAction(KoPointerEvent *event)
{
#ifdef Q_OS_ANDROID
    if (mobileGesturePending()) {
        const KisNodeSP target = m_mobileFillTarget;
        const QPoint seed = m_mobileFillSeed;
        if (event) updateMobileFillTolerance(event->pos().x());
        clearMobileFillTolerance();
        if (!event || currentNode() != target || !nodeEditable() ||
            nodePaintAbility() != NodePaintAbility::PAINT) return;
        const QScopedValueRollback<int> threshold(m_threshold,m_mobilePreviewTolerance);
        m_seedPoints = {seed};
        beginFilling(seed);
        if (m_fillStrokeId) {
            slotUpdateFill();
            endFilling();
        } else {
            setMode(KisTool::HOVER_MODE);
        }
        m_seedPoints.clear();
        return;
    }
#else
    Q_UNUSED(event);
#endif""","apply one native Fill at the original seed with the previewed threshold")
    fill=one(fill,'    sectionRegionExtent->appendWidget("sliderThreshold", m_sliderThreshold);',
        """    sectionRegionExtent->appendWidget("sliderThreshold", m_sliderThreshold);
#ifdef Q_OS_ANDROID
    auto *dragTolerance = new QCheckBox(i18n("Adjust tolerance by dragging"));
    dragTolerance->setChecked(m_mobileDragTolerance);
    sectionRegionExtent->appendWidget("mobileFillDragTolerance",dragTolerance);
    connect(dragTolerance,&QCheckBox::toggled,this,[this](bool enabled) {
        m_mobileDragTolerance = enabled;
        m_configGroup.writeEntry("mobileDragTolerance",enabled);
    });
#endif""","add an opt-in native Fill option")
    fill=one(fill,'    m_threshold = m_configGroup.readEntry<int>("thresholdAmount", 8);',
        """    m_threshold = m_configGroup.readEntry<int>("thresholdAmount", 8);
#ifdef Q_OS_ANDROID
    m_mobileDragTolerance = m_configGroup.readEntry<bool>("mobileDragTolerance",false);
#endif""","persist the Android Fill gesture preference")
    fill_source.write_text(fill,encoding="utf-8")

    select_dir=root/"plugins/tools/selectiontools"
    select_header=select_dir/"kis_tool_select_contiguous.h"
    select=select_header.read_text(encoding="utf-8")
    select=one(select,"class KoGroupButton;","class KoGroupButton;\nclass MobileFillTolerancePreview;",
               "declare shared contiguous preview")
    select=one(select,"    Q_OBJECT",
        """    Q_OBJECT
#ifdef Q_OS_ANDROID
    Q_PROPERTY(bool mobileGesturePending READ mobileGesturePending)
#endif""","route cancelled input through contiguous selection")
    select=one(select,"    void endPrimaryAction(KoPointerEvent *event) override;",
        """    void endPrimaryAction(KoPointerEvent *event) override;
#ifdef Q_OS_ANDROID
    void continuePrimaryAction(KoPointerEvent *event) override;
    bool mobileGesturePending() const {return bool(m_mobileTarget);}
    void requestStrokeEnd() override;
    void requestStrokeCancellation() override;
#endif""","defer opt-in native selection to release")
    select=one(select,"    ContiguousSelectionMode m_contiguousSelectionMode {FloodFill};",
        """    void selectAt(const QPoint &pos, SelectionAction action, int threshold);
#ifdef Q_OS_ANDROID
    void clearMobileTolerance();
    bool m_mobileDragTolerance=false;
    int m_mobileInitialTolerance=8;
    int m_mobileStartX=0;
    QPoint m_mobileSeed;
    KisNodeSP m_mobileTarget;
    SelectionAction m_mobileSelectionAction=SELECTION_REPLACE;
    QPointer<MobileFillTolerancePreview> m_mobilePreview;
#endif
    ContiguousSelectionMode m_contiguousSelectionMode {FloodFill};""",
        "capture native selection action and target at press")
    select=one(select,"class MobileFillTolerancePreview;",
        "#include <QPointer>\nclass MobileFillTolerancePreview;","guard preview widget lifetime")
    select_header.write_text(select,encoding="utf-8")

    select_source=select_dir/"kis_tool_select_contiguous.cc"
    select=select_source.read_text(encoding="utf-8")
    select=one(select,'#include "kis_tool_select_contiguous.h"',
        '''#include "kis_tool_select_contiguous.h"
#ifdef Q_OS_ANDROID
#include "../basictools/fill_tolerance_preview.h"
#include <KisView.h>
#include <kis_selection.h>
#endif''',"reuse the native Fill preview engine")
    select=one(select,"KisToolSelectContiguous::~KisToolSelectContiguous()\n{\n}",
        """KisToolSelectContiguous::~KisToolSelectContiguous()
{
#ifdef Q_OS_ANDROID
    delete m_mobilePreview.data();
#endif
}""","release the selection preview")
    select=one(select,"void KisToolSelectContiguous::deactivate()\n{",
        """void KisToolSelectContiguous::deactivate()
{
#ifdef Q_OS_ANDROID
    clearMobileTolerance();
#endif""","cancel an unfinished contiguous selection when switching tools")
    select=one(select,"    beginSelectInteraction();\n\n    KisCursorOverrideLock",
        """    beginSelectInteraction();
    const QPoint pos = convertToImagePixelCoordFloored(event);
#ifdef Q_OS_ANDROID
    if (m_mobileDragTolerance && currentImage()->bounds().contains(pos)) {
        m_mobileTarget=currentNode();
        m_mobileSelectionAction=selectionAction();
        m_mobileSeed=pos;
        m_mobileInitialTolerance=m_threshold;
        m_mobileStartX=event->pos().x();
        if (auto *kisCanvas=dynamic_cast<KisCanvas2*>(canvas())) {
            const auto selection=kisCanvas->imageView()?kisCanvas->imageView()->selection():KisSelectionSP();
            const auto reference=sampleLayersMode()==SampleCurrentLayer?dev:
                sampleLayersMode()==SampleAllLayers?currentImage()->projection():KisPaintDeviceSP();
            MobileFillTolerancePreview::Options options;
            options.spread=m_opacitySpread;
            options.closeGap=m_closeGap;
            options.boundaryFill=m_contiguousSelectionMode==BoundaryFill;
            options.boundaryColor=m_contiguousSelectionBoundaryColor;
            options.selectionBoundary=m_useSelectionAsBoundary;
            options.antiAlias=antiAliasSelection();
            options.grow=growSelection();
            options.feather=featherSelection();
            options.stopGrowingAtDarkest=stopGrowingAtDarkestPixel();
            m_mobilePreview=new MobileFillTolerancePreview(kisCanvas,currentImage(),reference,
                selection?selection->pixelSelection():KisPixelSelectionSP(),pos,options);
            m_mobilePreview->setThreshold(m_threshold);
        }
        return;
    }
#endif
    selectAt(pos,selectionAction(),m_threshold);
}

void KisToolSelectContiguous::selectAt(const QPoint &pos,SelectionAction action,int threshold)
{
    const auto dev=currentNode()->projection();
    KisCursorOverrideLock""","share the native contiguous selection transaction")
    select=one(select,"    QPoint pos = convertToImagePixelCoordFloored(event);\n","",
               "use the original seed rather than the release position")
    select=one(select,"    int threshold = m_threshold;\n","",
               "feed gesture tolerance into the native command")
    select=one(select,"helper.selectPixelSelection(applicator, selection, selectionAction());",
               "helper.selectPixelSelection(applicator, selection, action);","keep the native combine action")
    select=one(select,"void KisToolSelectContiguous::endPrimaryAction(KoPointerEvent *event)\n{",
        """#ifdef Q_OS_ANDROID
void KisToolSelectContiguous::continuePrimaryAction(KoPointerEvent *event)
{
    if (mobileGesturePending()) {
        if (m_mobilePreview) m_mobilePreview->setThreshold(
            mobileFillTolerance(m_mobileInitialTolerance,event->pos().x()-m_mobileStartX));
        return;
    }
    KisToolSelectBase::continuePrimaryAction(event);
}

void KisToolSelectContiguous::clearMobileTolerance()
{
    if (!mobileGesturePending()) return;
    m_mobileTarget.clear();
    delete m_mobilePreview.data();
    endSelectInteraction();
}

void KisToolSelectContiguous::requestStrokeEnd()
{
    clearMobileTolerance();
    KisToolSelectBase::requestStrokeEnd();
}

void KisToolSelectContiguous::requestStrokeCancellation()
{
    clearMobileTolerance();
    KisToolSelectBase::requestStrokeCancellation();
}
#endif

void KisToolSelectContiguous::endPrimaryAction(KoPointerEvent *event)
{
#ifdef Q_OS_ANDROID
    if (mobileGesturePending()) {
        const auto target=m_mobileTarget;
        if (event && currentNode()==target && target->projection() && selectionEditable()) {
            const int threshold=mobileFillTolerance(m_mobileInitialTolerance,event->pos().x()-m_mobileStartX);
            selectAt(m_mobileSeed,m_mobileSelectionAction,threshold);
        }
        clearMobileTolerance();
        return;
    }
#endif""","apply exactly one native contiguous selection on release")
    select=one(select,'    sectionSelectionExtent->appendWidget("sliderThreshold", sliderThreshold);',
        """    sectionSelectionExtent->appendWidget("sliderThreshold", sliderThreshold);
#ifdef Q_OS_ANDROID
    auto *dragTolerance=new QCheckBox(i18n("Adjust tolerance by dragging"));
    m_mobileDragTolerance=m_configGroup.readEntry("mobileDragTolerance",false);
    dragTolerance->setChecked(m_mobileDragTolerance);
    sectionSelectionExtent->appendWidget("mobileSelectDragTolerance",dragTolerance);
    connect(dragTolerance,&QCheckBox::toggled,this,[this](bool enabled) {
        m_mobileDragTolerance=enabled;
        m_configGroup.writeEntry("mobileDragTolerance",enabled);
    });
#endif""","expose a separate contiguous selection preference")
    select_source.write_text(select,encoding="utf-8")

    tool_proxy_file=root/"libs/ui/canvas/kis_tool_proxy.cpp"
    tool_proxy=tool_proxy_file.read_text(encoding="utf-8")
    tool_proxy=one(tool_proxy,
        "    QTouchEvent *touchEvent = dynamic_cast<QTouchEvent *> (event);\n\n    if (tabletEvent)",
        """    QTouchEvent *touchEvent = dynamic_cast<QTouchEvent *> (event);
#ifdef Q_OS_ANDROID
    if (state == END && priv()->activeTool &&
        priv()->activeTool->property("mobileGesturePending").toBool() &&
        (!event || (event->type() != QEvent::TouchEnd &&
                    event->type() != QEvent::TabletRelease &&
                    event->type() != QEvent::MouseButtonRelease) ||
         (touchEvent && touchEvent->touchPoints().isEmpty()))) {
        requestStrokeCancellation();
        Q_EMIT toolPrimaryActionActivated(false);
        return true;
    }
    if (touchEvent && touchEvent->touchPoints().isEmpty()) return true;
#endif

    if (tabletEvent)""","guard empty Android touch events and cancel interrupted tool gestures")
    tool_proxy=one(tool_proxy,'#include "kis_tool_proxy.h"',
        '#include "kis_tool_proxy.h"\n#include "kis_canvas_controller.h"',
        "share the latest tablet hover point with the mirror controller")
    tool_proxy=one(tool_proxy,
        "        this->tabletEvent(tabletEvent, docPoint);\n        return;",
        """#ifdef Q_OS_ANDROID
        if (auto *controller=dynamic_cast<KisCanvasController*>(canvas()->canvasController()))
            controller->mobileRememberPenPoint(docPoint);
#endif
        this->tabletEvent(tabletEvent, docPoint);
        return;""","remember the pen point even before the next stroke")
    tool_proxy_file.write_text(tool_proxy,encoding="utf-8")

    assistant_dir=root/"plugins/assistants/Assistants"
    (assistant_dir/"perspective_placement.h").write_text(
        (own/"mobile_core/perspective_placement.h").read_text(encoding="utf-8"),
        encoding="utf-8")
    assistant_header=assistant_dir/"kis_assistant_tool.h"
    assistant=assistant_header.read_text(encoding="utf-8")
    assistant=one(assistant,"#include <QPointer>","#include <QPointer>\n#include <QVector>",
                  "mobile perspective points")
    assistant=one(assistant,"    Q_OBJECT\n    enum PerspectiveAssistantEditionMode",
        """    Q_OBJECT
#ifdef Q_OS_ANDROID
    Q_PROPERTY(bool mobilePerspectivePlacement READ mobilePerspectivePlacement)
    Q_PROPERTY(bool mobileGesturePending READ mobilePerspectivePlacement)
#endif
    enum PerspectiveAssistantEditionMode""","route cancellation while placing vanishing points")
    assistant=one(assistant,"    void beginPrimaryAction(KoPointerEvent *event) override;",
        """    void beginPrimaryAction(KoPointerEvent *event) override;
#ifdef Q_OS_ANDROID
    Q_INVOKABLE void mobileToggleThreePointPlacement();
    bool mobilePerspectivePlacement() const {return m_mobilePerspectivePlacement;}
    void requestStrokeCancellation() override;
    void requestStrokeEnd() override;
#endif""","expose three-point placement on the mobile tool strip")
    assistant=one(assistant,"private:\n    // adds and removes assistant.",
        """private:
#ifdef Q_OS_ANDROID
    void mobileCancelThreePointPlacement();
    void mobilePlaceVanishingPoint(KoPointerEvent *event);
    bool m_mobilePerspectivePlacement=false;
    QVector<QPointF> m_mobileVanishingPoints;
#endif
    // adds and removes assistant.""","store pending guide points until the set is complete")
    assistant_header.write_text(assistant,encoding="utf-8")
    assistant_source=assistant_dir/"kis_assistant_tool.cc"
    assistant=assistant_source.read_text(encoding="utf-8")
    assistant=one(assistant,'#include <kis_assistant_tool.h>',
        '''#include <kis_assistant_tool.h>
#ifdef Q_OS_ANDROID
#include "perspective_placement.h"
#endif''',"native assistant placement helpers")
    assistant=one(assistant,"void KisAssistantTool::deactivate()\n{",
        """void KisAssistantTool::deactivate()
{
#ifdef Q_OS_ANDROID
    mobileCancelThreePointPlacement();
#endif""","discard incomplete guide sets when switching tools")
    assistant=one(assistant,"            m_options.availableAssistantsComboBox->addItem(id.name(), id.id());",
        """            QString label=id.name();
#ifdef Q_OS_ANDROID
            if (id.id()=="vanishing point") label=i18n("1-point perspective (Vanishing Point)");
            if (id.id()=="two point") label=i18n("2-point perspective");
#endif
            m_options.availableAssistantsComboBox->addItem(label, id.id());""","label native one- and two-point workflows explicitly")
    assistant=one(assistant,"    // show special display while a new assistant is in the process of being created",
        """#ifdef Q_OS_ANDROID
    if (m_mobilePerspectivePlacement) {
        _gc.save();
        _gc.setPen(QPen(QColor(63,198,255),2));
        _gc.setBrush(QColor(37,37,41,230));
        for (int i=0;i<m_mobileVanishingPoints.size();++i) {
            const QPointF center=_converter.documentToView(m_mobileVanishingPoints.at(i));
            _gc.drawEllipse(center,14,14);
            _gc.drawText(QRectF(center-QPointF(14,14),QSizeF(28,28)),
                         Qt::AlignCenter,QString::number(i+1));
        }
        _gc.restore();
    }
#endif

    // show special display while a new assistant is in the process of being created""",
        "draw numbered markers for uncommitted vanishing points")
    assistant=one(assistant,"void KisAssistantTool::beginPrimaryAction(KoPointerEvent *event)\n{",
        """#ifdef Q_OS_ANDROID
void KisAssistantTool::mobileToggleThreePointPlacement()
{
    if (m_mobilePerspectivePlacement) {
        mobileCancelThreePointPlacement();
        return;
    }
    if (mode()!=HOVER_MODE) return;
    m_newAssistant.clear();
    m_mobilePerspectivePlacement=true;
    m_canvas->viewManager()->showFloatingMessage(
        i18n("Place the first and second vanishing points on the horizon, then the third above or below."),
        QIcon(),3500,KisFloatingMessage::Medium);
    m_canvas->updateCanvas();
}

void KisAssistantTool::mobileCancelThreePointPlacement()
{
    if (!m_mobilePerspectivePlacement) return;
    m_mobilePerspectivePlacement=false;
    m_mobileVanishingPoints.clear();
    setMode(HOVER_MODE);
    m_canvas->updateCanvas();
}

void KisAssistantTool::requestStrokeCancellation()
{
    mobileCancelThreePointPlacement();
    KisTool::requestStrokeCancellation();
}

void KisAssistantTool::requestStrokeEnd()
{
    mobileCancelThreePointPlacement();
    KisTool::requestStrokeEnd();
}

void KisAssistantTool::mobilePlaceVanishingPoint(KoPointerEvent *event)
{
    setMode(HOVER_MODE);
    if (!event) {
        mobileCancelThreePointPlacement();
        return;
    }
    const auto transform=m_canvas->coordinatesConverter()->documentToWidgetTransform();
    QVector<QPointF> placed;
    for (const auto &point:m_mobileVanishingPoints) placed.append(transform.map(point));
    if (!mobileCanPlaceVanishingPoint(placed,transform.map(event->point))) {
        m_canvas->viewManager()->showFloatingMessage(
            i18n("Place the vanishing point farther from the others and off the horizon."),
            QIcon(),2000,KisFloatingMessage::Medium);
        return;
    }
    m_mobileVanishingPoints.append(event->point);
    if (m_mobileVanishingPoints.size()<3) {
        m_canvas->updateCanvas();
        return;
    }

    auto *factory=KisPaintingAssistantFactoryRegistry::instance()->get("vanishing point");
    if (factory) {
        auto decoration=m_canvas->paintingAssistantsDecoration();
        const auto before=KisPaintingAssistant::cloneAssistantList(decoration->assistants());
        for (const QPointF &point:m_mobileVanishingPoints) {
            auto guide=toQShared(factory->createPaintingAssistant());
            guide->setLocal(false);
            guide->addHandle(new KisPaintingAssistantHandle(point),HandleType::NORMAL);
            decoration->addAssistant(guide);
        }
        const auto after=KisPaintingAssistant::cloneAssistantList(decoration->assistants());
        m_canvas->viewManager()->undoAdapter()->addCommand(new EditAssistantsCommand(
            m_canvas,before,after,EditAssistantsCommand::ADD,before.size()));
        m_handles=decoration->handles();
        updateToolOptionsUI();
    }
    mobileCancelThreePointPlacement();
}
#endif

void KisAssistantTool::beginPrimaryAction(KoPointerEvent *event)
{
#ifdef Q_OS_ANDROID
    if (m_mobilePerspectivePlacement) {
        setMode(PAINT_MODE);
        return;
    }
#endif""","compose three native Vanishing Point assistants in one undo command")
    assistant=one(assistant,"void KisAssistantTool::continuePrimaryAction(KoPointerEvent *event)\n{",
        """void KisAssistantTool::continuePrimaryAction(KoPointerEvent *event)
{
#ifdef Q_OS_ANDROID
    if (m_mobilePerspectivePlacement) return;
#endif""","defer guide placement to release")
    assistant=one(assistant,"void KisAssistantTool::endPrimaryAction(KoPointerEvent *event)\n{",
        """void KisAssistantTool::endPrimaryAction(KoPointerEvent *event)
{
#ifdef Q_OS_ANDROID
    if (m_mobilePerspectivePlacement) {
        mobilePlaceVanishingPoint(event);
        return;
    }
#endif""","place a vanishing point on release")
    assistant_source.write_text(assistant,encoding="utf-8")

    transform_dir=root/"plugins/tools/tool_transform2"
    transform_header=transform_dir/"kis_tool_transform.h"
    transform=transform_header.read_text(encoding="utf-8")
    transform=one(transform,"#include <QPointer>","#include <QPointer>\n#include <optional>",
                  "pending Transform mode include")
    transform=one(transform,"    KisStrokeId m_strokeId;",
                  "    KisStrokeId m_strokeId;\n    std::optional<ToolTransformArgs::TransformMode> m_pendingTransformMode;",
                  "pending Transform mode field")
    transform=one(transform,"    void setNextActivationTransformMode(TransformToolMode mode);",
                  """    void setNextActivationTransformMode(TransformToolMode mode);
    Q_INVOKABLE bool mobileCanRetargetLayers() const;
    Q_INVOKABLE void mobileRetargetLayers();""",
                  "Studio Transform retarget API")
    transform=one(transform,"    KisStrokeId m_strokeId;",
                  """    ToolTransformArgs::TransformMode m_mobileStrokeMode = ToolTransformArgs::FREE_TRANSFORM;
    bool m_mobileTransformHasExternalSource = false;
    KisStrokeId m_strokeId;""","remember Transform mode and source during stroke initialization")
    transform_header.write_text(transform,encoding="utf-8")

    transform_source=transform_dir/"kis_tool_transform.cc"
    transform=transform_source.read_text(encoding="utf-8")
    transform=one(transform,"    if( mode != m_currentArgs.mode() ) {",
        "    if (m_optionsWidget && (mode != m_currentArgs.mode() || m_pendingTransformMode)) {",
        "allow replacing a queued Transform mode")
    transform=one(transform,
        "    ToolTransformArgs *config = m_transaction.currentConfig();\n    const ToolTransformArgs::TransformMode previousMode",
        """    m_mobileStrokeMode = mode;
    // Initial stroke setup runs on the image worker. Its result would
    // replace any mode written before the transaction has arrived.
    if (m_strokeId && m_transaction.rootNodes().isEmpty()) {
        m_pendingTransformMode = mode;
        return;
    }
    ToolTransformArgs *config = m_transaction.currentConfig();
    const ToolTransformArgs::TransformMode previousMode""",
        "defer Transform mode changes while the stroke starts")
    transform=one(transform,"    initGuiAfterTransformMode();\n\n    if (m_transaction.hasInvisibleNodes())",
        """    initGuiAfterTransformMode();

    const auto requestedMode = m_pendingTransformMode;
    m_pendingTransformMode.reset();
    if (requestedMode && *requestedMode != m_currentArgs.mode()) {
        slotResetTransform(*requestedMode);
    }

    if (m_transaction.hasInvisibleNodes())""",
        "apply latest mode to the initialized Transform stroke")
    for method in ("endStroke","cancelStroke"):
        anchor=f"void KisToolTransform::{method}()\n{{"
        transform=one(transform,anchor,anchor+"\n    m_pendingTransformMode.reset();",
                      f"clear queued Transform mode on {method}")
    transform=one(transform,
        "void KisToolTransform::startStroke(ToolTransformArgs::TransformMode mode, bool forceReset)\n{",
        """void KisToolTransform::startStroke(ToolTransformArgs::TransformMode mode, bool forceReset)
{
    m_mobileStrokeMode = mode;
    m_mobileTransformHasExternalSource = bool(m_externalSourceForNextActivation);""",
        "capture initial Transform mode and external source for retarget")
    transform=one(transform,"void KisToolTransform::slotRestartTransform()",
        """bool KisToolTransform::mobileCanRetargetLayers() const
{
    if (mode() != HOVER_MODE) return false;
    if (!m_strokeId) return true;
    if (m_mobileTransformHasExternalSource) return false;

    // Krita can reopen the last committed transform as the starting
    // preview. Compare against that baseline before checking identity.
    const auto *continued = m_currentArgs.continuedTransform();
    return continued ? m_currentArgs == *continued : m_currentArgs.isUnchanging();
}

void KisToolTransform::mobileRetargetLayers()
{
    if (!mobileCanRetargetLayers()) return;
    const auto requestedMode = m_pendingTransformMode.value_or(
        m_transaction.rootNodes().isEmpty() ? m_mobileStrokeMode : m_currentArgs.mode());
    cancelStroke();
    if (!selectedNodes().isEmpty()) startStroke(requestedMode, true);
}

void KisToolTransform::slotRestartTransform()""","retarget only an untouched Transform stroke")
    transform=one(transform,
        "m_optionsWidget->setApplyResetDisabled(m_currentArgs.isIdentity());",
        "m_optionsWidget->setApplyResetDisabled(m_currentArgs.isUnchanging());",
        "allow Apply for externally supplied pixels even at identity")
    transform_source.write_text(transform,encoding="utf-8")
    node_manager_header=root/"libs/ui/kis_node_manager.h"
    node_manager=one(node_manager_header.read_text(encoding="utf-8"),
        "    void slotNonUiActivatedNode(KisNodeSP node);",
        """    void slotNonUiActivatedNode(KisNodeSP node);
#ifdef Q_OS_ANDROID
    void mobileActivateGlobalSelection(KisNodeSP node);
#endif""","declare native selection mask activation for Studio")
    node_manager=one(node_manager,
        "    void slotSomethingActivatedNodeImpl(KisNodeSP node);",
        "    void slotSomethingActivatedNodeImpl(KisNodeSP node, bool allowGlobalSelection = false);",
        "allow explicit activation of the hidden global selection")
    node_manager_header.write_text(node_manager,encoding="utf-8")

    node_manager_file=root/"libs/ui/kis_node_manager.cpp"
    node_manager=one(node_manager_file.read_text(encoding="utf-8"),
        "void KisNodeManager::slotSomethingActivatedNodeImpl(KisNodeSP node)",
        """#ifdef Q_OS_ANDROID
void KisNodeManager::mobileActivateGlobalSelection(KisNodeSP node)
{
    if (!node || !node->graphListener() ||
        !isNodeHidden(node, true) || isNodeHidden(node, false)) return;
    if (node != activeNode()) slotSomethingActivatedNodeImpl(node, true);
}
#endif

void KisNodeManager::slotSomethingActivatedNodeImpl(KisNodeSP node, bool allowGlobalSelection)""",
        "activate the global mask without changing layer display preferences")
    node_manager=one(node_manager,
        "    const bool nodeVisible = !isNodeHidden(node, !m_d->nodeDisplayModeAdapter->showGlobalSelectionMask());",
        """    const bool hideGlobalSelection =
        !allowGlobalSelection && !m_d->nodeDisplayModeAdapter->showGlobalSelectionMask();
    const bool nodeVisible = !isNodeHidden(node, hideGlobalSelection);""",
        "keep native hidden-node checks for other activations")
    node_manager_file.write_text(node_manager,encoding="utf-8")

    selection_file=root/"libs/ui/kis_selection_manager.cc"
    selection=one(selection_file.read_text(encoding="utf-8"),
        """    KisAction *action = m_view->actionManager()->actionByName("show-global-selection-mask");
    KIS_SAFE_ASSERT_RECOVER_RETURN(action);

    if (!action->isChecked()) {
        action->setChecked(true);
        Q_EMIT action->toggled(true);
        Q_EMIT action->triggered(true);
    }

    KisNodeSP node = selection->parentNode();
    KIS_SAFE_ASSERT_RECOVER_RETURN(node);

    m_view->nodeManager()->slotNonUiActivatedNode(node);""",
        """#ifdef Q_OS_ANDROID
    if (!m_view->mainWindow()) {
        KisNodeSP node = selection->parentNode();
        if (!node) return;

        auto *tools = KoToolManager::instance();
        const QString activeId = tools->activeToolId();
        KoToolBase *transform = nullptr;
        auto *nodes = m_view->nodeManager();
        if (activeId.section(QLatin1Char('/'), -1) == QStringLiteral("KisToolTransform") &&
            (node != nodes->activeNode() || nodes->selectedNodes() != KisNodeList{node})) {
            transform = tools->toolById(m_view->canvasBase(), activeId);
            bool canRetarget = false;
            if (!transform ||
                !QMetaObject::invokeMethod(transform, "mobileCanRetargetLayers",
                                           Qt::DirectConnection, Q_RETURN_ARG(bool, canRetarget)) ||
                !canRetarget) {
                m_view->showFloatingMessage(
                    i18n("Apply or reset the current transform before editing the selection."),
                    QIcon(), 3000, KisFloatingMessage::Medium);
                return;
            }
        }

        if (KisNodeManager::isNodeHidden(node, true)) {
            nodes->mobileActivateGlobalSelection(node);
        } else {
            nodes->slotNonUiActivatedNode(node);
        }
        if (nodes->activeNode() != node) return;
        nodes->slotSetSelectedNodes(KisNodeList{node});
        if (transform && !selection->hasShapeSelection()) {
            QMetaObject::invokeMethod(transform, "mobileRetargetLayers", Qt::DirectConnection);
            return;
        }
    } else
#endif
    {
        KisAction *action = m_view->actionManager()->actionByName("show-global-selection-mask");
        KIS_SAFE_ASSERT_RECOVER_RETURN(action);

        if (!action->isChecked()) {
            action->setChecked(true);
            Q_EMIT action->toggled(true);
            Q_EMIT action->triggered(true);
        }

        KisNodeSP node = selection->parentNode();
        KIS_SAFE_ASSERT_RECOVER_RETURN(node);

        m_view->nodeManager()->slotNonUiActivatedNode(node);
    }""","retain the desktop selection edit sequence")
    selection=one(selection,"#include <KoToolManager.h>",
        "#include <KoToolManager.h>\n#include <KoToolBase.h>",
        "use the native Transform tool to retarget the selection")
    selection_file.write_text(selection,encoding="utf-8")

    draft_header_file=root/"libs/ui/KisDocument.h"
    draft_header=draft_header_file.read_text(encoding="utf-8")
    draft_header=one(draft_header,"    void sigCompleteBackgroundSaving(",
        "    void sigMobileDraftPublished(const QString &draft,const QString &project);\n"
        "    void sigCompleteBackgroundSaving(","notify Studio after publishing a checkpoint")
    draft_header_file.write_text(draft_header,encoding="utf-8")

    draft_document_file=root/"libs/ui/KisDocument.cpp"
    draft_document=draft_document_file.read_text(encoding="utf-8")
    draft_document=one(draft_document,"#include <QFileInfo>",
        "#include <QFileInfo>\n#include \"mobile_draft_recovery.h\"","draft recovery include")
    draft_document=one(draft_document,
        "    if (!d->modified || !d->modifiedAfterAutosave) return;\n    const QString autoSaveFileName = generateAutoSaveFileName(localFilePath());",
        """    if (!d->modified || !d->modifiedAfterAutosave) return;
    const QString draftPath = property("_mobileDraftPath").toString();
    if (!draftPath.isEmpty()) {
        if (d->documentIsClosing || isSaving()) return;
        const bool nativeProject = mimeType() == nativeFormatMimeType() ||
                                   mimeType() == "application/x-krita-archive";
        if (!MobileDraftRecovery::setProject(draftPath,nativeProject ? path() : QString())) {
            setEmergencyAutoSaveInterval();
            return;
        }
    }
    const QString autoSaveFileName = draftPath.isEmpty()
        ? generateAutoSaveFileName(localFilePath())
        : draftPath + QStringLiteral(".pending.kra");""",
        "write Studio autosave to a private staging file")
    draft_document=one(draft_document,
        "    Q_UNUSED(job);\n    Q_UNUSED(warningMessage);\n\n    const QString fileName = QFileInfo(job.filePath).fileName();",
        """    Q_UNUSED(warningMessage);

    const QString draftPath = property("_mobileDraftPath").toString();
    if (!draftPath.isEmpty() && job.filePath == draftPath + QStringLiteral(".pending.kra")) {
        if (!MobileDraftRecovery::publish(draftPath,status.isOk())) {
            d->modifiedAfterAutosave = true;
            if (status.isOk()) {
                setEmergencyAutoSaveInterval();
                qWarning() << "Could not commit Studio draft";
                return;
            }
        } else {
            Q_EMIT sigMobileDraftPublished(draftPath,MobileDraftRecovery::projectFor(draftPath));
        }
    }

    const QString fileName = QFileInfo(job.filePath).fileName();""",
        "atomically publish a completed private draft")
    draft_document=one(draft_document,
        "    // Eliminate any auto-save file\n    QString asf = generateAutoSaveFileName(autosaveBaseName);",
        """    const QString draftPath = property("_mobileDraftPath").toString();
    if (!draftPath.isEmpty()) {
        QFile::remove(draftPath);
        QFile::remove(draftPath + QStringLiteral(".pending.kra"));
        QFile::remove(draftPath + QStringLiteral(".project"));
        QFile::remove(draftPath + QStringLiteral(".title"));
    }
    // Eliminate any auto-save file
    QString asf = generateAutoSaveFileName(autosaveBaseName);""",
        "clear private draft on native Save or Discard")
    draft_document_file.write_text(draft_document,encoding="utf-8")
    print(f"Installed independent Mobile Studio app; XMLGUI checksum {digest}")

if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: apply_mobile_shell.py KRITA_SOURCE")
    main(Path(sys.argv[1]))
