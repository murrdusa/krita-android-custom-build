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
    auto *settingsMenu=more?more->findChild<QMenu*>(QStringLiteral("_mobileNativeSettingsMenu")):nullptr;
    verify(bar && scroll && more && settingsMenu,"toolbar or Settings missing");
    verify(!more->menu() && !more->defaultAction()->menu(),"Settings has an implicit wrapper menu");
    more->click();
    QApplication::processEvents();
    verify(settingsMenu->isVisible(),"Settings button did not open its menu directly");
    settingsMenu->hide();
    verify(canvas->rect().contains(bar->geometry()),"toolbar overflowed the narrow canvas");
    verify(scroll->horizontalScrollBar()->maximum()>0,"narrow toolbar does not scroll");
    verify(settingsMenu->actions().contains(&open),"file command disappeared");
    auto *strip=scroll->widget()->layout();
    verify(qobject_cast<QToolButton*>(strip->itemAt(0)->widget())->defaultAction()==&tool,
           "toolbar did not load the saved command order");

    auto *shortcutButton=qobject_cast<QToolButton*>(strip->itemAt(5)->widget());
    shortcut.setData(QVariant::fromValue<QObject*>(&tool));
    verify(shortcutButton && shortcutButton->isHidden(),"toolbar duplicated the shortcut target");
    shortcut.setData(QVariant::fromValue<QObject*>(&open));
    verify(!shortcutButton->isHidden(),"distinct shortcut disappeared");

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
    const QVariantMap mainBar{{QStringLiteral("name"),QStringLiteral("Main")},
                              {QStringLiteral("commands"),saved}};
    const QVariantMap sideBar{{QStringLiteral("name"),QStringLiteral("Side")},
                              {QStringLiteral("commands"),QStringList{"brush","undo"}},
                              {QStringLiteral("edge"),3},
                              {QStringLiteral("target"),56}};
    QSettings().setValue(QStringLiteral("MobileStudio/toolbars"),QVariantList{mainBar,sideBar});
    QMainWindow second;
    second.resize(420,600);
    auto *secondCanvas=new QWidget(&second);
    second.setCentralWidget(secondCanvas);
    verify(MobileNativeCommandLayout::install(
        &second,&home,icon,commands,defaults,{},&canvasOnly),"could not reload multiple toolbars");
    second.show();
    QApplication::processEvents();
    auto *side=secondCanvas->findChild<QFrame*>(QStringLiteral("_mobileCustomToolbar"));
    verify(side && side->isVisible() && side->height()>side->width() &&
           side->geometry().right()==secondCanvas->rect().right()-2,
           "saved right rail did not restore vertically at the edge");
    verify(MobileNativeCommandLayout::overlayBounds(secondCanvas).right()<side->geometry().left(),
           "right rail did not reserve space for floating panels");
    canvasOnly.trigger();
    verify(!side->isVisible(),"Canvas Only left an extra toolbar visible");
    canvasOnly.trigger();
    second.resize(160,280);
    QApplication::processEvents();
    verify(!side->isVisible(),"oversized rail crowded a small canvas");
    auto *secondMenu=secondCanvas->findChild<QMenu*>(QStringLiteral("_mobileNativeSettingsMenu"));
    verify(secondMenu,"second toolbar lost its Settings menu");
    bool fallback=false;
    for(auto *action:secondMenu->actions())
        if(auto *menu=action->menu())
            for(auto *entry:menu->actions())
                if(entry->menu() && entry->menu()->actions().contains(&brush))fallback=true;
    verify(fallback,"the gear menu lost commands from an overflow toolbar");
    return 0;
}
