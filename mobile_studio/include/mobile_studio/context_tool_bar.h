/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Compact controls delegate to the active tool's native widgets or actions.
 * The full Tool Options panel remains the only owner of the native layout.
 */
#pragma once
#include <algorithm>
#ifdef Q_OS_ANDROID
#include <QAbstractButton>
#include <KoToolBase.h>
#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMetaObject>
#include <QMetaEnum>
#include <QDebug>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QScrollBar>
#include <QMouseEvent>
#include "touch_scroll.h"
#include "touch_action_picker.h"
#include <QSet>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QStyle>
#include <QVBoxLayout>
#include <functional>
#include <initializer_list>
#include <type_traits>
#include <KisOptionCollectionWidget.h>
#include <KisOptionButtonStrip.h>
#include <KoGroupButton.h>
#include <kis_slider_spin_box.h>
#include <KisDocument.h>
#include <KisView.h>
#include <KisReferenceImage.h>
#include <KisReferenceImagesLayer.h>
#include <KisReferenceImagesDecoration.h>
#include <KoSelection.h>
#include <KoShapeManager.h>
#include <kis_canvas2.h>
#include <kis_coordinates_converter.h>

#include "engine_bridge.h"

class MobileContextToolBar final : public QFrame
{
public:
    explicit MobileContextToolBar(QWidget *parent,QAction *options) : QFrame(parent)
    {
        setObjectName(QStringLiteral("mobileContextToolBar"));
        setStyleSheet(QStringLiteral(
            "QFrame#mobileContextToolBar{background:rgba(26,29,34,210);"
            "border:0;border-radius:12px;}"
            "QWidget#mobileContextControls,QScrollArea{background:transparent;border:0;}"
            "QToolButton{color:#dadce5;background:#303137;border:0;"
            "font-size:11px;border-radius:8px;padding:0 3px;}"
            "QToolButton#mobileContextCancel{color:#f18c97;background:#4c292f;}"
            "QToolButton#mobileContextDone{color:#182232;background:#8da7d9;}"
            "QToolButton#mobileContextDone:disabled{color:#667080;background:#4d5667;}"
            "QToolButton:checked,QToolButton:pressed{background:#536278;}"
            "QToolButton:disabled{color:#98989f;background:#2c2c31;}"
            "QScrollBar:horizontal{height:5px;background:#252529;}"
            "QScrollBar::handle:horizontal{background:#70707a;min-width:20px;}"
            "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}"));
        auto *layout=new QHBoxLayout(this);
        layout->setContentsMargins(2,2,2,2);
        layout->setSpacing(2);
        m_tool=new QToolButton(this);
        m_tool->setFixedHeight(38);
        m_tool->setToolTip(QObject::tr("Choose a tool"));
        connect(m_tool,&QToolButton::clicked,this,[this]() {
            if(chooseTool)chooseTool(m_tool);
        });
        layout->addWidget(m_tool);
        m_scroll=new QScrollArea(this);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setWidgetResizable(true);
        m_scroll->viewport()->installEventFilter(this);
        m_scroll->setMinimumWidth(0);
        m_scroll->setFixedHeight(38);
        // A single compact row may pan horizontally, never vertically.
        // In particular, ignore touch drag attempts in the cross axis.
        m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        MobileTouchScroll::configure(m_scroll);
        layout->addWidget(m_scroll,1);
        // Restore the native-options shortcut as requested. The active
        // tool chip remains the separate All Tools picker.
        if(options) {
            auto *gear=new QToolButton(this);
            gear->setDefaultAction(options);
            gear->setToolButtonStyle(Qt::ToolButtonIconOnly);
            gear->setIconSize(QSize(20,20));
            gear->setFixedSize(36,36);
            gear->setAccessibleName(QObject::tr("Full Krita Tool Options"));
            gear->setToolTip(gear->accessibleName());
            layout->addWidget(gear);
        }
        m_sync=new QTimer(this);
        // Native transform updates can be expensive; never poll the entire
        // options widget tree at nearly six times per second.
        m_sync->setInterval(400);
        connect(m_sync,&QTimer::timeout,this,[this]() {
            if(isVisible())refreshControls();
        });
    }

    QWidget *toolButton() const {return m_tool;}

    std::function<void(QWidget *)> chooseTool;
    std::function<void(QWidget *)> openTextFormatting;
    std::function<void(bool,QWidget *)> chooseFillResource;

