/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Compact controls delegate to the active tool's native widgets or actions.
 * The full Tool Options panel remains the only owner of the native layout.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QAbstractButton>
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
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
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
    explicit MobileContextToolBar(QWidget *parent) : QFrame(parent)
    {
        setObjectName(QStringLiteral("mobileContextToolBar"));
        setStyleSheet(QStringLiteral(
            "QFrame#mobileContextToolBar{background:#252529;"
            "border:1px solid #48484e;border-radius:10px;}"
            "QWidget#mobileContextControls,QScrollArea{background:#252529;border:0;}"
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
        m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scroll->setMinimumWidth(0);
        m_scroll->viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        QScroller::grabGesture(m_scroll->viewport(),QScroller::LeftMouseButtonGesture);
        layout->addWidget(m_scroll,1);
        auto *gear=new QToolButton(this);
        gear->setIcon(QIcon(QStringLiteral(":/light_configure.svg")));
        gear->setIconSize(QSize(22,22));
        gear->setFixedSize(44,44);
        gear->setAccessibleName(QObject::tr("Advanced Tool Options"));
        gear->setToolTip(gear->accessibleName());
        connect(gear,&QToolButton::clicked,this,[this]() {
            if(openOptions)openOptions();
        });
        layout->addWidget(gear);
        m_sync=new QTimer(this);
        m_sync->setInterval(180);
        connect(m_sync,&QTimer::timeout,this,[this]() {
            if(isVisible())refreshControls();
        });
    }

    std::function<void()> chooseTool;
    std::function<void()> openOptions;
    std::function<void()> openTextFormatting;

    void setTool(const QString &id,const QString &label,
                 const QList<QPointer<QWidget>> &options,
                 const MobileEngineBridge &engine)
    {
        if(m_id==id && m_options==options) {
            refreshControls();
            return;
        }
        m_id=id;
        m_options=options;
        ++m_generation;
        m_refresh.clear();
        m_used.clear();
        const QString shortLabel=id==QStringLiteral("KisToolSelectContiguous")?
            QObject::tr("Wand"):id==QStringLiteral("KisToolSelectOutline")?
            QObject::tr("Lasso"):id==QStringLiteral("KisToolSelectRectangular")?
            QObject::tr("Rect"):id==QStringLiteral("KisToolSelectElliptical")?
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
        if(id.startsWith(QStringLiteral("KisToolSelect"))) {
            addSectionChoices("sectionAction",QObject::tr("Combine"),{
                QObject::tr("Replace"),QObject::tr("Intersect"),QObject::tr("Add"),
                QObject::tr("Subtract"),QObject::tr("Symmetric difference")
            });
            named("sliderThreshold",QObject::tr("Tol."));
            named("sliderCloseGap",QObject::tr("Gap"));
            addSectionChoices("sectionMode",QObject::tr("Type"),
                              {QObject::tr("Pixel"),QObject::tr("Vector")});
            native("select_all",QObject::tr("Select all"));
            native("invert_selection",QObject::tr("Invert"));
            native("deselect",QObject::tr("Deselect"));
            native("growselection",QObject::tr("Grow"));
            native("shrinkselection",QObject::tr("Shrink"));
            native("featherselection",QObject::tr("Feather"));
            native("copy_merged",QObject::tr("Copy merged"));
            native("copy_selection_to_new_layer",QObject::tr("Copy to layer"));
        } else if(id==QStringLiteral("KisToolTransform")) {
            addNamedChoices(QObject::tr("Transform mode"),{
                {"freeTransformButton",QObject::tr("Free")},
                {"perspectiveTransformButton",QObject::tr("Perspective")},
                {"warpButton",QObject::tr("Warp")},
                {"cageButton",QObject::tr("Cage")},
                {"liquifyButton",QObject::tr("Liquify")},
                {"meshButton",QObject::tr("Mesh")}
            });
            if(auto *box=qobject_cast<QDialogButtonBox*>(findOption("buttonBox"))) {
                addOption(box->button(QDialogButtonBox::Apply),QObject::tr("Apply"));
                addOption(box->button(QDialogButtonBox::Reset),QObject::tr("Reset"));
            }
            named("flipXButton",QObject::tr("Flip H"));
            named("flipYButton",QObject::tr("Flip V"));
            named("rotateCCWButton",QObject::tr("Rotate left"));
            named("rotateCWButton",QObject::tr("Rotate right"));
        } else if(id==QStringLiteral("KritaFill/KisToolFill")) {
            addSectionChoices("sectionWhatToFill",QObject::tr("Fill area"),
                {QObject::tr("Selection"),QObject::tr("Contiguous"),QObject::tr("Similar color")});
            addSectionChoices("sectionFillWith",QObject::tr("Source"),
                {QObject::tr("Foreground"),QObject::tr("Background"),QObject::tr("Pattern")});
            named("sliderThreshold",QObject::tr("Tol."));
            named("sliderCloseGap",QObject::tr("Gap"));
            addSectionChoices("sectionReference",QObject::tr("Reference"),
                {QObject::tr("Current layer"),QObject::tr("All layers"),QObject::tr("Color labels")});
            named("checkBoxAntiAlias",QObject::tr("Anti-alias"));
        } else if(id==QStringLiteral("KritaFill/KisToolGradient")) {
            named("shape_combo",QObject::tr("Shape"));
            named("repeat_combo",QObject::tr("Repeat"));
            named("reverse_check",QObject::tr("Reverse"));
            named("dither_check",QObject::tr("Dither"));
        } else if(id==QStringLiteral("KritaTransform/KisToolMove")) {
            addNamedChoices(QObject::tr("Move target"),{
                {"radioSelectedLayer",QObject::tr("Selected layer")},
                {"radioFirstLayer",QObject::tr("Layer under pointer")},
                {"radioGroup",QObject::tr("Group under pointer")}
            });
            named("spinMoveStep",QObject::tr("Step"));
        } else if(id==QStringLiteral("KisToolCrop")) {
            named("cmbType",QObject::tr("Crop target"));
            named("intWidth",QObject::tr("Width"));
            named("intHeight",QObject::tr("Height"));
            named("lockRatioButton",QObject::tr("Lock ratio"));
            named("bnCrop",QObject::tr("Apply crop"));
        } else if(id==QStringLiteral("KisAssistantTool")) {
            named("availableAssistantsComboBox",QObject::tr("Guide type"));
            named("localAssistantCheckbox",QObject::tr("Local"));
            native("view_toggle_painting_assistants",QObject::tr("Guides"));
            native("view_toggle_assistant_previews",QObject::tr("Preview"));
        } else if(id==QStringLiteral("KritaSelected/KisToolColorSampler")) {
            named("cmbSources",QObject::tr("Sample from"));
            named("radius",QObject::tr("Radius"));
            named("blend",QObject::tr("Blend"));
            named("cbUpdateCurrentColor",QObject::tr("Update color"));
        } else if(id==QStringLiteral("KritaShape/KisToolMultiBrush")) {
            named("multihandTypeCombobox",QObject::tr("Symmetry"));
            named("showAxesCheckbox",QObject::tr("Show axes"));
        } else if(id==QStringLiteral("SvgTextTool")) {
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

    void addChoices(const QString &label,const QList<Choice> &choices)
    {
        if(choices.isEmpty())return;
        auto *control=button(label);
        connect(control,&QToolButton::clicked,this,[this,control,choices]() {
            QMenu menu;
            styleMenu(menu);
            const quint64 generation=m_generation;
            for(const auto &choice:choices) {
                auto *item=menu.addAction(choice.label);
                if(choice.button) {
                    item->setEnabled(optionAvailable(choice.button));
                    item->setCheckable(choice.button->isCheckable());
                    item->setChecked(choice.button->isChecked());
                } else {
                    item->setEnabled(false);
                }
                connect(item,&QAction::triggered,this,[this,generation,choice]() {
                    if(generation!=m_generation || !isVisible())return;
                    if(optionAvailable(choice.button))choice.button->click();
                });
            }
            menu.exec(control->mapToGlobal(QPoint(0,control->height())));
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
        auto *strip=section?qobject_cast<KisOptionButtonStrip*>(section->primaryWidget()):nullptr;
        if(!strip)return;
        const auto buttons=strip->buttons();
        if(buttons.size()!=labels.size())return;
        QList<Choice> choices;
        for(int i=0;i<buttons.size();++i) {
            choices.append({labels.at(i),buttons.at(i)});
            m_used.insert(buttons.at(i));
        }
        addChoices(label,choices);
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
                for(int i=0;i<native->count();++i) {
                    const QString text=native->itemText(i);
                    if(text.isEmpty())continue;
                    auto *item=menu.addAction(text);
                    const auto index=native->model()->index(i,native->modelColumn(),
                                                           native->rootModelIndex());
                    item->setEnabled(index.flags().testFlag(Qt::ItemIsEnabled));
                    item->setCheckable(true);
                    item->setChecked(i==native->currentIndex());
                    connect(item,&QAction::triggered,this,[this,generation,native,i,text]() {
                        if(generation!=m_generation || !isVisible() ||
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
            using Editor=std::conditional_t<std::is_same<SpinBox,QSpinBox>::value,
                                            KisSliderSpinBox,KisDoubleSliderSpinBox>;
            auto *editor=new Editor(&dialog);
            if constexpr(std::is_same<SpinBox,QDoubleSpinBox>::value)
                editor->setRange(native->minimum(),native->maximum(),native->decimals());
            else
                editor->setRange(native->minimum(),native->maximum());
            editor->setSingleStep(native->singleStep());
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
               alive->optionAvailable(native))native->setValue(editor->value());
        });
        m_refresh.append([this,control,label,native]() {
            control->setEnabled(optionAvailable(native));
            setValueLabel(control,label,native?native->cleanText():QString());
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
