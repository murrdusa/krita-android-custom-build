// SPDX-License-Identifier: GPL-2.0-or-later
#include "mobile_core/workspace_profiles.h"
#include "mobile_core/native_command_layout.h"
#include <QApplication>
#include <QEventLoop>
#include <QTemporaryDir>

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QTemporaryDir files;
    if(!files.isValid())return 1;
    QSettings settings(files.filePath(QStringLiteral("profiles.ini")),QSettings::IniFormat);
    using namespace MobileWorkspaceProfiles;
    if(!portraitDefault(QSize(360,800),false) ||
       portraitDefault(QSize(800,840),false) || portraitDefault(QSize(800,360),true))return 2;
    if(!matches(QSize(360,800),QSize(370,760)) ||
       matches(QSize(360,800),QSize(800,800)) ||
       !matches(QSize(799,801),QSize(801,799)))return 3;
    remember(settings,QStringLiteral("folded"),QSize(360,800));
    remember(settings,QStringLiteral("wide"),QSize(850,610));
    settings.setValue(group(QStringLiteral("folded"))+QStringLiteral("layersCollapsed"),true);
    replace(settings,QStringLiteral("wide"),QSize(850,610),QStringLiteral("folded"));
    if(!settings.value(group(QStringLiteral("wide"))+QStringLiteral("layersCollapsed")).toBool() ||
       closest(settings,QSize(350,780))!=QStringLiteral("folded"))return 4;
    replace(settings,QStringLiteral("wide"),QSize(850,610));
    if(saved(settings).size()!=1 || !closest(settings,QSize(850,610)).isEmpty())return 5;

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,files.path());
    QCoreApplication::setOrganizationName(QStringLiteral("KritaWorkspaceTest"));
    QCoreApplication::setApplicationName(QStringLiteral("Studio"));
    QMainWindow window;
    auto *canvas=new QWidget(&window);
    window.setCentralWidget(canvas);
    window.resize(320,700);
    QPixmap pixel(16,16);
    pixel.fill(Qt::white);
    const QIcon icon(pixel);
    QAction home(icon,QStringLiteral("Home"),&window);
    MobileNativeCommandLayout::Commands commands;
    QStringList defaults;
    for(int i=0;i<8;++i) {
        const auto key=QString::number(i);
        commands.insert(key,new QAction(icon,key,&window));
        defaults.append(key);
    }
    auto *layout=MobileNativeCommandLayout::install(&window,&home,icon,commands,defaults,{});
    if(!layout)return 6;
    window.show();
    QApplication::processEvents();
    auto *bar=canvas->findChild<QFrame*>(QStringLiteral("_mobileNativeToolbar"));
    auto *scroll=bar?bar->findChild<QScrollArea*>():nullptr;
    if(!bar || !scroll || bar->geometry().bottom()!=canvas->rect().bottom()-2 ||
       scroll->horizontalScrollBar()->maximum()!=0)return 7;
    window.resize(800,360);
    QEventLoop wait;
    QTimer::singleShot(240,&wait,&QEventLoop::quit);
    wait.exec();
    if(bar->geometry().left()!=2 || bar->height()<=bar->width() ||
       scroll->verticalScrollBar()->maximum()!=0)return 8;
}
