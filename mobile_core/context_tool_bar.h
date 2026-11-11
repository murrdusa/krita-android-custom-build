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
#include "touch_scroll.h"
#include <QSet>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <initializer_list>
#include <type_traits>
#include <KisOptionCollectionWidget.h>
#include <KisOptionButtonStrip.h>
#include <KoGroupButton.h>
#include <kis_slider_spin_box.h>

#include "engine_bridge.h"

class MobileContextToolBar final : public QFrame
{
public:
    explicit MobileContextToolBar(QWidget *parent,QAction *options) : QFrame(parent)
    {
        setObjectName(QStringLiteral("mobileContextToolBar"));
        setStyleSheet(QStringLiteral(
            "QFrame#mobileContextToolBar{background:rgba(26,29,34,175);"
            "border:1px solid #48484e;border-radius:10px;}"
            "QWidget#mobileContextControls,QScrollArea{background:transparent;border:0;}"
            "QToolButton{color:#f4f4f5;background:#34343a;border:0;"
            "border-radius:6px;padding:0 5px;}"
            "QToolButton:checked,QToolButton:pressed{background:#536278;}"
            "QToolButton:disabled{color:#98989f;background:#2c2c31;}"
            "QScrollBar:horizontal{height:5px;background:#252529;}"
            "QScrollBar::handle:horizontal{background:#70707a;min-width:20px;}"
            "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}"));
        auto *layout=new QHBoxLayout(this);
        layout->setContentsMargins(2,2,2,2);
        layout->setSpacing(2);
        m_tool=new QToolButton(this);
        m_tool->setFixedHeight(44);
        m_tool->setToolTip(QObject::tr("Choose a tool"));
        connect(m_tool,&QToolButton::clicked,this,[this]() {
            if(chooseTool)chooseTool();
        });
        layout->addWidget(m_tool);
        m_scroll=new QScrollArea(this);
        m_scroll->setWidgetResizable(true);
        m_scroll->setMinimumWidth(0);
        MobileTouchScroll::configure(m_scroll);
        layout->addWidget(m_scroll,1);
        auto *gear=new QToolButton(this);
        gear->setDefaultAction(options);
        gear->setToolButtonStyle(Qt::ToolButtonIconOnly);
        gear->setIconSize(QSize(22,22));
        gear->setFixedSize(44,44);
        gear->setAccessibleName(QObject::tr("Advanced Tool Options"));
        gear->setToolTip(gear->accessibleName());
        layout->addWidget(gear);
        m_sync=new QTimer(this);
        m_sync->setInterval(180);
        connect(m_sync,&QTimer::timeout,this,[this]() {
            if(isVisible())refreshControls();
        });
    }

    std::function<void()> chooseTool;
    std::function<void()> openTextFormatting;
    std::function<void(bool)> chooseFillResource;

