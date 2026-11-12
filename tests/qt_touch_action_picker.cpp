// SPDX-License-Identifier: GPL-2.0-or-later
#include "mobile_core/touch_action_picker.h"

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
    return mobilePickAction(&window,menu)==mask && phase==3?0:1;
}
