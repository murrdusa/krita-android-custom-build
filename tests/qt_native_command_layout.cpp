/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Regression: native commands visible in two short rows, real QActions
 * preserved, and existing Krita canvas not touched.
 */
#include <QApplication>
#include <QDebug>
#include <QMainWindow>
#include <QMenu>
#include <QToolBar>
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
    host.resize(450,870);
    auto *canvas=new QWidget(&host);
    canvas->setObjectName(QStringLiteral("unchangedKritaCanvas"));
    host.setCentralWidget(canvas);

    QAction canvases(QObject::tr("Canvases"),&host);
    QAction undo(QObject::tr("Undo"),&host);
    QAction redo(QObject::tr("Redo"),&host);
    QAction save(QObject::tr("Save"),&host);
    QAction brushes(QObject::tr("Brushes"),&host);
    QAction layers(QObject::tr("Layers"),&host);
    QAction color(QObject::tr("Color"),&host);
    QAction tools(QObject::tr("Tools"),&host);
    QAction newCanvas(QObject::tr("New"),&host);
    QAction open(QObject::tr("Open"),&host);
    QAction exit(QObject::tr("Exit test"),&host);
    QAction canvasOnly(QObject::tr("Canvas Only"),&host);
    canvasOnly.setCheckable(true);
    int brushed=0;
    int opened=0;
    QObject::connect(&brushes,&QAction::triggered,&host,
                     [&brushed]() { ++brushed; });
    QObject::connect(&open,&QAction::triggered,&host,
                     [&opened]() { ++opened; });

    verify(MobileNativeCommandLayout::install(
        &host,{&canvases,&undo,&redo,&save},
        {&brushes,&layers,&color,&tools},
        {&newCanvas,&open,&exit,&canvasOnly},&canvasOnly),
        "could not install command chrome");
    auto *primary=host.findChild<QToolBar*>(
        QStringLiteral("_mobileNativeDocumentBar"));
    auto *secondary=host.findChild<QToolBar*>(
        QStringLiteral("_mobileNativeWorkspaceBar"));
    auto *more=host.findChild<QToolButton*>(
        QStringLiteral("_mobileNativeMoreButton"));
    verify(primary && secondary && more && more->menu(),
           "the two rows or overflow menu are missing");
    verify(host.centralWidget()==canvas &&
           canvas->parentWidget()==&host,
           "native command chrome replaced or reparented painting canvas");
    verify(primary->actions().contains(&canvases) &&
           primary->actions().contains(&save) &&
           !primary->actions().contains(&brushes),
           "document actions leaked into the workspace row");
    verify(secondary->actions().size()==4 &&
           secondary->actions().contains(&brushes) &&
           secondary->actions().contains(&layers) &&
           secondary->actions().contains(&color) &&
           secondary->actions().contains(&tools),
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
    const int normalHeight=canvas->height();
    canvasOnly.trigger();
    QApplication::processEvents();
    verify(canvasOnly.isChecked() && restore->isVisible() &&
           !primary->isVisible() && !secondary->isVisible() &&
           host.centralWidget()==canvas && canvas->height()>normalHeight,
           "Canvas Only did not make room for the existing canvas");
    restore->click();
    QApplication::processEvents();
    verify(!canvasOnly.isChecked() && !restore->isVisible() &&
           primary->isVisible() && secondary->isVisible() &&
           canvas->height()==normalHeight,
           "Show Controls did not restore the command bars");
    verify(!MobileNativeCommandLayout::install(
        &host,{&save},{&brushes},{&exit}),
        "duplicate chrome installation was accepted");
    qInfo()<<"PASS: Krita canvas unchanged, native controls immediately"
              " accessible, real actions preserved";
    return 0;
}
