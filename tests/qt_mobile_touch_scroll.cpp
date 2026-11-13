/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <QApplication>
#include <QListWidget>
#include <QScrollArea>
#include <QStringListModel>
#include <QtTest/QTest>
#include <QtTest/qtesttouch.h>
#include "mobile_core/touch_scroll.h"

static void verify(bool pass,const char *message)
{
    if(!pass)qFatal("Mobile touch scroll: %s",message);
}

static void pointer(QWidget *target,QEvent::Type type,const QPoint &pos,
                    Qt::MouseEventSource source=Qt::MouseEventNotSynthesized)
{
    const auto button=type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton;
    const auto held=type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton;
    QMouseEvent event(type,pos,target->mapTo(target->window(),pos),
                      target->mapToGlobal(pos),button,held,Qt::NoModifier,source);
    QApplication::sendEvent(target,&event);
}

static void mouseTap(QWidget *target,const QPoint &pos,Qt::MouseEventSource source)
{
    pointer(target,QEvent::MouseButtonPress,pos,source);
    pointer(target,QEvent::MouseButtonRelease,pos,source);
}

static void mouseSwipe(QWidget *target,const QPoint &start,Qt::MouseEventSource source)
{
    pointer(target,QEvent::MouseButtonPress,start,source);
    for(int y=15;y<=100;y+=15) {
        QTest::qWait(12);
        pointer(target,QEvent::MouseMove,start-QPoint(0,y),source);
    }
    pointer(target,QEvent::MouseButtonRelease,start-QPoint(0,100),source);
}

static void checkView(QListView &view,int &clicks)
{
    view.setViewMode(QListView::IconMode);
    view.setGridSize(QSize(68,62));
    view.setWrapping(true);
    view.setMovement(QListView::Static);
    view.resize(240,225);
    MobileTouchScroll::configureList(&view);
    view.show();
    QTest::qWait(35);
    auto *viewport=view.viewport();
    const auto tile=view.visualRect(view.model()->index(0,0)).center();
    QPoint swipeStart;
    for(int row=0;row<view.model()->rowCount();++row) {
        const auto center=view.visualRect(view.model()->index(row,0)).center();
        if(center.y()>130 && viewport->rect().contains(center)) {
            swipeStart=center;
            break;
        }
    }
    verify(view.indexAt(tile).isValid() && view.indexAt(swipeStart).isValid(),
           "fixture points must hit real items");

    const auto reset=[&]() {
        auto *scroller=QScroller::scroller(viewport);
        QTest::qWait(50);
        scroller->stop();
        verify(view.verticalScrollBar()->value()>0,"swipe did not scroll the item view");
        view.verticalScrollBar()->setValue(0);
    };
    for(auto source:{Qt::MouseEventNotSynthesized,Qt::MouseEventSynthesizedByQt,
                     Qt::MouseEventSynthesizedBySystem}) {
        const int before=clicks;
        pointer(viewport,QEvent::MouseButtonPress,tile,source);
        verify(clicks==before,"press activated an item before release");
        pointer(viewport,QEvent::MouseButtonRelease,tile,source);
        verify(clicks==before+1,"mouse or synthesized mouse tap did not click exactly once");
        mouseSwipe(viewport,swipeStart,source);
        verify(clicks==before+1,"mouse or synthesized mouse swipe activated an item");
        reset();
    }

    auto *device=QTest::createTouchDevice();
    const int before=clicks;
    QTest::touchEvent(view.windowHandle(),device).press(0,viewport->mapTo(&view,tile),view.windowHandle());
    pointer(viewport,QEvent::MouseButtonPress,tile,Qt::MouseEventSynthesizedBySystem);
    QTest::touchEvent(view.windowHandle(),device).release(0,viewport->mapTo(&view,tile),view.windowHandle());
    pointer(viewport,QEvent::MouseButtonRelease,tile,Qt::MouseEventSynthesizedBySystem);
    verify(clicks==before+1,"raw touch tap did not click exactly once");
    mouseTap(viewport,tile,Qt::MouseEventSynthesizedBySystem);
    verify(clicks==before+1,"Android-style duplicate mouse tap activated the item");

    QTest::touchEvent(view.windowHandle(),device).press(0,viewport->mapTo(&view,swipeStart),view.windowHandle());
    for(int y=15;y<=100;y+=15) {
        QTest::qWait(12);
        QTest::touchEvent(view.windowHandle(),device).move(0,viewport->mapTo(&view,swipeStart-QPoint(0,y)),view.windowHandle());
    }
    QTest::touchEvent(view.windowHandle(),device).release(0,viewport->mapTo(&view,swipeStart-QPoint(0,100)),view.windowHandle());
    verify(clicks==before+1,"raw touch swipe activated an item");
    reset();

    mouseTap(viewport,tile,Qt::MouseEventNotSynthesized);
    verify(clicks==before+2,"mouse tap after a raw swipe did not activate");
    int holds=0;
    MobileTouchScroll::configureList(&view,[&](const QModelIndex &index) {
        verify(index==view.indexAt(tile),"held a different item");
        ++holds;
    });
    QTest::qWait(210);
    pointer(viewport,QEvent::MouseButtonPress,tile);
    QTest::qWait(550);
    pointer(viewport,QEvent::MouseButtonRelease,tile);
    verify(holds==1 && clicks==before+2,"mouse hold did not suppress click");

    const auto mapped=viewport->mapTo(&view,tile);
    QTest::touchEvent(view.windowHandle(),device).press(0,mapped,view.windowHandle());
    QTest::qWait(550);
    QTest::touchEvent(view.windowHandle(),device).release(0,mapped,view.windowHandle());
    mouseTap(viewport,tile,Qt::MouseEventSynthesizedBySystem);
    verify(holds==2 && clicks==before+2,"touch hold or synthesized release activated item");
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
    int actions=0;
    QObject::connect(action,&QPushButton::clicked,&sheet,[&]() {++actions;});
    const auto start=action->rect().center();
    mouseTap(action,start,Qt::MouseEventNotSynthesized);
    verify(actions==1,"stationary tap did not activate the sheet action once");
    mouseSwipe(action,start,Qt::MouseEventNotSynthesized);
    verify(actions==1 && sheet.verticalScrollBar()->value()>0,"sheet swipe regressed");
    sheet.verticalScrollBar()->setValue(0);
    sheet.setProperty("_mobileScrollGaps",true);
    const auto gap=sheet.viewport()->rect().center();
    mouseSwipe(sheet.viewport(),gap,Qt::MouseEventSynthesizedBySystem);
    verify(actions==1 && sheet.verticalScrollBar()->value()>0,
           "pen-generated drag on a menu gap did not scroll");
    sheet.hide();

    QStringList names;
    for(int i=0;i<40;++i)names.append(QString::number(i));
    QListWidget projects;
    projects.addItems(names);
    int projectClicks=0;
    QObject::connect(&projects,&QListWidget::itemClicked,&projects,[&]() {++projectClicks;});
    checkView(projects,projectClicks);
    projects.hide();

    QStringListModel presets(names);
    QListView brushes;
    brushes.setModel(&presets);
    int presetClicks=0;
    QObject::connect(&brushes,&QListView::clicked,&brushes,[&]() {++presetClicks;});
    checkView(brushes,presetClicks);
}