    void setTool(const QString &id,const QString &label,
                 const QList<QPointer<QWidget>> &options,
                 const MobileEngineBridge &engine,KoToolBase *nativeTool)
    {
        if(m_id==id && m_options==options && m_nativeTool==nativeTool) {
            // No need to walk all native settings on every workspace sync;
            // the visibility-scoped timer updates changing controls.
            return;
        }
        m_id=id;
        m_options=options;
        m_nativeTool=nativeTool;
        ++m_generation;
        m_refresh.clear();
        m_used.clear();
        const QString shortId=id.section(QLatin1Char('/'),-1);
        const QString shortLabel=shortId==QStringLiteral("KisToolSelectContiguous")?
            QObject::tr("Wand"):shortId==QStringLiteral("KisToolSelectOutline")?
            QObject::tr("Lasso"):shortId==QStringLiteral("KisToolSelectRectangular")?
            QObject::tr("Rect"):shortId==QStringLiteral("KisToolSelectElliptical")?
            QObject::tr("Ellipse"):label;
        setChipText(m_tool,shortLabel,92);
        m_tool->setAccessibleName(QObject::tr("Current tool: %1. Choose a tool").arg(label));

        auto *content=new QWidget;
        content->setObjectName(QStringLiteral("mobileContextControls"));
        m_row=new QHBoxLayout(content);
        m_row->setContentsMargins(0,0,0,0);
        m_row->setSpacing(2);
        content->setFixedHeight(38);
        // Never let QScrollArea compress the children to its viewport.
        // Qt's tool-button minimum size otherwise permits clipped text.
        m_row->setSizeConstraint(QLayout::SetMinimumSize);
        const auto named=[this](const char *name,const QString &caption) {
            addOption(findOption(name),caption);
        };
        const auto native=[this,&engine](const char *name,const QString &caption) {
            if(auto *action=engine.action(name))addAction(action,caption);
        };
        const auto nativeMenu=[this,&engine](
            const QString &caption,std::initializer_list<Named> entries) {
            QList<ActionChoice> choices;
            for(const auto &entry:entries)
                if(auto *action=engine.action(entry.name))
                    choices.append({entry.label,action});
            addActionMenu(caption,choices);
        };
        const auto addFillResource=[this](bool gradient) {
            if(!chooseFillResource)return;
            auto *control=button(gradient?QObject::tr("Gradient"):QObject::tr("Pattern"));
            connect(control,&QToolButton::clicked,this,[this,gradient,control]() {
                chooseFillResource(gradient,control);
            });
        };
        // Shape tools may register with their plugin prefix, unlike the
        // selection tools. Both forms use Krita's native stroke completion.
        const bool finishablePath=shortId==QStringLiteral("KisToolPolygon") ||
            shortId==QStringLiteral("KisToolPolyline") ||
            shortId==QStringLiteral("KisToolPath") ||
            shortId==QStringLiteral("KisToolBezierCurve") ||
            shortId==QStringLiteral("KisToolSelectPolygonal") ||
            shortId==QStringLiteral("KisToolSelectPath") ||
            shortId==QStringLiteral("KisToolSelectMagnetic");
        // Finish/Cancel and native Apply are appended after ordinary
        // settings so they remain adjacent at the right end of the row.
        if(shortId.startsWith(QStringLiteral("KisToolSelect"))) {
            addSectionChoices("sectionAction",QObject::tr("Combine"),{
                QObject::tr("Replace"),QObject::tr("Intersect"),QObject::tr("Add"),
                QObject::tr("Subtract"),QObject::tr("Symmetric difference")
            });
            native("deselect",QObject::tr("Deselect"));
            named("sliderThreshold",QObject::tr("Tolerance"));
            named("mobileSelectDragTolerance",QObject::tr("Drag tolerance"));
            named("sliderCloseGap",QObject::tr("Close gap"));
            if(shortId==QStringLiteral("KisToolSelectMagnetic"))
                named("radius",QObject::tr("Radius"));
            addSectionChoices("sectionMode",QObject::tr("Type"),
                              {QObject::tr("Pixel"),QObject::tr("Vector")});
            // Keep everyday selection controls visible; preserve the full
            // native command set in a single touch-friendly menu.
            nativeMenu(QObject::tr("Selection"),{
                {"select_all",QObject::tr("Select all")},
                {"reselect",QObject::tr("Reselect")},
                {"invert_selection",QObject::tr("Invert")},
                {"edit_selection",QObject::tr("Edit selection")},
                {"fill_selection_foreground_color",QObject::tr("Fill foreground")},
                {"copy_selection_to_new_layer",QObject::tr("Copy to new layer")}
            });
            nativeMenu(QObject::tr("Edges"),{
                {"growselection",QObject::tr("Grow")},
                {"shrinkselection",QObject::tr("Shrink")},
                {"featherselection",QObject::tr("Feather")},
                {"borderselection",QObject::tr("Border")},
                {"smoothselection",QObject::tr("Smooth")}
            });
            nativeMenu(QObject::tr("Convert and copy"),{
                {"convert_to_vector_selection",QObject::tr("Vector selection")},
                {"convert_to_raster_selection",QObject::tr("Raster selection")},
                {"colorrange",QObject::tr("Color range")},
                {"copy_merged",QObject::tr("Copy merged")},
                {"copy_selection_to_new_layer",QObject::tr("Copy to layer")},
                {"cut_selection_to_new_layer",QObject::tr("Cut to layer")}
            });
        } else if(shortId==QStringLiteral("KisToolBrush")) {
            // Use Krita's actual freehand smoothing actions. Selecting a
            // mode changes its native paint tool configuration, not the
            // mobile brush engine or the drawing input stream.
            nativeMenu(QObject::tr("Smoothing"),{
                {"set_no_brush_smoothing",QObject::tr("None")},
                {"set_simple_brush_smoothing",QObject::tr("Basic")},
                {"set_weighted_brush_smoothing",QObject::tr("Weighted")},
                {"set_stabilizer_brush_smoothing",QObject::tr("Stabilizer")},
                {"set_pixel_perfect_smoothing",QObject::tr("Pixel perfect")}
            });
            nativeMenu(QObject::tr("Symmetry"),{
                {"hmirror_action",QObject::tr("Horizontal")},
                {"vmirror_action",QObject::tr("Vertical")}
            });
        } else if(shortId==QStringLiteral("KisToolTransform")) {
            // Krita's full Tool Options panel switches mode by calling
            // KisToolTransformConfigWidget slots. Its global shortcut
            // actions require KoToolManager::activeCanvasController(), which
            // can differ in this independent, main-window-free host.
            // Call exactly the same native slots as the working gear panel.
            addTransformModeMenu();
            if(auto *box=qobject_cast<QDialogButtonBox*>(findOption("buttonBox")))
                addOption(box->button(QDialogButtonBox::Reset),QObject::tr("Reset"));
            const auto modeOption=[this](const char *name,const QString &label) {
                QPointer<QWidget> source=findOption(name);
                const int before=m_row->count();
                addOption(source,label);
                if(m_row->count()==before)return;
                QPointer<QWidget> control=m_row->itemAt(before)->widget();
                m_refresh.append([this,source,control]() {
                    if(control)control->setVisible(optionAvailable(source));
                });
            };
            modeOption("liquifySizeSlider",QObject::tr("Size"));
            modeOption("liquifyAmountSlider",QObject::tr("Strength"));
            modeOption("chkShowControlPoints",QObject::tr("Mesh points"));
            // Keep related modes nested instead of flattening four
            // desktop-style options into separate toolbar buttons.
            addNamedChoices(QObject::tr("Flip"),{
                {"flipXButton",QObject::tr("Horizontal")},
                {"flipYButton",QObject::tr("Vertical")}
            });
        } else if(shortId==QStringLiteral("KisToolFill")) {
            addSectionChoices("sectionWhatToFill",QObject::tr("Fill area"),
                {QObject::tr("Selection"),QObject::tr("Contiguous"),QObject::tr("Similar color")});
            addSectionChoices("sectionFillWith",QObject::tr("Source"),
                {QObject::tr("Foreground"),QObject::tr("Background"),QObject::tr("Pattern")});
            addFillResource(false);
            named("sliderThreshold",QObject::tr("Tolerance"));
            named("mobileFillDragTolerance",QObject::tr("Drag tolerance"));
            named("sliderCloseGap",QObject::tr("Close gap"));
            addSectionChoices("sectionReference",QObject::tr("Reference"),
                {QObject::tr("Current layer"),QObject::tr("All layers"),QObject::tr("Color labels")});
        } else if(shortId==QStringLiteral("KisToolEncloseAndFill")) {
            addSectionChoices("sectionEnclosingMethod",QObject::tr("Enclose with"),
                {QObject::tr("Rectangle"),QObject::tr("Ellipse"),QObject::tr("Path"),
                 QObject::tr("Lasso"),QObject::tr("Brush")});
            if(auto *section=qobject_cast<KisOptionCollectionWidgetWithHeader*>(
                   findOption("sectionWhatToFill")))
                addOption(section->primaryWidget(),QObject::tr("Regions"));
            addSectionChoices("sectionFillWith",QObject::tr("Source"),
                {QObject::tr("Foreground"),QObject::tr("Background"),QObject::tr("Pattern")});
            addFillResource(false);
            named("sliderThreshold",QObject::tr("Tolerance"));
            named("sliderCloseGap",QObject::tr("Close gap"));
            addSectionChoices("sectionReference",QObject::tr("Reference"),
                {QObject::tr("Current layer"),QObject::tr("All layers"),QObject::tr("Color labels")});
        } else if(shortId==QStringLiteral("ToolReferenceImages")) {
            addReferenceControls();
            addNamedChoices(QObject::tr("Add reference"),{
                {"bnAddReferenceImage",QObject::tr("From file")},
                {"bnPasteReferenceImage",QObject::tr("From clipboard")},
                {"bnAddReferenceImageFromCurrentLayer",QObject::tr("From active layer")},
                {"bnAddReferenceImageFromVisible",QObject::tr("From visible layers")}
            });
            named("opacitySlider",QObject::tr("Opacity"));
            named("bnDeleteSelectedImages",QObject::tr("Remove"));
            nativeMenu(QObject::tr("Arrange"),{
                {"object_order_front",QObject::tr("Bring to front")},
                {"object_order_back",QObject::tr("Send to back")},
                {"object_transform_rotate_90_cw",QObject::tr("Rotate right")},
                {"object_transform_rotate_90_ccw",QObject::tr("Rotate left")},
                {"object_transform_mirror_horizontally",QObject::tr("Flip H")},
                {"object_transform_mirror_vertically",QObject::tr("Flip V")},
                {"object_transform_reset",QObject::tr("Reset transform")}
            });
        } else if(shortId==QStringLiteral("KisToolLazyBrush")) {
            named("btnUpdate",QObject::tr("Update colorize"));
            named("chkAutoUpdates",QObject::tr("Auto update"));
            named("chkShowKeyStrokes",QObject::tr("Strokes"));
            named("chkShowOutput",QObject::tr("Output"));
            named("intRadius",QObject::tr("Gap"));
        } else if(shortId==QStringLiteral("KisToolKnife")) {
            addStripChoices(qobject_cast<KisOptionButtonStrip*>(
                findOption("optionButtonStripToolMode")),QObject::tr("Gutter"),
                {QObject::tr("Add"),QObject::tr("Remove")});
            addNamedChoices(QObject::tr("Gap"),{
                {"thickGapWidthRadioButton",QObject::tr("Thick")},
                {"thinGapWidthRadioButton",QObject::tr("Thin")},
                {"specialGapWidthRadioButton",QObject::tr("Custom")},
                {"automaticGapWidthRadioButton",QObject::tr("Automatic")}
            });
            named("thickGapWidth",QObject::tr("Thick width"));
            named("thinGapWidth",QObject::tr("Thin width"));
            named("customGapWidth",QObject::tr("Custom width"));
        } else if(shortId==QStringLiteral("KisToolDyna")) {
            named("mobileDynaMass",QObject::tr("Mass"));
            named("mobileDynaDrag",QObject::tr("Drag"));
        } else if(shortId==QStringLiteral("KisToolSmartPatch")) {
            named("accuracySlider",QObject::tr("Accuracy"));
            named("patchRadius",QObject::tr("Radius"));
        } else if(shortId==QStringLiteral("KisToolMeasure")) {
            addReadout("mobileMeasureDistance",QObject::tr("Distance"));
            addReadout("mobileMeasureAngle",QObject::tr("Angle"));
            named("mobileMeasureUnits",QObject::tr("Units"));
        } else if(shortId==QStringLiteral("KisToolGradient")) {
            addFillResource(true);
            named("shape_combo",QObject::tr("Shape"));
            named("repeat_combo",QObject::tr("Repeat"));
            named("reverse_check",QObject::tr("Reverse"));
        } else if(shortId==QStringLiteral("KisToolMove")) {
            addNamedChoices(QObject::tr("Move target"),{
                {"radioSelectedLayer",QObject::tr("Selected layer")},
                {"radioFirstLayer",QObject::tr("Layer under pointer")},
                {"radioGroup",QObject::tr("Group under pointer")}
            });
            named("spinMoveStep",QObject::tr("Step"));
        } else if(shortId==QStringLiteral("KisToolCrop")) {
            named("cmbType",QObject::tr("Crop target"));
            named("intWidth",QObject::tr("Width"));
            named("intHeight",QObject::tr("Height"));
            named("lockRatioButton",QObject::tr("Lock ratio"));
            named("bnCrop",QObject::tr("Apply crop"));
        } else if(shortId==QStringLiteral("KisAssistantTool")) {
            named("availableAssistantsComboBox",QObject::tr("Guide type"));
            const auto assistant=m_nativeTool;
            auto *threePoint=button(QObject::tr("3-point perspective"));
            connect(threePoint,&QToolButton::clicked,this,[this,assistant]() {
                if(assistant && assistant==m_nativeTool)
                    QMetaObject::invokeMethod(assistant.data(),"mobileToggleThreePointPlacement");
            });
            m_refresh.append([assistant,threePoint]() {
                setChipText(threePoint,assistant && assistant->property("mobilePerspectivePlacement").toBool()?
                    QObject::tr("Cancel placement"):QObject::tr("3-point perspective"));
            });
            named("localAssistantCheckbox",QObject::tr("Local"));
            addNamedChoices(QObject::tr("Guide files"),{
                {"loadAssistantButton",QObject::tr("Import guides")},
                {"saveAssistantButton",QObject::tr("Export guides")}
            });
            nativeMenu(QObject::tr("Visibility"),{
                {"view_toggle_painting_assistants",QObject::tr("Guides")},
                {"view_toggle_assistant_previews",QObject::tr("Preview")}
            });
        } else if(shortId==QStringLiteral("KisToolColorSampler")) {
            named("cmbSources",QObject::tr("Sample from"));
            named("radius",QObject::tr("Radius"));
            named("blend",QObject::tr("Blend"));
            named("cbUpdateCurrentColor",QObject::tr("Update color"));
        } else if(shortId==QStringLiteral("KisToolMultiBrush")) {
            named("multihandTypeCombobox",QObject::tr("Symmetry"));
            named("showAxesCheckbox",QObject::tr("Show axes"));
            named("brushCountSpinBox",QObject::tr("Copies"));
            named("translationRadiusSpinbox",QObject::tr("Radius"));
            named("horizontalCheckbox",QObject::tr("Horizontal"));
            named("verticalCheckbox",QObject::tr("Vertical"));
            named("moveOriginButton",QObject::tr("Move origin"));
            named("resetOriginButton",QObject::tr("Reset origin"));
        } else if(shortId==QStringLiteral("SvgTextTool")) {
            named("mobileEditSvgText",QObject::tr("Edit text"));
            if(openTextFormatting) {
                auto *format=button(QObject::tr("Format"));
                connect(format,&QToolButton::clicked,this,[this,format]() {
                    if(openTextFormatting)openTextFormatting(format);
                });
            }
            named("mobileSvgGlyphs",QObject::tr("Glyphs"));
        }
        named("cmbOutline",QObject::tr("Outline"));
        named("cmbFill",QObject::tr("Fill"));
        if(m_row->count()==0)addCommonOptions();

        // Use native in-progress stroke transactions. End actions always
        // stay at the end of the compact subtool strip.
        if(finishablePath) {
            const QPointer<KoToolBase> path=nativeTool;
            auto *cancel=button(QObject::tr("Cancel"));
            connect(cancel,&QToolButton::clicked,this,[this,path]() {
                if(path && m_nativeTool==path.data())path->requestStrokeCancellation();
            });
            auto *finish=button(QObject::tr("Done"));
            connect(finish,&QToolButton::clicked,this,[this,path]() {
                if(path && m_nativeTool==path.data())path->requestStrokeEnd();
            });
        }
        if(shortId==QStringLiteral("KisToolTransform")) {
            // Use the native transform editor's Cancel button if present.
            // A generic requestStrokeCancellation() after the operation
            // commits is usually a no-op and should not be presented.
            if(auto *box=qobject_cast<QDialogButtonBox*>(findOption("buttonBox"))) {
                addOption(box->button(QDialogButtonBox::Cancel),QObject::tr("Cancel"));
                addOption(box->button(QDialogButtonBox::Apply),QObject::tr("Done"));
            }
        }
        // Keep critical native transaction controls easy to spot.
        for(int i=0;i<m_row->count();++i) {
            auto *entry=m_row->itemAt(i);
            auto *control=entry?qobject_cast<QToolButton*>(entry->widget()):nullptr;
            if(!control)continue;
            const QString name=control->accessibleName();
            if(name==QObject::tr("Cancel"))control->setObjectName(QStringLiteral("mobileContextCancel"));
            else if(name==QObject::tr("Done") || name==QObject::tr("Finish"))
                control->setObjectName(QStringLiteral("mobileContextDone"));
            if(!control->objectName().isEmpty())control->style()->polish(control);
        }
        m_row->addStretch();
        m_scroll->setWidget(content);
        content->installEventFilter(this);
        // Each tool starts with its most common controls; a long strip on
        // the previous tool must not leave the new tool scrolled to the end.
        m_scroll->horizontalScrollBar()->setValue(0);
        refreshControls();
    }

protected:
    // Qt's kinetic scrolling handles finger touches. S Pen hover/mouse
    // events over the empty row are not recognized by QScroller on
    // Android, so pan those backgrounds directly along the X axis.
    // Children (mode chips, buttons) retain their own click semantics.
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        const bool background=watched==m_scroll->viewport() ||
            (m_scroll->widget() && watched==m_scroll->widget());
        if(!background)return QFrame::eventFilter(watched,event);
        const auto type=event->type();
        if(type==QEvent::MouseButtonPress) {
            auto *mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()!=Qt::LeftButton)return false;
            m_penScrollActive=true;
            m_penScrollStart=mouse->globalPos();
            m_penScrollValue=m_scroll->horizontalScrollBar()->value();
            return true;
        }
        if(!m_penScrollActive)return false;
        if(type==QEvent::MouseMove) {
            auto *mouse=static_cast<QMouseEvent*>(event);
            if(!(mouse->buttons()&Qt::LeftButton))return false;
            const QPoint moved=mouse->globalPos()-m_penScrollStart;
            m_scroll->horizontalScrollBar()->setValue(m_penScrollValue-moved.x());
            return true;
        }
        if(type==QEvent::MouseButtonRelease) {
            m_penScrollActive=false;
            return true;
        }
        if(type==QEvent::Hide || type==QEvent::Leave) {
            m_penScrollActive=false;
        }
        return QFrame::eventFilter(watched,event);
    }

    void showEvent(QShowEvent *event) override
    {
        QFrame::showEvent(event);
        m_sync->start();
    }

    void hideEvent(QHideEvent *event) override
    {
        m_sync->stop();
        QFrame::hideEvent(event);
    }

