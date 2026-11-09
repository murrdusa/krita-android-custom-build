/*
 * Qt5-only Android Back policy test. No Android emulator or Krita engine.
 * Confirms that a Back key routed to a focused child of Krita's main
 * window can dismiss mobile UI, while modal dialogs, unrelated windows,
 * auto repeats and Krita's Escape key are left alone.
 */
#include <QApplication>
#include <QDebug>
#include <QMainWindow>
#include <QPushButton>
#include <QWindow>
#include "mobile_core/android_back_policy.h"

static void require(bool ok,const char *reason) {
    if(!ok)qFatal("Mobile Back policy test failed: %s",reason);
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QMainWindow krita;
    krita.setObjectName(QStringLiteral("KritaNativeMainWindow"));
    auto *central=new QWidget;
    krita.setCentralWidget(central);
    auto *layerButton=new QPushButton(QStringLiteral("Layers"),central);
    krita.show();

    QMainWindow unrelated;
    unrelated.show();
    QApplication::processEvents();

    const auto allowed=[&](QObject *target,QEvent::Type event,int key,
                           bool repeat,bool active,bool changing,
                           bool modal,bool popup) {
        return MobileAndroidBackPolicy::mayDismiss(
            &krita,target,event,key,repeat,active,changing,modal,popup);
    };

    require(allowed(layerButton,QEvent::KeyPress,Qt::Key_Back,
                    false,true,false,false,false),
            "Back key on a nested mobile layer button was not handled");
    require(allowed(&krita,QEvent::KeyPress,Qt::Key_Back,
                    false,true,false,false,false),
            "Back key on Krita's main window was not handled");
    require(krita.windowHandle() &&
            allowed(krita.windowHandle(),QEvent::KeyPress,Qt::Key_Back,
                    false,true,false,false,false),
            "Qt's native main window Back event was not recognized");
    require(!allowed(layerButton,QEvent::KeyPress,Qt::Key_Escape,
                     false,true,false,false,false),
            "Escape was stolen from Krita's painting tools");
    require(!allowed(layerButton,QEvent::KeyRelease,Qt::Key_Back,
                     false,true,false,false,false),
            "Key release should not initiate another mode transition");
    require(!allowed(layerButton,QEvent::KeyPress,Qt::Key_Back,
                     true,true,false,false,false),
            "auto-repeated Back can trigger multiple transitions");
    require(!allowed(layerButton,QEvent::KeyPress,Qt::Key_Back,
                     false,true,false,true,false),
            "Back stole Krita Save/Discard/Cancel modal dialog");
    require(!allowed(layerButton,QEvent::KeyPress,Qt::Key_Back,
                     false,true,false,false,true),
            "Back stole a mobile/Qt popup menu");
    require(!allowed(layerButton,QEvent::KeyPress,Qt::Key_Back,
                     false,false,false,false,false),
            "Back activated outside the mobile presentation");
    require(!allowed(layerButton,QEvent::KeyPress,Qt::Key_Back,
                     false,true,true,false,false),
            "Back ran during a canvas ownership transition");
    require(!allowed(&unrelated,QEvent::KeyPress,Qt::Key_Back,
                     false,true,false,false,false),
            "Back was incorrectly applied to an unrelated app window");
    qInfo() << "PASS: Android Back targets only active Krita mobile"
            << "main-window keys, respects modal/popup and native"
            << "Escape, rejects key release/auto-repeat/transitions";
    return 0;
}
