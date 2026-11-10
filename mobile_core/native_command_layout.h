/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Floating touch controls for the independent Krita engine host.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QAction>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenu>
#include <QStackedWidget>
#include <QToolButton>

class MobileNativeCommandLayout final : public QObject
{
public:
    static bool install(QMainWindow *host,
                        const QList<QAction*> &documentActions,
                        const QList<QAction*> &workspaceActions,
                        const QList<QAction*> &moreActions,
                        QAction *canvasOnly=nullptr)
    {
        QWidget *canvas=host ? host->centralWidget() : nullptr;
        if(!canvas || panel(canvas,"_mobileNativeDocumentBar"))return false;
        auto *layout=new MobileNativeCommandLayout(canvas);
        layout->m_documents=makePanel(canvas,"_mobileNativeDocumentBar",
                                      documentActions);
        layout->m_workspace=makePanel(canvas,"_mobileNativeWorkspaceBar",
                                      workspaceActions);
        auto *more=new QToolButton(layout->m_documents);
        more->setObjectName(QStringLiteral("_mobileNativeMoreButton"));
        more->setText(QStringLiteral("..."));
        more->setAccessibleName(QObject::tr("More"));
        more->setToolTip(QObject::tr("More"));
        more->setFixedSize(44,44);
        more->setPopupMode(QToolButton::InstantPopup);
        auto *menu=new QMenu(more);
        const QString popupStyle=QStringLiteral(
            "QMenu{background:#252529;color:#f4f4f5;"
            "border:1px solid #55555c;padding:4px;}"
            "QMenu::item{color:#f4f4f5;padding:11px 17px;min-height:26px;}"
            "QMenu::item:selected{background:#45454f;}"
            "QMenu::item:disabled{color:#9a9aa2;}"
            "QMenu::separator{height:1px;background:#55555c;margin:4px 8px;}");
        menu->setStyleSheet(popupStyle);
        for(QAction *action:moreActions) {
            if(!action)continue;
            if(QMenu *submenu=action->menu())submenu->setStyleSheet(popupStyle);
            menu->addAction(action);
        }
        more->setMenu(menu);
        layout->m_documents->layout()->addWidget(more);

        if(canvasOnly) {
            auto *restore=new QToolButton(canvas);
            layout->m_restore=restore;
            restore->setObjectName(QStringLiteral("_mobileNativeRestoreControls"));
            restore->setText(QObject::tr("Show Controls"));
            restore->setMinimumSize(110,44);
            restore->setStyleSheet(QStringLiteral(
                "QToolButton{background:#252529;color:#f4f4f5;"
                "border:1px solid #55555c;border-radius:10px;padding:5px;}"));
            restore->adjustSize();
            restore->move(6,6);
            restore->hide();
            QObject::connect(restore,&QToolButton::clicked,canvasOnly,
                             [canvasOnly]() { canvasOnly->setChecked(false); });
            QObject::connect(canvasOnly,&QAction::toggled,layout,
                             [layout,restore](bool enabled) {
                layout->m_documents->setVisible(!enabled);
                layout->m_workspace->setVisible(!enabled);
                restore->setVisible(enabled);
                layout->position();
            });
        }

        canvas->installEventFilter(layout);
        layout->m_documents->show();
        layout->m_workspace->show();
        if(auto *stack=qobject_cast<QStackedWidget*>(canvas))
            QObject::connect(stack,&QStackedWidget::currentChanged,layout,
                             [layout](int) { layout->position(); });
        layout->position();
        return true;
    }

    static int panelTopInset(QWidget *canvas)
    {
        int inset=6;
        for(const char *name:{"_mobileNativeDocumentBar","_mobileNativeWorkspaceBar"}) {
            if(auto *bar=panel(canvas,name); bar && !bar->isHidden())
                inset=qMax(inset,bar->geometry().bottom()+7);
        }
        return inset;
    }

protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==parent() &&
           (event->type()==QEvent::Resize || event->type()==QEvent::Show))
            position();
        return false;
    }

private:
    explicit MobileNativeCommandLayout(QWidget *canvas) : QObject(canvas) {}

    static QFrame *panel(QWidget *canvas,const char *name)
    {
        return canvas->findChild<QFrame*>(QString::fromLatin1(name),
                                          Qt::FindDirectChildrenOnly);
    }

    static QFrame *makePanel(QWidget *canvas,const char *name,
                             const QList<QAction*> &actions)
    {
        auto *frame=new QFrame(canvas);
        frame->setObjectName(QString::fromLatin1(name));
        frame->setStyleSheet(QStringLiteral(
            "QFrame{background:#252529;border:1px solid #48484e;border-radius:13px;}"
            "QToolButton{background:transparent;color:#f4f4f5;border:0;"
            "border-radius:8px;padding:0;}"
            "QToolButton:pressed,QToolButton:checked{background:#484850;}"
            "QToolButton[mobileBrushMode=\"true\"]:checked{background:#316e88;}"
            "QToolButton:disabled{color:#9a9aa2;}"
            "QToolButton::menu-indicator{image:none;width:0;}"));
        auto *row=new QHBoxLayout(frame);
        row->setContentsMargins(3,3,3,3);
        row->setSpacing(1);
        for(QAction *action:actions) {
            if(!action)continue;
            auto *button=new QToolButton(frame);
            if(action->property("mobileBrushMode").toBool())
                button->setProperty("mobileBrushMode",true);
            button->setDefaultAction(action);
            button->setAccessibleName(action->text());
            QObject::connect(action,&QAction::changed,button,[action,button]() {
                button->setAccessibleName(action->text());
            });
            button->setIconSize(QSize(23,23));
            if(action->icon().isNull()) {
                button->setToolButtonStyle(Qt::ToolButtonTextOnly);
                button->setFixedSize(qMax(44,button->fontMetrics()
                    .horizontalAdvance(action->text())+6),44);
            } else {
                button->setToolButtonStyle(Qt::ToolButtonIconOnly);
                button->setFixedSize(44,44);
            }
            row->addWidget(button);
        }
        return frame;
    }

    void position()
    {
        auto *canvas=static_cast<QWidget*>(parent());
        m_workspace->adjustSize();
        m_documents->adjustSize();
        const int edge=6;
        const bool fits=canvas->width()>=m_workspace->width()+
                                        m_documents->width()+3*edge;
        m_workspace->move(edge,edge);
        m_documents->move(qMax(edge,canvas->width()-m_documents->width()-edge),
                          fits ? edge : m_workspace->geometry().bottom()+edge+1);
        m_workspace->raise();
        m_documents->raise();
        if(m_restore)m_restore->raise();
    }

    QFrame *m_documents=nullptr;
    QFrame *m_workspace=nullptr;
    QToolButton *m_restore=nullptr;
};
#endif
