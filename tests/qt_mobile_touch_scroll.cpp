/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <QApplication>
#include <QListWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include "mobile_core/touch_scroll.h"

static void verify(bool pass,const char *message)
{
    if(!pass)qFatal("Mobile touch scroll: %s",message);
}

static void pointer(QWidget *target,QEvent::Type type,const QPoint &global)
{
    const auto button=type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton;
    const auto held=type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton;
    QMouseEvent event(type,target->mapFromGlobal(global),global,button,held,Qt::NoModifier);
    QApplication::sendEvent(target,&event);
}

static void drag(QWidget *target,const QPoint &start)
{
    const QPoint end=start-QPoint(0,QApplication::startDragDistance()+40);
    pointer(target,QEvent::MouseButtonPress,start);
    pointer(target,QEvent::MouseMove,end);
    pointer(target,QEvent::MouseButtonRelease,end);
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QScrollArea sheet;
    sheet.resize(240,200);
    auto *body=new QWidget;
    auto *layout=new QVBoxLayout(body);
    auto *action=new QPushButton(QStringLiteral("Layer options"),body);
    layout->addWidget(action);
    layout->addSpacing(500);
    sheet.setWidgetResizable(true);
    sheet.setWidget(body);
    MobileTouchScroll::configure(&sheet);
    sheet.show();
    QApplication::processEvents();

    int clicks=0;
    QObject::connect(action,&QPushButton::clicked,&sheet,[&]() {++clicks;});
    const QPoint start=action->mapToGlobal(action->rect().center());
    pointer(action,QEvent::MouseButtonPress,start);
    verify(clicks==0,"press activated a sheet action");
    pointer(action,QEvent::MouseButtonRelease,start);
    verify(clicks==1,"stationary tap did not activate exactly once");
    drag(action,start);
    verify(clicks==1,"swipe activated a sheet action");
    verify(sheet.verticalScrollBar()->value()>0,"swipe did not scroll the sheet");

    QListWidget modes;
    modes.resize(240,180);
    for(int i=0;i<40;++i)modes.addItem(QString::number(i));
    MobileTouchScroll::configureList(&modes);
    modes.show();
    QApplication::processEvents();
    clicks=0;
    QObject::connect(&modes,&QListWidget::itemClicked,&modes,[&]() {++clicks;});
    auto *viewport=modes.viewport();
    const auto row=viewport->mapToGlobal(modes.visualItemRect(modes.item(0)).center());
    drag(viewport,row);
    verify(clicks==0,"swipe selected a blend mode");
    verify(modes.verticalScrollBar()->value()>0,"swipe did not scroll the list");
    modes.verticalScrollBar()->setValue(0);
    pointer(viewport,QEvent::MouseButtonPress,row);
    pointer(viewport,QEvent::MouseButtonRelease,row);
    verify(clicks==1,"list tap did not select exactly once");
}
