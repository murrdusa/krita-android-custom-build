/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QApplication>
#include <QDebug>
#include <QMainWindow>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include "mobile_core/native_command_layout.h"

static void verify(bool pass,const char *message)
{
    if(!pass)qFatal("Native engine command layout: %s",message);
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QTemporaryDir config;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,config.path());
    app.setOrganizationName(QStringLiteral("KritaToolbarTest"));
    app.setApplicationName(QStringLiteral("KritaToolbarTest"));

    QMainWindow host;
    host.resize(320,420);
    auto *canvas=new QWidget(&host);
    host.setCentralWidget(canvas);

    QAction home(QStringLiteral("Home"),&host);
    QAction brush(QStringLiteral("Paint"),&host);
    QAction tool(QStringLiteral("Tool Settings"),&host);
    QAction undo(QStringLiteral("Undo"),&host);
    QAction redo(QStringLiteral("Redo"),&host);
    QAction save(QStringLiteral("Save"),&host);
    QAction shortcut(QStringLiteral("Shortcut"),&host);
    QAction open(QStringLiteral("Open"),&host);
    QAction canvasOnly(QStringLiteral("Canvas Only"),&host);
    canvasOnly.setCheckable(true);
    brush.setCheckable(true);
    QPixmap pixmap(20,20);
    pixmap.fill(Qt::white);
    const QIcon icon(pixmap);
    for(auto *action:{&home,&brush,&tool,&undo,&redo,&save,&shortcut})
        action->setIcon(icon);

    const QStringList defaults{"brush","tool","undo","redo","save","shortcut"};
    const QStringList saved{"tool","redo","brush","save","undo","shortcut"};
    QSettings().setValue(QStringLiteral("MobileStudio/primaryToolbarCommands"),saved);
    const MobileNativeCommandLayout::Commands commands{
        {"brush",&brush},{"tool",&tool},{"undo",&undo},{"redo",&redo},
        {"save",&save},{"shortcut",&shortcut}
    };
    int triggered=0;
    QObject::connect(&brush,&QAction::triggered,&host,[&]() {++triggered;});
    verify(MobileNativeCommandLayout::install(
        &host,&home,icon,commands,defaults,{&open},&canvasOnly),"toolbar installation");
    host.show();
    QApplication::processEvents();

    auto *bar=canvas->findChild<QFrame*>(QStringLiteral("_mobileNativeToolbar"));
    auto *scroll=bar?bar->findChild<QScrollArea*>():nullptr;
    auto *more=bar?bar->findChild<QToolButton*>(QStringLiteral("_mobileNativeMoreButton")):nullptr;
    verify(bar && scroll && more && more->menu(),"toolbar or Settings missing");
    verify(canvas->rect().contains(bar->geometry()),"toolbar overflowed the narrow canvas");
    verify(scroll->horizontalScrollBar()->maximum()>0,"narrow toolbar does not scroll");
    verify(more->menu()->actions().contains(&open),"file command disappeared");
    auto *strip=scroll->widget()->layout();
    verify(qobject_cast<QToolButton*>(strip->itemAt(0)->widget())->defaultAction()==&tool,
           "toolbar did not load the saved command order");

    auto *button=qobject_cast<QToolButton*>(strip->itemAt(2)->widget());
    verify(button && button->defaultAction()==&brush && button->height()>=44,
           "brush does not have its original action or a finger-sized button");
    brush.setChecked(true);
    brush.setEnabled(false);
    brush.setText(QStringLiteral("Brush"));
    verify(button->isChecked() && !button->isEnabled() &&
           button->accessibleName()==QStringLiteral("Brush"),"button state diverged from its action");
    brush.setEnabled(true);
    button->click();
    verify(triggered==1,"toolbar button did not trigger the existing action");

    const QRect bounds=canvas->geometry();
    canvasOnly.trigger();
    auto *restore=canvas->findChild<QToolButton*>(QStringLiteral("_mobileNativeRestoreControls"));
    verify(restore && restore->isVisible() && !bar->isVisible(),"Canvas Only did not hide the toolbar");
    restore->click();
    verify(bar->isVisible() && !restore->isVisible() && canvas->geometry()==bounds,
           "restoring the toolbar changed canvas geometry");
    verify(!MobileNativeCommandLayout::install(&host,&home,icon,commands,defaults,{}),
           "duplicate toolbar installation accepted");
    return 0;
}
