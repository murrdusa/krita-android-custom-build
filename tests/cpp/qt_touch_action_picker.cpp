// SPDX-License-Identifier: GPL-2.0-or-later
#include "mobile_studio/touch_action_picker.h"

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QWidget window;
    window.resize(360,600);
    window.show();
    QMenu menu(QStringLiteral("Layers"));
    menu.addAction(QStringLiteral("Disabled"))->setEnabled(false);
    auto *submenu=menu.addMenu(QStringLiteral("Masks"));
    auto *mask=submenu->addAction(QStringLiteral("Filter mask"));
    QTimer click;
    int phase=0;
    QObject::connect(&click,&QTimer::timeout,&window,[&]() {
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto *list=dialog?dialog->findChild<QListWidget*>():nullptr;
        if(!list)return;
        const int row=phase==1?1:0;
        ++phase;
        const QPoint pos=list->visualItemRect(list->item(row)).center();
        QMouseEvent press(QEvent::MouseButtonPress,pos,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,pos,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(list->viewport(),&press);
        QApplication::sendEvent(list->viewport(),&release);
    });
    QTimer::singleShot(2000,&window,[]() {
        if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();
    });
    click.start(80);
    if(mobilePickAction(&window,menu)!=mask || phase!=3)return 1;
    click.stop();

    QWidget anchor(&window);
    anchor.setGeometry(100,50,80,40);
    anchor.show();
    QVariant picked;
    int calls=0;
    {
        QMenu options(QStringLiteral("Sample mode"));
        options.addAction(QStringLiteral("Disabled"))->setEnabled(false);
        options.addAction(QStringLiteral("Current layer"))->setData(42);
        mobileShowActionList(&window,&anchor,options,&window,[&](const QVariant &value) {
            ++calls;
            picked=value;
        });
    }
    auto *panel=window.findChild<QFrame*>(QStringLiteral("mobileContextOptionList"));
    auto *list=panel?panel->findChild<QListWidget*>():nullptr;
    if(!list || !panel->isVisible() || list->count()!=2)return 2;
    const auto tap=[list](int row) {
        const QPoint pos=list->visualItemRect(list->item(row)).center();
        QMouseEvent press(QEvent::MouseButtonPress,pos,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,pos,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(list->viewport(),&press);
        QApplication::sendEvent(list->viewport(),&release);
    };
    tap(0);
    if(!panel->isVisible() || calls)return 3;
    tap(1);
    if(panel->isVisible())return 4;
    QCoreApplication::processEvents();
    return calls==1 && picked.toInt()==42?0:5;
}
