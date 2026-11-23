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
    // Layout edits after slightly resizing an existing Fold/split-screen
    // workspace should track the new viewport without duplicating an ID
    // or erasing that profile's command and panel settings.
    if(sizeForSave(QSize(375,790),QSize(360,800),true)!=QSize(375,790) ||
       sizeForSave(QSize(800,360),QSize(360,800),true)!=QSize(360,800) ||
       sizeForSave(QSize(750,780),QSize(360,800),true)!=QSize(360,800))
        return 14;
    remember(settings,QStringLiteral("folded"),QSize(375,790));
    if(saved(settings).size()!=1 ||
       settings.value(group(QStringLiteral("folded"))+
           QStringLiteral("size")).toSize()!=QSize(375,790) ||
       !settings.value(group(QStringLiteral("folded"))+
           QStringLiteral("layersCollapsed")).toBool() ||
       closest(settings,QSize(375,790))!=QStringLiteral("folded"))return 12;
    remember(settings,QStringLiteral("folded"),QSize());
    if(settings.value(group(QStringLiteral("folded"))+
        QStringLiteral("size")).toSize()!=QSize(375,790))return 13;

    const auto folded=QStringLiteral("folded"),wide=QStringLiteral("wide");
    const auto historyKey=QStringLiteral("MobileStudio/colorWheelHistory");
    const auto artworkKey=QStringLiteral("MobileStudio/strokeColors");
    const auto touchKey=QStringLiteral("MobileStudio/touchGestures/99");
    settings.setValue(historyKey,true);
    settings.setValue(artworkKey,QStringList{QStringLiteral("#ff445566")});
    const auto preset=capturePreset(settings,folded);
    settings.setValue(historyKey,false);
    settings.setValue(touchKey,QStringLiteral("changed"));
    applyPreset(settings,wide,QSize(850,610),preset);
    if(!settings.value(historyKey).toBool() || settings.contains(touchKey) ||
       settings.value(artworkKey).toStringList().size()!=1 ||
       !settings.value(group(wide)+QStringLiteral("layersCollapsed")).toBool() ||
       settings.value(group(wide)+QStringLiteral("size")).toSize()!=QSize(850,610) ||
       settings.value(group(folded)+QStringLiteral("size")).toSize()!=QSize(375,790))return 15;

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,files.path());
    QCoreApplication::setOrganizationName(QStringLiteral("KritaWorkspaceTest"));
    QCoreApplication::setApplicationName(QStringLiteral("Studio"));
    // A centered camera hole must not push a corner toolbar down by an
    // entire safe-inset band. In landscape a bar that actually overlaps
    // the camera must still move clear of the physical cutout.
    const QRect phoneBounds(2,2,356,776);
    const QRect cornerBar(222,2,136,48);
    const QList<QRect> portraitCutout{QRect(170,0,20,30)};
    if(MobileOverlayPlacement::nearest(cornerBar,phoneBounds,portraitCutout,cornerBar.size())!=cornerBar)return 28;
    const QRect landscapeBounds(2,2,776,356);
    const QRect sideBar(2,2,48,320);
    const QList<QRect> landscapeCutout{QRect(0,160,30,20)};
    const auto shifted=MobileOverlayPlacement::nearest(sideBar,landscapeBounds,landscapeCutout,sideBar.size());
    if(!shifted.isValid() || shifted.intersects(landscapeCutout.first()))return 29;
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
    const auto factoryLayout=twoToolbarPreset().value(QStringLiteral("layout")).toMap();
    const auto factoryBars=factoryLayout.value(QStringLiteral("toolbars")).toList();
    if(factoryBars.size()!=2)return 16;
    QStringList ids=factoryBars.first().toMap().value(QStringLiteral("commands")).toStringList();
    ids.append(factoryBars.last().toMap().value(QStringLiteral("commands")).toStringList());
    if(ids.contains(QStringLiteral("undo")) || ids.contains(QStringLiteral("redo")) ||
       !ids.contains(QStringLiteral("liquify")) || !ids.contains(QStringLiteral("reference-images")))return 17;
    for(const auto &key:ids) {
        commands.insert(key,new QAction(icon,key,&window));
        defaults.append(key);
    }
    auto *layout=MobileNativeCommandLayout::install(&window,&home,icon,commands,defaults,{});
    if(!layout)return 6;
    window.show();
    QApplication::processEvents();
    auto *bar=canvas->findChild<QFrame*>(QStringLiteral("_mobileNativeToolbar"));
    auto *scroll=bar?bar->findChild<QScrollArea*>():nullptr;
    if(!bar || !scroll || bar->geometry().top()!=2 || bar->geometry().right()!=canvas->rect().right()-2 ||
       scroll->horizontalScrollBar()->maximum()!=0)return 7;
    const auto extras=canvas->findChildren<QFrame*>(QString(),Qt::FindDirectChildrenOnly);
    QFrame *side=nullptr;
    for(auto *frame:extras)
        if(frame!=bar && frame->property("mobileCommandToolbar").toBool())side=frame;
    if(!side || !side->isVisible() || side->width()>=side->height() || side->x()!=2)return 18;
    auto *layersButton=layout->buttonForCommand(QStringLiteral("layers"));
    if(!layersButton || layersButton->width()<30 || !bar->isAncestorOf(layersButton))return 23;
    if(!layout->buttonForCommand(QStringLiteral("liquify")) ||
       !layout->buttonForCommand(QStringLiteral("tool-settings")))return 19;
    const auto *topRow=bar->layout();
    if(topRow->itemAt(1)->widget()->objectName()!=QStringLiteral("_mobileNativeMoreButton"))return 20;
    auto *homeEffect=qobject_cast<QGraphicsOpacityEffect*>(topRow->itemAt(0)->widget()->graphicsEffect());
    if(!homeEffect || homeEffect->opacity()!=1.0 || !topRow->itemAt(0)->widget()->isEnabled())return 21;
    auto *reference=layout->buttonForCommand(QStringLiteral("reference-images"));
    if(!reference || !bar->isAncestorOf(reference))return 24;
    for(const auto &value:QSettings().value(group(MobileWorkspaceProfiles::closest(QSettings(),canvas->size()))+
            QStringLiteral("toolbars")).toList())
        if(value.toMap().value(QStringLiteral("colorHistory"),true).toBool())return 22;
    QString active;
    bool portrait=false;
    layout->bindWorkspace([&](const QString &id,bool isPortrait) {active=id;portrait=isPortrait;});
    const QString portraitId=active;
    if(portraitId.isEmpty() || !portrait)return 8;
    QSettings local;
    remember(local,portraitId,canvas->size());
    // An existing installation has an invisible Home and Reference on the side.
    auto legacy=local.value(group(portraitId)+QStringLiteral("toolbars")).toList();
    auto legacyTop=legacy.first().toMap(),legacySide=legacy.last().toMap();
    legacyTop.insert(QStringLiteral("homeOpacity"),0);
    legacyTop.insert(QStringLiteral("commands"),QStringList{QStringLiteral("layers")});
    auto sideIds=legacySide.value(QStringLiteral("commands")).toStringList();
    sideIds.append(QStringLiteral("reference-images"));
    legacySide.insert(QStringLiteral("commands"),sideIds);
    legacy[0]=legacyTop;
    legacy[1]=legacySide;
    local.setValue(group(portraitId)+QStringLiteral("toolbars"),legacy);
    local.remove(group(portraitId)+QStringLiteral("topBarNavigationV1"));
    int positioned=0;
    layout->onToolbarPositioned=[&]() {++positioned;};
    layout->reloadSettings();
    QApplication::processEvents();
    reference=layout->buttonForCommand(QStringLiteral("reference-images"));
    homeEffect=qobject_cast<QGraphicsOpacityEffect*>(topRow->itemAt(0)->widget()->graphicsEffect());
    if(!positioned || !reference || !bar->isAncestorOf(reference) ||
       !homeEffect || homeEffect->opacity()!=1.0)return 25;
    // Existing sidebars receive Selection once without resetting navigation,
    // other commands, or geometry. Reloading must not duplicate the button.
    auto selectionLegacy=local.value(group(portraitId)+QStringLiteral("toolbars")).toList();
    const auto preservedTop=selectionLegacy.first().toMap();
    auto oldSide=selectionLegacy.last().toMap();
    auto oldIds=oldSide.value(QStringLiteral("commands")).toStringList();
    oldIds.removeAll(QStringLiteral("selection"));
    oldSide.insert(QStringLiteral("commands"),oldIds);
    selectionLegacy[selectionLegacy.size()-1]=oldSide;
    local.setValue(group(portraitId)+QStringLiteral("toolbars"),selectionLegacy);
    local.remove(group(portraitId)+QStringLiteral("selectionToolbarV1"));
    layout->reloadSettings();
    layout->reloadSettings();
    QApplication::processEvents();
    const auto selectionSaved=local.value(group(portraitId)+QStringLiteral("toolbars")).toList();
    auto *selection=layout->buttonForCommand(QStringLiteral("selection"));
    if(!selection || bar->isAncestorOf(selection) || selectionSaved.first().toMap()!=preservedTop ||
       selectionSaved.last().toMap().value(QStringLiteral("commands")).toStringList().count(QStringLiteral("selection"))!=1 ||
       selectionSaved.last().toMap().value(QStringLiteral("edge"))!=oldSide.value(QStringLiteral("edge")))return 30;
    // Toolbar removal must notify the workspace so the standalone Layers
    // launcher can return immediately, and adding Layers must hide it again.
    auto migrated=local.value(group(portraitId)+QStringLiteral("toolbars")).toList();
    auto withoutLayers=migrated.first().toMap();
    withoutLayers.insert(QStringLiteral("commands"),QStringList{QStringLiteral("reference-images")});
    migrated[0]=withoutLayers;
    local.setValue(group(portraitId)+QStringLiteral("toolbars"),migrated);
    positioned=0;
    layout->reloadSettings();
    QApplication::processEvents();
    if(!positioned || layout->buttonForCommand(QStringLiteral("layers")))return 26;
    QStringList custom=defaults;
    custom.move(0,3);
    auto customized=local.value(group(portraitId)+QStringLiteral("toolbars")).toList();
    auto main=customized.first().toMap();
    main.insert(QStringLiteral("commands"),custom);
    customized[0]=main;
    local.setValue(group(portraitId)+QStringLiteral("toolbars"),customized);
    const auto resize=[&](int width,int height) {
        window.resize(width,height);
        QEventLoop wait;
        QTimer::singleShot(260,&wait,&QEventLoop::quit);
        wait.exec();
    };
    resize(800,360);
    if(portrait || active==portraitId || bar->geometry().top()!=2 ||
       bar->height()>=bar->width())return 9;
    resize(750,780);
    if(portrait || active==portraitId || bar->height()>=bar->width())return 10;
    resize(340,720);
    const auto *first=qobject_cast<QToolButton*>(scroll->widget()->layout()->itemAt(0)->widget());
    if(!portrait || active!=portraitId || !first || first->defaultAction()!=commands.value(custom.first()))return 11;
}