    void setTool(const QString &id,const QString &label,
                 const QList<QPointer<QWidget>> &options,
                 const MobileEngineBridge &engine,KoToolBase *nativeTool)
    {
        if(m_id==id && m_options==options && m_nativeTool==nativeTool) {
            refreshControls();
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
            connect(control,&QToolButton::clicked,this,[this,gradient]() {
                chooseFillResource(gradient);
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
        if(finishablePath) {
            const QPointer<KoToolBase> path=nativeTool;
            connect(button(QObject::tr("Finish")),&QToolButton::clicked,this,[this,path]() {
                if(path && m_nativeTool==path.data())path->requestStrokeEnd();
            });
            connect(button(QObject::tr("Cancel")),&QToolButton::clicked,this,[this,path]() {
                if(path && m_nativeTool==path.data())path->requestStrokeCancellation();
            });
        }
        if(shortId.startsWith(QStringLiteral("KisToolSelect"))) {
            addSectionChoices("sectionAction",QObject::tr("Combine"),{
                QObject::tr("Replace"),QObject::tr("Intersect"),QObject::tr("Add"),
                QObject::tr("Subtract"),QObject::tr("Symmetric difference")
            });
            named("sliderThreshold",QObject::tr("Tol."));
            named("sliderCloseGap",QObject::tr("Gap"));
            addSectionChoices("sectionMode",QObject::tr("Type"),
                              {QObject::tr("Pixel"),QObject::tr("Vector")});
            native("deselect",QObject::tr("Deselect"));
            if(shortId==QStringLiteral("KisToolSelectMagnetic")) {
                named("radius",QObject::tr("Radius"));
                named("threshold",QObject::tr("Edge"));
                named("frequency",QObject::tr("Search"));
                named("anchorgap",QObject::tr("Anchor gap"));
            } else {
                named("sliderGrowSelection",QObject::tr("Grow"));
                named("sliderFeather",QObject::tr("Feather"));
                named("checkBoxAntiAliasSelection",QObject::tr("Anti-alias"));
            }
            // Keep everyday selection controls visible; preserve the full
            // native command set in a single touch-friendly menu.
            nativeMenu(QObject::tr("Selection"),{
                {"select_all",QObject::tr("Select all")},
                {"reselect",QObject::tr("Reselect")},
                {"invert_selection",QObject::tr("Invert")},
                {"edit_selection",QObject::tr("Edit selection")}
            });
            nativeMenu(QObject::tr("Edges"),{
                {"growselection",QObject::tr("Grow")},
                {"shrinkselection",QObject::tr("Shrink")},
                {"featherselection",QObject::tr("Feather")},
                {"borderselection",QObject::tr("Border")},
                {"smoothselection",QObject::tr("Smooth")}
            });
            nativeMenu(QObject::tr("Convert"),{
                {"convert_to_vector_selection",QObject::tr("Vector selection")},
                {"convert_to_raster_selection",QObject::tr("Raster selection")},
                {"colorrange",QObject::tr("Color range")}
            });
            nativeMenu(QObject::tr("Extract"),{
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
            modeOption("translateXBox",QObject::tr("X"));
            modeOption("translateYBox",QObject::tr("Y"));
            modeOption("scaleXBox",QObject::tr("Width %"));
            modeOption("scaleYBox",QObject::tr("Height %"));
            modeOption("densityBox",QObject::tr("Grid"));
            modeOption("liquifySizeSlider",QObject::tr("Size"));
            modeOption("liquifyAmountSlider",QObject::tr("Strength"));
            modeOption("liquifyFlowSlider",QObject::tr("Speed"));
            modeOption("liquifyReverseDirectionChk",QObject::tr("Reverse"));
            modeOption("chkShowControlPoints",QObject::tr("Mesh points"));
            modeOption("chkSymmetricalHandles",QObject::tr("Symmetric"));
            if(auto *box=qobject_cast<QDialogButtonBox*>(findOption("buttonBox"))) {
                addOption(box->button(QDialogButtonBox::Apply),QObject::tr("Apply"));
                addOption(box->button(QDialogButtonBox::Reset),QObject::tr("Reset"));
            }
            addNamedChoices(QObject::tr("Adjust"),{
                {"flipXButton",QObject::tr("Flip H")},
                {"flipYButton",QObject::tr("Flip V")},
                {"rotateCCWButton",QObject::tr("Rotate left")},
                {"rotateCWButton",QObject::tr("Rotate right")}
            });
        } else if(shortId==QStringLiteral("KisToolFill")) {
            addSectionChoices("sectionWhatToFill",QObject::tr("Fill area"),
                {QObject::tr("Selection"),QObject::tr("Contiguous"),QObject::tr("Similar color")});
            addSectionChoices("sectionFillWith",QObject::tr("Source"),
                {QObject::tr("Foreground"),QObject::tr("Background"),QObject::tr("Pattern")});
            addFillResource(false);
            named("sliderThreshold",QObject::tr("Tol."));
            named("sliderCloseGap",QObject::tr("Gap"));
            addSectionChoices("sectionReference",QObject::tr("Reference"),
                {QObject::tr("Current layer"),QObject::tr("All layers"),QObject::tr("Color labels")});
            named("checkBoxAntiAlias",QObject::tr("Anti-alias"));
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
            named("sliderThreshold",QObject::tr("Tol."));
            named("sliderCloseGap",QObject::tr("Gap"));
            named("checkBoxSelectionAsBoundary",QObject::tr("Selection boundary"));
            addSectionChoices("sectionReference",QObject::tr("Reference"),
                {QObject::tr("Current layer"),QObject::tr("All layers"),QObject::tr("Color labels")});
        } else if(shortId==QStringLiteral("ToolReferenceImages")) {
            addNamedChoices(QObject::tr("Add reference"),{
                {"bnAddReferenceImage",QObject::tr("From file")},
                {"bnPasteReferenceImage",QObject::tr("From clipboard")},
                {"bnAddReferenceImageFromCurrentLayer",QObject::tr("From active layer")},
                {"bnAddReferenceImageFromVisible",QObject::tr("From visible layers")}
            });
            named("opacitySlider",QObject::tr("Opacity"));
            named("saturationSlider",QObject::tr("Saturation"));
            named("chkKeepAspectRatio",QObject::tr("Lock ratio"));
            named("bnDeleteSelectedImages",QObject::tr("Remove"));
            addNamedChoices(QObject::tr("Reference set"),{
                {"bnLoad",QObject::tr("Load set")},
                {"bnSave",QObject::tr("Save set")}
            });
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
            named("chkUseEdgeDetection",QObject::tr("Detect edges"));
            named("intEdgeDetectionSize",QObject::tr("Edge size"));
            named("intRadius",QObject::tr("Gap"));
            named("intCleanUp",QObject::tr("Clean up"));
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
            named("unitsCombobox",QObject::tr("Units"));
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
            named("dither_check",QObject::tr("Dither"));
            named("threshold_slider",QObject::tr("Anti-alias"));
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
            named("intX",QObject::tr("X"));
            named("intY",QObject::tr("Y"));
            named("boolGrow",QObject::tr("Grow canvas"));
        } else if(shortId==QStringLiteral("KisAssistantTool")) {
            named("availableAssistantsComboBox",QObject::tr("Guide type"));
            named("localAssistantCheckbox",QObject::tr("Local"));
            named("twoPointUseVerticalCheckbox",QObject::tr("Vertical"));
            named("twoPointDensitySpinbox",QObject::tr("Density"));
            named("subdivisionsSpinbox",QObject::tr("Divisions"));
            addNamedChoices(QObject::tr("Guide files"),{
                {"loadAssistantButton",QObject::tr("Import guides")},
                {"saveAssistantButton",QObject::tr("Export guides")}
            });
            native("view_toggle_painting_assistants",QObject::tr("Guides"));
            native("view_toggle_assistant_previews",QObject::tr("Preview"));
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
            named("intervalXSpinBox",QObject::tr("X spacing"));
            named("intervalYSpinBox",QObject::tr("Y spacing"));
        } else if(shortId==QStringLiteral("SvgTextTool")) {
            named("mobileEditSvgText",QObject::tr("Edit text"));
            if(openTextFormatting) {
                auto *format=button(QObject::tr("Format"));
                connect(format,&QToolButton::clicked,this,[this]() {
                    if(openTextFormatting)openTextFormatting();
                });
            }
            named("mobileSvgGlyphs",QObject::tr("Glyphs"));
        }
        named("cmbOutline",QObject::tr("Outline"));
        named("cmbFill",QObject::tr("Fill"));
        if(m_row->count()<4)addCommonOptions();
        if(m_row->count()==0)
            m_row->addWidget(new QLabel(QObject::tr("Tool options"),content));
        m_row->addStretch();
        m_scroll->setWidget(content);
        // Each tool starts with its most common controls; a long strip on
        // the previous tool must not leave the new tool scrolled to the end.
        m_scroll->horizontalScrollBar()->setValue(0);
        refreshControls();
    }

protected:
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
        control->setFixedHeight(44);
        m_row->addWidget(control);
        return control;
    }

    void refreshControls()
    {
        for(const auto &refresh:m_refresh)refresh();
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
        connect(control,&QToolButton::clicked,this,[this,control,choices]() {
            QMenu menu;
            styleMenu(menu);
            const quint64 generation=m_generation;
            const QPointer<MobileContextToolBar> alive=this;
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
            auto *picked=menu.exec(control->mapToGlobal(QPoint(0,control->height())));
            if(picked && alive) {
                const auto action=choices.at(picked->data().toInt()).action;
                QTimer::singleShot(0,alive.data(),
                    [this,generation,invokedTool,action]() {
                    // Android can briefly hide the floating toolbar while
                    // closing a QMenu. Visibility is not proof the active
                    // native tool changed; dropping these callbacks made
                    // selection/shape commands intermittently do nothing.
                    // Reject stale callbacks by tool identity and generation.
                    if(generation==m_generation && invokedTool &&
                       m_nativeTool==invokedTool.data() &&
                       action && action->isEnabled())
                        action->trigger();
                });
            }
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
            setValueLabel(control,label,active);
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
            {"freeTransformButton","slotSetFreeTransformModeButtonClicked",QObject::tr("Free"),"FreeTransformMode"},
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
            for(int i=0;i<choices.size();++i) {
                const auto &choice=choices.at(i);
                auto *item=menu.addAction(choice.label);
                item->setData(i);
                item->setCheckable(true);
                item->setChecked(m_nativeTool && choice.mode>=0 &&
                    m_nativeTool->property("transformMode").toInt()==choice.mode);
                item->setEnabled(choice.config && optionAvailable(choice.button));
            }
            auto *picked=menu.exec(control->mapToGlobal(QPoint(0,control->height())));
            if(!picked)return;
            const int index=picked->data().toInt();
            if(index<0 || index>=choices.size())return;
            const Choice choice=choices.at(index);
            QTimer::singleShot(0,this,[this,generation,choice]() {
                // QMenu may briefly hide the floating bar while Android
                // transfers focus. Visibility is NOT proof that the native
                // Transform tool was deactivated, and was dropping taps.
                if(generation!=m_generation ||
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
            setValueLabel(control,QObject::tr("Mode"),active);
        });
    }

    void addChoices(const QString &label,const QList<Choice> &choices)
    {
        if(choices.isEmpty())return;
        auto *control=button(label);
        connect(control,&QToolButton::clicked,this,[this,control,choices]() {
            QMenu menu;
            styleMenu(menu);
            const quint64 generation=m_generation;
            const QPointer<MobileContextToolBar> alive=this;
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
            auto *picked=menu.exec(control->mapToGlobal(QPoint(0,control->height())));
            if(picked && alive) {
                const auto source=choices.at(picked->data().toInt()).button;
                QTimer::singleShot(0,alive.data(),[this,generation,source]() {
                    if(generation==m_generation && optionAvailable(source))
                        source->click();
                });
            }
        });
        m_refresh.append([this,control,label,choices]() {
            bool enabled=false;
            QString active;
            for(const auto &choice:choices) {
                enabled|=optionAvailable(choice.button);
                if(choice.button && choice.button->isChecked())active=choice.label;
            }
            control->setEnabled(enabled);
            setValueLabel(control,label,active);
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
            connect(control,&QToolButton::clicked,this,[this,control,native]() {
                if(!optionAvailable(native))return;
                QMenu menu;
                styleMenu(menu);
                const quint64 generation=m_generation;
                const QPointer<QObject> invokedTool=m_nativeTool;
                for(int i=0;i<native->count();++i) {
                    const QString text=native->itemText(i);
                    if(text.isEmpty())continue;
                    auto *item=menu.addAction(text);
                    const auto index=native->model()->index(i,native->modelColumn(),
                                                           native->rootModelIndex());
                    item->setEnabled(index.flags().testFlag(Qt::ItemIsEnabled));
                    item->setCheckable(true);
                    item->setChecked(i==native->currentIndex());
                    connect(item,&QAction::triggered,this,
                            [this,generation,invokedTool,native,i,text]() {
                        if(generation!=m_generation || !invokedTool ||
                           m_nativeTool!=invokedTool.data() ||
                           !optionAvailable(native) || i>=native->count() ||
                           native->itemText(i)!=text)return;
                        native->setCurrentIndex(i);
                        if(native)Q_EMIT native->activated(i);
                    });
                }
                menu.exec(control->mapToGlobal(QPoint(0,control->height())));
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
        connect(control,&QToolButton::clicked,this,[this,label,native]() {
            if(!optionAvailable(native))return;
            const quint64 generation=m_generation;
            const QPointer<MobileContextToolBar> alive=this;
            QDialog dialog(window());
            dialog.setWindowTitle(label);
            dialog.setStyleSheet(QStringLiteral(
                "QDialog{background:#252529;}"
                "QLabel{color:#f4f4f5;}"
                "QPushButton{min-height:44px;padding:3px 12px;}"));
            auto *layout=new QVBoxLayout(&dialog);
            layout->addWidget(new QLabel(label,&dialog));
            using Editor=std::conditional_t<std::is_same<SpinBox,QDoubleSpinBox>::value,
                                            KisDoubleSliderSpinBox,KisSliderSpinBox>;
            auto *editor=new Editor(&dialog);
            if constexpr(std::is_same<SpinBox,QDoubleSpinBox>::value)
                editor->setRange(native->minimum(),native->maximum(),native->decimals());
            else
                editor->setRange(native->minimum(),native->maximum());
            editor->setSingleStep(native->singleStep());
            if constexpr(!std::is_same<SpinBox,QSlider>::value)
                editor->setSuffix(native->suffix());
            editor->setValue(native->value());
            editor->setMinimumSize(220,44);
            layout->addWidget(editor);
            auto *buttons=new QDialogButtonBox(
                QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
            connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
            connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
            layout->addWidget(buttons);
            if(dialog.exec()==QDialog::Accepted && alive &&
               generation==alive->m_generation && alive->isVisible() &&
               alive->optionAvailable(native)) {
                native->setValue(editor->value());
                if constexpr(!std::is_same<SpinBox,QSlider>::value) {
                    if(native)Q_EMIT native->editingFinished();
                }
            }
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

    static void setValueLabel(QToolButton *control,const QString &label,const QString &value)
    {
        const QString text=value.isEmpty()?label:QStringLiteral("%1: %2").arg(label,value);
        setChipText(control,text);
        control->setToolTip(text);
        control->setAccessibleName(text);
    }

    static void setChipText(QToolButton *control,const QString &text,int limit=124)
    {
        const auto metrics=control->fontMetrics();
        const QString shown=metrics.elidedText(text,Qt::ElideRight,limit);
        control->setText(shown);
        control->setFixedWidth(qMax(44,metrics.horizontalAdvance(shown)+14));
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
    QHBoxLayout *m_row=nullptr;
    QTimer *m_sync=nullptr;
    quint64 m_generation=0;
};
#endif
