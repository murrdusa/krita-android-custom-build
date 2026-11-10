/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Regression: native commands visible in floating control groups, real QActions
 * preserved, and existing Krita canvas not touched.
 */
#include <QApplication>
#include <QDebug>
#include <QFrame>
#include <QMainWindow>
#include <QPixmap>
#include <QMenu>
#include <QToolButton>
#include <QWidget>
#include "mobile_core/native_command_layout.h"

static void verify(bool pass,const char *message)
{
    if(!pass)qFatal("Native engine command layout: %s",message);
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QMainWindow host;
    host.resize(720,870);
    auto *canvas=new QWidget(&host);
    canvas->setObjectName(QStringLiteral("unchangedKritaCanvas"));
    host.setCentralWidget(canvas);

    QAction canvases(QObject::tr("Canvases"),&host);
    QAction home(QObject::tr("Home"),&host);
    QAction undo(QObject::tr("Undo"),&host);
    QAction redo(QObject::tr("Redo"),&host);
    QAction save(QObject::tr("Save"),&host);
    QAction brushes(QObject::tr("Brushes"),&host);
    QAction eraser(QObject::tr("Eraser"),&host);
    QAction blend(QObject::tr("Blend"),&host);
    QAction layers(QObject::tr("Layers"),&host);
    QAction color(QObject::tr("Color"),&host);
    QAction tools(QObject::tr("Tools"),&host);
    QAction newCanvas(QObject::tr("New"),&host);
    QAction open(QObject::tr("Open"),&host);
    QAction exit(QObject::tr("Exit test"),&host);
    QAction canvasOnly(QObject::tr("Canvas Only"),&host);
    canvasOnly.setCheckable(true);
    QPixmap icon(20,20);
    icon.fill(Qt::white);
    for(QAction *action:{&home,&canvases,&undo,&redo,&save,&brushes,&eraser,&blend,
                         &layers,&color,&tools})
        action->setIcon(QIcon(icon));
    int brushed=0;
    int opened=0;
    QObject::connect(&brushes,&QAction::triggered,&host,
                     [&brushed]() { ++brushed; });
    QObject::connect(&open,&QAction::triggered,&host,
                     [&opened]() { ++opened; });

    verify(MobileNativeCommandLayout::install(
        &host,{&home,&canvases,&undo,&redo,&save},
        {&brushes,&eraser,&blend,&color,&layers,&tools},
        {&newCanvas,&open,&exit,&canvasOnly},&canvasOnly),
        "could not install command chrome");
    auto *primary=host.findChild<QFrame*>(
        QStringLiteral("_mobileNativeDocumentBar"));
    auto *secondary=host.findChild<QFrame*>(
        QStringLiteral("_mobileNativeWorkspaceBar"));
    auto *more=host.findChild<QToolButton*>(
        QStringLiteral("_mobileNativeMoreButton"));
    verify(primary && secondary && more && more->menu(),
           "the two rows or overflow menu are missing");
    verify(host.centralWidget()==canvas &&
           canvas->parentWidget()==&host,
           "native command chrome replaced or reparented painting canvas");
    const auto hasButton=[](QFrame *panel,QAction *action) {
        for(auto *button:panel->findChildren<QToolButton*>())
            if(button->defaultAction()==action)return true;
        return false;
    };
    verify(hasButton(primary,&canvases) && hasButton(primary,&save) &&
           !hasButton(primary,&brushes),
           "document actions leaked into the workspace controls");
    canvases.setText(QObject::tr("Tool Options"));
    for(auto *button:primary->findChildren<QToolButton*>()) {
        if(button->defaultAction()==&canvases)
            verify(button->accessibleName()==canvases.text(),
                   "reassigned toolbar shortcut kept its previous label");
    }
    verify(hasButton(secondary,&brushes) && hasButton(secondary,&eraser) &&
           hasButton(secondary,&layers) &&
           hasButton(secondary,&color) && hasButton(secondary,&tools),
           "brush/layer/color/tool controls are not directly available");
    verify(more->menu()->actions().contains(&newCanvas) &&
           more->menu()->actions().contains(&open) &&
           more->menu()->actions().contains(&exit),
           "file and exit actions disappeared from the More menu");
    brushes.trigger();
    open.trigger();
    verify(brushed==1 && opened==1,
           "rehosting QAction changed command delivery");
    host.show();
    QApplication::processEvents();
    verify(host.centralWidget()==canvas && secondary->isVisible() &&
           primary->isVisible(),
           "visible command bars hid or destroyed the original canvas");
    auto *restore=canvas->findChild<QToolButton*>(
        QStringLiteral("_mobileNativeRestoreControls"));
    verify(restore && !restore->isVisible(),"restore button starts visible");
    verify(primary->y()==secondary->y() && !primary->geometry().intersects(secondary->geometry()),
           "control groups collide on a wide canvas");
    const QRect normalGeometry=canvas->geometry();
    canvasOnly.trigger();
    QApplication::processEvents();
    verify(canvasOnly.isChecked() && restore->isVisible() &&
           !primary->isVisible() && !secondary->isVisible() &&
           host.centralWidget()==canvas && canvas->geometry()==normalGeometry,
           "Canvas Only failed to hide controls without resizing the canvas");
    restore->click();
    QApplication::processEvents();
    verify(!canvasOnly.isChecked() && !restore->isVisible() &&
           primary->isVisible() && secondary->isVisible() &&
           canvas->geometry()==normalGeometry,
           "Show Controls did not restore the command bars");
    host.resize(320,420);
    QApplication::processEvents();
    verify(primary->window()==&host &&
           primary->y()>secondary->geometry().bottom(),
           "control groups did not stack on a narrow canvas");
    verify(MobileNativeCommandLayout::panelTopInset(canvas)>primary->geometry().bottom(),
           "workspace overlays have no clearance below the controls");
    for(QFrame *panel:{primary,secondary}) {
        verify(canvas->rect().contains(panel->geometry()),
               "a floating group overflowed at 320 px");
        for(QToolButton *button:panel->findChildren<QToolButton*>()) {
            verify(button->isVisible() && button->height()>=44,
                   "a primary touch target overflowed at 320 px");
        }
    }
    verify(!MobileNativeCommandLayout::install(
        &host,{&save},{&brushes},{&exit}),
        "duplicate chrome installation was accepted");
    qInfo()<<"PASS: Krita canvas unchanged, native controls immediately"
              " accessible, real actions preserved";
    return 0;
}