private:
    struct Named {const char *name; QString label;};
    struct Choice {
        QString label;
        QPointer<QAbstractButton> button;
    };
    struct ActionChoice {
        QString label;
        QPointer<QAction> action;
        QPointer<QAbstractButton> state{}; // optional: native button whose isChecked() reflects current mode
    };

    QWidget *findOption(const char *name) const
    {
        const QString key=QString::fromLatin1(name);
        for(const auto &root:m_options) {
            if(!root)continue;
            if(auto *child=root->findChild<QWidget*>(key))return child;
            auto collections=root->findChildren<KisOptionCollectionWidget*>();
            if(auto *collection=qobject_cast<KisOptionCollectionWidget*>(root.data()))
                collections.prepend(collection);
            for(auto *collection:collections) {
                if(collection->containsWidget(key))return collection->widget(key);
            }
        }
        return nullptr;
    }

    QToolButton *button(const QString &label)
    {
        auto *control=new QToolButton;
        setChipText(control,label);
        control->setAccessibleName(label);
        control->setToolTip(label);
        control->setFixedHeight(36);
        m_row->addWidget(control);
        return control;
    }

    void refreshControls()
    {
        for(const auto &refresh:m_refresh)refresh();
    }

    void addReferenceControls()
    {
        auto *choose=button(tr("References"));
        auto *pin=button(tr("Pin to screen"));
        const auto canvas=[this]() {
            const auto *tool=qobject_cast<KoToolBase*>(m_nativeTool.data());
            return tool?qobject_cast<KisCanvas2*>(tool->canvas()):nullptr;
        };
        const auto layer=[canvas]() -> KisReferenceImagesLayerSP {
            auto *current=canvas();
            auto *view=current?current->imageView().data():nullptr;
            return view && view->document()?view->document()->referenceImagesLayer():KisReferenceImagesLayerSP();
        };
        connect(choose,&QToolButton::clicked,this,[this,choose,layer]() {
            const auto refs=layer();
            if(!refs)return;
            const auto images=refs->referenceImages();
            auto *selection=refs->shapeManager()->selection();
            QMenu menu(tr("References"),this);
            for(int i=0;i<images.size();++i) {
                auto *image=images.at(i);
                const auto file=QFileInfo(image->filename()).fileName();
                auto *entry=menu.addAction(file.isEmpty()?tr("Reference %1").arg(i+1):
                    tr("%1 · %2").arg(i+1).arg(file));
                entry->setCheckable(true);
                entry->setChecked(selection->isSelected(image));
                entry->setData(i);
            }
            const auto generation=m_generation;
            mobileShowActionList(parentWidget(),choose,menu,this,
                [this,layer,refs,images,generation](const QVariant &value) {
                    if(generation!=m_generation || layer()!=refs ||
                       refs->referenceImages()!=images)return;
                    const int index=value.toInt();
                    if(index<0 || index>=images.size())return;
                    auto *selection=refs->shapeManager()->selection();
                    selection->deselectAll();
                    selection->select(images.at(index));
                    refreshControls();
                });
        });
        const auto selected=[layer]() -> KisReferenceImage* {
            const auto refs=layer();
            if(!refs)return nullptr;
            const auto shapes=refs->shapeManager()->selection()->selectedShapes();
            return shapes.size()==1?dynamic_cast<KisReferenceImage*>(shapes.first()):nullptr;
        };
        connect(pin,&QToolButton::clicked,this,[this,canvas,selected]() {
            auto *image=selected();
            auto *current=canvas();
            if(!image || !current)return;
            if(auto *command=KisReferenceImage::mobileToggleScreenPin(
                    image,current->coordinatesConverter()->documentToWidgetTransform())) {
                current->addCommand(command);
                if(auto decoration=current->referenceImagesDecoration())
                    decoration->invalidateInteractiveReferenceCache();
            }
            refreshControls();
        });
        m_refresh.append([this,choose,pin,layer,selected]() {
            const auto refs=layer();
            auto *image=selected();
            const int index=refs && image?refs->referenceImages().indexOf(image):-1;
            choose->setEnabled(refs && refs->shapeCount()>0);
            setChipText(choose,index<0?tr("References"):tr("Reference %1").arg(index+1));
            pin->setEnabled(image);
            setChipText(pin,image && image->mobileScreenPinned()?tr("Attach to canvas"):tr("Pin to screen"));
        });
    }

    bool optionAvailable(QWidget *source) const
    {
        if(!source || !source->isEnabled())return false;
        for(const auto &root:m_options) {
            if(root && (source==root.data() || root->isAncestorOf(source)))
                return source==root.data() || source->isVisibleTo(root.data());
        }
        return false;
    }

    void addAction(QAction *source,const QString &label)
    {
        auto *control=button(label);
        const QPointer<QAction> native=source;
        connect(control,&QToolButton::clicked,this,[native]() {
            if(native && native->isEnabled())native->trigger();
        });
        m_refresh.append([control,native]() {
            control->setEnabled(native && native->isEnabled());
            control->setCheckable(native && native->isCheckable());
            control->setChecked(native && native->isChecked());
        });
    }

    void addActionMenu(const QString &label,const QList<ActionChoice> &choices)
    {
        if(choices.isEmpty())return;
        auto *control=button(label);
        connect(control,&QToolButton::clicked,this,[this,control,label,choices]() {
            QMenu menu(label);
            const quint64 generation=m_generation;
            const QPointer<QObject> invokedTool=m_nativeTool;
            for(int i=0;i<choices.size();++i) {
                const auto &choice=choices.at(i);
                if(!choice.action)continue;
                auto *item=menu.addAction(choice.label);
                item->setEnabled(choice.action->isEnabled());
                // Use the native button's checked state when available; it
                // reflects the live transform mode more reliably than the
                // action's own checked state on Android.
                const bool checked=choice.state?choice.state->isChecked()
                                               :choice.action->isChecked();
                item->setCheckable(choice.state||choice.action->isCheckable());
                item->setChecked(checked);
                item->setData(i);
            }
            mobileShowActionList(parentWidget(),control,menu,this,
                [this,generation,invokedTool,choices](const QVariant &picked) {
                    const auto action=choices.value(picked.toInt()).action;
                    if(generation==m_generation && invokedTool==m_nativeTool &&
                       action && action->isEnabled())action->trigger();
                });
        });
        m_refresh.append([control,label,choices]() {
            bool enabled=false;
            QString active;
            for(const auto &choice:choices) {
                enabled|=choice.action&&choice.action->isEnabled();
                // Prefer the native button's isChecked() — it tracks the
                // live Krita transform mode without depending on the action's
                // own checked property which may not be set on Android.
                const bool checked=choice.state?choice.state->isChecked()
                                               :(choice.action&&choice.action->isChecked());
                if(checked&&active.isEmpty())active=choice.label;
            }
            control->setEnabled(enabled);
            setChoiceLabel(control,label,active);
        });
    }

    void addTransformModeMenu()
    {
        struct ModeSpec {
            const char *buttonName;
            const char *slot;
            QString label;
            const char *mode;
        };
        struct Choice {
            QString label;
            QPointer<QAbstractButton> button;
            QPointer<QObject> config;
            QByteArray slot;
            int mode=-1;
        };
        const std::initializer_list<ModeSpec> specs={
            {"freeTransformButton","slotSetFreeTransformModeButtonClicked",QObject::tr("Standard"),"FreeTransformMode"},
            {"perspectiveTransformButton","slotSetPerspectiveModeButtonClicked",QObject::tr("Perspective"),"PerspectiveTransformMode"},
            {"warpButton","slotSetWarpModeButtonClicked",QObject::tr("Warp"),"WarpTransformMode"},
            {"cageButton","slotSetCageModeButtonClicked",QObject::tr("Cage"),"CageTransformMode"},
            {"liquifyButton","slotSetLiquifyModeButtonClicked",QObject::tr("Liquify"),"LiquifyTransformMode"},
            {"meshButton","slotSetMeshModeButtonClicked",QObject::tr("Mesh"),"MeshTransformMode"}
        };
        QList<Choice> choices;
        for(const auto &spec:specs) {
            auto *button=qobject_cast<QAbstractButton*>(findOption(spec.buttonName));
            if(!button)continue;
            const QByteArray slot(spec.slot);
            const QByteArray signature=slot+"(bool)";
            // Find the tool-options owner, not the autoExclusive QToolButton.
            // Its native slot emits sigResetTransform and preserves the
            // same transaction handling as the full Tool Options panel.
            for(QObject *owner=button;owner;owner=owner->parent()) {
                if(owner->metaObject()->indexOfMethod(signature.constData())<0)
                    continue;
                const int enumIndex=m_nativeTool?
                    m_nativeTool->metaObject()->indexOfEnumerator("TransformToolMode"):-1;
                const int mode=enumIndex<0?-1:
                    m_nativeTool->metaObject()->enumerator(enumIndex).keyToValue(spec.mode);
                choices.append({spec.label,button,owner,slot,mode});
                m_used.insert(button);
                break;
            }
        }
        if(choices.isEmpty())return;
        auto *control=button(QObject::tr("Mode"));
        connect(control,&QToolButton::clicked,this,[this,control,choices]() {
            QMenu menu;
            styleMenu(menu);
            const quint64 generation=m_generation;
            const QPointer<QObject> invokedTool=m_nativeTool;
            for(int i=0;i<choices.size();++i) {
                const auto &choice=choices.at(i);
                auto *item=menu.addAction(choice.label);
                item->setData(i);
                item->setCheckable(true);
                item->setChecked(m_nativeTool && choice.mode>=0 &&
                    m_nativeTool->property("transformMode").toInt()==choice.mode);
                item->setEnabled(choice.config && optionAvailable(choice.button));
            }
            mobileShowActionList(parentWidget(),control,menu,this,
                [this,generation,invokedTool,choices](const QVariant &picked) {
                const auto choice=choices.value(picked.toInt());
                // QMenu may briefly hide the floating bar while Android
                // transfers focus. Visibility is NOT proof that the native
                // Transform tool was deactivated, and was dropping taps.
                if(generation!=m_generation || !invokedTool ||
                   m_nativeTool!=invokedTool.data() ||
                   m_id.section(QLatin1Char('/'),-1)!=QStringLiteral("KisToolTransform") ||
                   !choice.config || !choice.button ||
                   !choice.button->isEnabled()) {
                    qWarning() << "Mobile Transform Mode: request dropped"
                               << choice.label << "generation" << generation << m_generation;
                    return;
                }

                // Preserve native exclusive-button state and dispatch one
                // reset signal. The label and diagnostic read the tool's
                // actual TransformToolMode property after dispatch.
                choice.button->setChecked(true);
                const bool invoked=QMetaObject::invokeMethod(
                    choice.config.data(),choice.slot.constData(),
                    Qt::DirectConnection,Q_ARG(bool,true));
                if(!invoked) {
                    qWarning() << "Mobile Transform Mode: native slot failed"
                               << choice.slot << choice.label;
                } else {
                    qInfo() << "Mobile Transform Mode: native options slot invoked"
                            << choice.label << "checked"
                            << choice.button->isChecked();
                    // A later native transaction refresh can still change
                    // the selection. Log that rather than claiming the
                    // transform succeeded solely from invokeMethod(true).
                    const QPointer<QObject> tool=m_nativeTool;
                    QTimer::singleShot(350,this,[this,choice,tool]() {
                        if(m_id.section(QLatin1Char('/'),-1)!=QStringLiteral("KisToolTransform") ||
                           !tool || tool!=m_nativeTool)return;
                        const int actual=tool->property("transformMode").toInt();
                        qInfo() << "Mobile Transform Mode: verified native state"
                                << "tool" << tool.data() << "options" << choice.config.data()
                                << "requested" << choice.mode << "actual" << actual;
                        if(actual!=choice.mode)
                            qWarning() << "Mobile Transform Mode: native tool mode mismatch"
                                       << choice.label << actual;
                    });
                }
                refreshControls();
            });
        });
        m_refresh.append([this,control,choices]() {
            bool enabled=false;
            QString active;
            for(const auto &choice:choices) {
                enabled|=choice.config && optionAvailable(choice.button);
                if(m_nativeTool && choice.mode>=0 &&
                   m_nativeTool->property("transformMode").toInt()==choice.mode)
                    active=choice.label;
            }
            control->setEnabled(enabled);
            setChoiceLabel(control,QObject::tr("Mode"),active);
        });
    }

    void addChoices(const QString &label,const QList<Choice> &choices)
    {
        if(choices.isEmpty())return;
        auto *control=button(label);
        connect(control,&QToolButton::clicked,this,[this,control,label,choices]() {
            QMenu menu(label);
            const quint64 generation=m_generation;
            const QPointer<QObject> invokedTool=m_nativeTool;
            for(int i=0;i<choices.size();++i) {
                const auto &choice=choices.at(i);
                auto *item=menu.addAction(choice.label);
                item->setData(i);
                if(choice.button) {
                    item->setEnabled(optionAvailable(choice.button));
                    item->setCheckable(choice.button->isCheckable());
                    item->setChecked(choice.button->isChecked());
                } else {
                    item->setEnabled(false);
                }
            }
            mobileShowActionList(parentWidget(),control,menu,this,
                [this,generation,invokedTool,choices](const QVariant &picked) {
                    const auto source=choices.value(picked.toInt()).button;
                    if(generation==m_generation && invokedTool==m_nativeTool &&
                       optionAvailable(source))source->click();
                });
        });
        m_refresh.append([this,control,label,choices]() {
            bool enabled=false;
            QString active;
            for(const auto &choice:choices) {
                enabled|=optionAvailable(choice.button);
                if(choice.button && choice.button->isChecked())active=choice.label;
            }
            control->setEnabled(enabled);
            setChoiceLabel(control,label,active);
        });
    }

    void addNamedChoices(const QString &label,std::initializer_list<Named> entries)
    {
        QList<Choice> choices;
        for(const auto &entry:entries) {
            if(auto *control=qobject_cast<QAbstractButton*>(findOption(entry.name))) {
                choices.append({entry.label,control});
                m_used.insert(control);
            }
        }
        addChoices(label,choices);
    }

    void addSectionChoices(const char *name,const QString &label,
                           const QStringList &labels)
    {
        auto *section=qobject_cast<KisOptionCollectionWidgetWithHeader*>(findOption(name));
        addStripChoices(section?qobject_cast<KisOptionButtonStrip*>(section->primaryWidget()):nullptr,
                        label,labels);
    }

    void addStripChoices(KisOptionButtonStrip *strip,const QString &label,
                         const QStringList &labels)
    {
        if(!strip)return;
        const auto buttons=strip->buttons();
        if(buttons.isEmpty())return;
        QList<Choice> choices;
        for(int i=0;i<buttons.size();++i) {
            auto *native=buttons.at(i);
            if(!native)continue;
            // Krita tool versions can add/reorder option-strip buttons.
            // Never drop the entire control just because our short-label
            // table differs: retain the native label and QAction behavior.
            QString caption=native->text().trimmed();
            if(caption.isEmpty())caption=native->toolTip().trimmed();
            if(caption.isEmpty())caption=native->accessibleName().trimmed();
            // Only use the abbreviated mobile name when Krita does not
            // provide a native label. A plugin may reorder its options
            // without changing their count; positional labels then lie.
            if(caption.isEmpty() && i<labels.size())
                caption=labels.at(i);
            if(caption.isEmpty())
                caption=QObject::tr("Option %1").arg(i+1);
            choices.append({caption,native});
            m_used.insert(native);
        }
        addChoices(label,choices);
    }

    void addReadout(const char *name,const QString &label)
    {
        const QPointer<QLabel> native=qobject_cast<QLabel*>(findOption(name));
        if(!native)return;
        auto *readout=new QLabel;
        readout->setStyleSheet(QStringLiteral("color:#f4f4f5;padding:0 6px;"));
        m_row->addWidget(readout);
        m_refresh.append([native,label,readout]() {
            if(native)readout->setText(QStringLiteral("%1 %2").arg(label,native->text()));
        });
    }

    void addOption(QWidget *source,const QString &label)
    {
        if(!source || label.isEmpty() || m_used.contains(source))return;
        if(auto *toggle=qobject_cast<QAbstractButton*>(source)) {
            auto *control=button(label);
            const QPointer<QAbstractButton> native=toggle;
            connect(control,&QToolButton::clicked,this,[this,native]() {
                if(optionAvailable(native))native->click();
            });
            m_refresh.append([this,control,native]() {
                control->setEnabled(optionAvailable(native));
                control->setCheckable(native && native->isCheckable());
                control->setChecked(native && native->isChecked());
            });
        } else if(auto *combo=qobject_cast<QComboBox*>(source)) {
            auto *control=button(label);
            const QPointer<QComboBox> native=combo;
            connect(control,&QToolButton::clicked,this,[this,control,label,native]() {
                if(!optionAvailable(native))return;
                const quint64 generation=m_generation;
                const QPointer<QObject> invokedTool=m_nativeTool;
                QMenu choices(label);
                const QStringList preferred=native->objectName()==
                    QStringLiteral("availableAssistantsComboBox")?
                    QStringList{QStringLiteral("vanishing point"),QStringLiteral("two point"),
                                QStringLiteral("perspective"),QStringLiteral("parallel ruler")}:
                    QStringList{};
                QVector<int> rows;
                for(const QString &id:preferred) {
                    const int row=native->findData(id);
                    if(row>=0)rows.append(row);
                }
                for(int row=0;row<native->count();++row)
                    if(!rows.contains(row))rows.append(row);
                for(const int row:rows) {
                    const QString text=native->itemText(row);
                    if(text.isEmpty())continue;
                    auto *item=choices.addAction(text);
                    item->setData(QVariantList{row,text});
                    item->setEnabled(native->model()->index(row,native->modelColumn(),
                        native->rootModelIndex()).flags().testFlag(Qt::ItemIsEnabled));
                    item->setCheckable(true);
                    item->setChecked(row==native->currentIndex());
                }
                mobileShowActionList(parentWidget(),control,choices,this,
                    [this,native,generation,invokedTool](const QVariant &selection) {
                    const auto data=selection.toList();
                    if(data.size()!=2 || generation!=m_generation || !invokedTool ||
                       m_nativeTool!=invokedTool.data() || !optionAvailable(native))return;
                    const int row=data.first().toInt();
                    if(row<0 || row>=native->count() || native->itemText(row)!=data.at(1).toString())return;
                    if(invokedTool->property("mobilePerspectivePlacement").toBool())
                        QMetaObject::invokeMethod(invokedTool.data(),"mobileToggleThreePointPlacement");
                    if(!native)return;
                    native->setCurrentIndex(row);
                    if(native)Q_EMIT native->activated(row);
                });
            });
            m_refresh.append([this,control,label,native]() {
                control->setEnabled(optionAvailable(native));
                setValueLabel(control,label,native?native->currentText():QString());
            });
        } else if(auto *integer=qobject_cast<QSpinBox*>(source)) {
            addNumber(integer,label);
        } else if(auto *decimal=qobject_cast<QDoubleSpinBox*>(source)) {
            addNumber(decimal,label);
        } else if(auto *slider=qobject_cast<QSlider*>(source)) {
            addNumber(slider,label);
        } else {
            return;
        }
        m_used.insert(source);
    }

    template<class SpinBox>
    void addNumber(SpinBox *source,const QString &label)
    {
        auto *control=button(label);
        const QPointer<SpinBox> native=source;
        connect(control,&QToolButton::clicked,this,[this,control,label,native]() {
            if(!optionAvailable(native))return;
            const auto generation=m_generation;
            const auto invokedTool=m_nativeTool;
            if(MobileTransientPanel::closeForAnchor(parentWidget(),control))return;
            auto *panel=new MobileTransientPanel(parentWidget());
            panel->setObjectName(QStringLiteral("mobileNumericOption"));
            panel->setStyleSheet(QStringLiteral(
                "QFrame#mobileNumericOption{background:#252529;border:1px solid #55555c;border-radius:8px;}"
                "QLabel{color:#f4f4f5;}"));
            auto *layout=new QVBoxLayout(panel);
            auto *value=new QLabel(panel);
            auto *slider=new MobileTouchSlider(panel);
            slider->setAccessibleName(label);
            const double low=native->minimum(),high=native->maximum();
            constexpr bool decimal=std::is_same<SpinBox,QDoubleSpinBox>::value;
            const int steps=decimal?1000:qMax(1,int(high-low));
            slider->setRange(0,steps);
            slider->setValue(high>low?qRound(steps*(native->value()-low)/(high-low)):0);
            const auto updateLabel=[label,value,native]() {
                if(!native)return;
                if constexpr(std::is_same<SpinBox,QSlider>::value)
                    value->setText(QStringLiteral("%1  %2").arg(label).arg(native->value()));
                else value->setText(QStringLiteral("%1  %2").arg(label,native->cleanText()));
            };
            updateLabel();
            layout->addWidget(value);
            layout->addWidget(slider);
            connect(slider,&QSlider::valueChanged,this,
                [this,panel,native,generation,invokedTool,low,high,steps,updateLabel](int position) {
                    if(generation!=m_generation || invokedTool!=m_nativeTool ||
                       !optionAvailable(native)) {panel->close();return;}
                    const double setting=low+(high-low)*position/steps;
                    if constexpr(decimal)native->setValue(setting);
                    else native->setValue(qRound(setting));
                    updateLabel();
                });
            if constexpr(!std::is_same<SpinBox,QSlider>::value) {
                connect(slider,&QSlider::sliderReleased,this,[this,native,generation,invokedTool]() {
                    if(generation==m_generation && invokedTool==m_nativeTool && optionAvailable(native))
                        Q_EMIT native->editingFinished();
                });
            }
            panel->anchorTo(control,QSize(245,85));
            panel->present();
        });
        m_refresh.append([this,control,label,native]() {
            control->setEnabled(optionAvailable(native));
            QString value;
            if(native) {
                if constexpr(std::is_same<SpinBox,QSlider>::value)
                    value=QString::number(native->value());
                else
                    value=native->cleanText();
            }
            setValueLabel(control,label,value);
        });
    }

    void addCommonOptions()
    {
        for(const auto &root:m_options) {
            if(!root)continue;
            const auto widgets=root->findChildren<QWidget*>();
            for(QWidget *candidate:widgets) {
                if(m_row->count()>=5)return;
                if(m_used.contains(candidate) || !candidate->isVisibleTo(root))continue;
                QString label=candidate->accessibleName();
                if(auto *check=qobject_cast<QCheckBox*>(candidate))label=check->text();
                else if(!qobject_cast<QComboBox*>(candidate) &&
                        !qobject_cast<QSpinBox*>(candidate) &&
                        !qobject_cast<QDoubleSpinBox*>(candidate))continue;
                if(label.isEmpty()) {
                    if(auto *parent=candidate->parentWidget())
                        if(auto *form=qobject_cast<QFormLayout*>(parent->layout()))
                            if(auto *caption=qobject_cast<QLabel*>(form->labelForField(candidate)))
                                label=caption->text();
                }
                if(label.isEmpty()) {
                    if(auto *spin=qobject_cast<QSpinBox*>(candidate))label=spin->prefix();
                    if(auto *spin=qobject_cast<QDoubleSpinBox*>(candidate))label=spin->prefix();
                }
                label.remove(QLatin1Char('&'));
                label=label.trimmed();
                if(label.endsWith(QLatin1Char(':')))label.chop(1);
                if(label.size()<=32)addOption(candidate,label);
            }
        }
    }

    static void setChoiceLabel(QToolButton *control,const QString &label,const QString &choice)
    {
        const QString full=choice.isEmpty()?label:
            QStringLiteral("%1: %2").arg(label,choice);
        // The selected mode identifies the chip; keep the longer label
        // for accessibility without using scarce horizontal space.
        setChipText(control,choice.isEmpty()?label:choice);
        control->setToolTip(full);
        control->setAccessibleName(full);
    }

    static void setValueLabel(QToolButton *control,const QString &label,const QString &value)
    {
        const QString text=value.isEmpty()?label:QStringLiteral("%1: %2").arg(label,value);
        setChipText(control,text);
        control->setToolTip(text);
        control->setAccessibleName(text);
    }

    static void setChipText(QToolButton *control,const QString &text,int limit=86)
    {
        const auto metrics=control->fontMetrics();
        const QString shown=metrics.elidedText(text,Qt::ElideRight,limit);
        control->setText(shown);
        control->setFixedWidth(qMax(43,metrics.horizontalAdvance(shown)+12));
    }

    static void styleMenu(QMenu &menu)
    {
        menu.setStyleSheet(QStringLiteral(
            "QMenu{background:#252529;color:#f4f4f5;border:1px solid #55555c;}"
            "QMenu::item{min-height:32px;padding:7px 14px;}"
            "QMenu::item:selected{background:#45454f;}"
            "QMenu::item:disabled{color:#98989f;}"));
    }

    QString m_id;
    QPointer<QObject> m_nativeTool;
    QList<QPointer<QWidget>> m_options;
    QList<std::function<void()>> m_refresh;
    QSet<QWidget*> m_used;
    QToolButton *m_tool=nullptr;
    QScrollArea *m_scroll=nullptr;
    bool m_penScrollActive=false;
    QPoint m_penScrollStart;
    int m_penScrollValue=0;
    QHBoxLayout *m_row=nullptr;
    QTimer *m_sync=nullptr;
    quint64 m_generation=0;
};
#endif
