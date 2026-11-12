/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Android entrypoint for Krita Mobile Studio. Krita's Qt application
 * object still handles input/event dispatch, while engine setup and
 * the drawing window no longer enter KisApplication::start.
 */
#include "krita_android_runtime.h"
#ifdef Q_OS_ANDROID
#include <QAbstractSpinBox>
#include <QApplication>
#include <QInputMethod>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QPalette>
#include <QSettings>
#include <jni.h>
#include "KisApplication.h"
#include "KisApplicationArguments.h"
#include "KisAndroidDonations.h"
#include "KisPart.h"
#include "kis_config_notifier.h"
#include "kis_config.h"
#include "kis_document_aware_spin_box_unit_manager.h"
#include "kis_spin_box_unit_manager.h"
#include "krita_application_entry.h"
#include "android_document_intents.h"
#include "krita_native_workspace.h"
#include <config-qt-patches-present.h>

namespace {
class StudioTextInput final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *object,QEvent *event) override
    {
        auto *edit=qobject_cast<QLineEdit*>(object);
        if(edit && edit->isEnabled() && !edit->isReadOnly()) {
            bool tapped=false;
            if(event->type()==QEvent::MouseButtonRelease) {
                auto *mouse=static_cast<QMouseEvent*>(event);
                tapped=mouse->button()==Qt::LeftButton && edit->rect().contains(mouse->pos());
            } else if(event->type()==QEvent::TabletRelease) {
                tapped=edit->rect().contains(static_cast<QTabletEvent*>(event)->pos());
            }
            if(tapped) {
                edit->setFocus(Qt::MouseFocusReason);
                QTimer::singleShot(0,edit,[edit]() {
                    if(edit->isVisible() && edit->hasFocus())
                        QGuiApplication::inputMethod()->show();
                });
            }
        }
        if(event->type()!=QEvent::FocusIn)return false;
        auto *spin=qobject_cast<QAbstractSpinBox*>(object);
        if(!spin)spin=qobject_cast<QAbstractSpinBox*>(object->parent());
        if(spin) {
            QTimer::singleShot(0,spin,[spin]() {
                auto *focus=QApplication::focusWidget();
                if(focus && (focus==spin || spin->isAncestorOf(focus)))
                    spin->selectAll();
            });
        }
        return false;
    }
};

void applyStudioWidgetColors(QApplication &app)
{
    // Use the same foreground/background pairs for plain Qt dialogs,
    // popup lists and menus, regardless of the Android system theme.
    app.setStyle(QStringLiteral("Fusion"));
    QPalette palette=app.palette();
    palette.setColor(QPalette::Window,QColor("#252529"));
    palette.setColor(QPalette::Base,QColor("#2c2c32"));
    palette.setColor(QPalette::AlternateBase,QColor("#34343c"));
    palette.setColor(QPalette::Button,QColor("#383840"));
    palette.setColor(QPalette::ToolTipBase,QColor("#383840"));
    const QColor text("#f4f4f5");
    for(auto role:{QPalette::WindowText,QPalette::Text,QPalette::ButtonText,
                   QPalette::ToolTipText,QPalette::HighlightedText,QPalette::BrightText})
        palette.setColor(role,text);
    palette.setColor(QPalette::Highlight,QColor("#365c78"));
    palette.setColor(QPalette::Link,QColor("#a7def1"));
    palette.setColor(QPalette::LinkVisited,QColor("#d4bafb"));
    palette.setColor(QPalette::PlaceholderText,QColor("#b4b7c1"));
    for(auto role:{QPalette::WindowText,QPalette::Text,QPalette::ButtonText,
                   QPalette::PlaceholderText})
        palette.setColor(QPalette::Disabled,role,QColor("#a6a8b2"));
    app.setPalette(palette);

    // These popup surfaces can be separate windows and need explicit pairs
    // even when the parent widget already has its own stylesheet.
    app.setStyleSheet(QStringLiteral(
        "QMenu{background:#252529;color:#f4f4f5;border:1px solid #55555c;}"
        "QMenu::item:selected{background:#365c78;color:#ffffff;}"
        "QMenu::item:disabled{color:#a6a8b2;}"
        "QComboBox QAbstractItemView{background:#2c2c32;color:#f4f4f5;"
        "selection-background-color:#365c78;selection-color:#ffffff;}"));
}
}

MobileKritaAndroidRuntime::Result MobileKritaAndroidRuntime::launch(
    KisApplication &app,const KisApplicationArguments &args)
{
    // This APK has one application: the native Mobile Studio painting
    // host. External files are queued below; there is no desktop UI
    // to fall back into on any Android launch.
    if(!args.fileLayer().isEmpty() || !args.session().isEmpty() ||
       !args.workspace().isEmpty() || !args.windowLayout().isEmpty() ||
       args.exportAs() || args.exportSequence() ||
       !args.exportFileName().isEmpty() || args.canvasOnly() ||
       args.fullScreen() || args.doNewImage() || args.doTemplate()) {
        qWarning() << "Mobile Studio: desktop-only launch flags ignored";
    }

    qInfo() << "Mobile Studio: standalone Android process startup";
    MobileAndroidDocumentIntents::activate();
    for(const QString &file:args.filenames())
        MobileAndroidDocumentIntents::enqueue(file);

    KisConfig cfg(false);
    const QString opengl=cfg.canvasState();
    if(opengl==QStringLiteral("OPENGL_NOT_TRIED"))
        cfg.setCanvasState("TRY_OPENGL");
    else if(opengl!=QStringLiteral("OPENGL_SUCCESS") &&
            opengl!=QStringLiteral("TRY_OPENGL"))
        cfg.setCanvasState("OPENGL_FAILED");

    app.initializeGlobals(args);
    app.initializeAndroidScaling(cfg);
    applyStudioWidgetColors(app);
    app.installEventFilter(new StudioTextInput(&app));
    app.addResourceTypes();
    app.loadPlugins();
    if(!app.registerResources()) {
        qCritical() << "Mobile Studio: resource registration failed";
        return Result::Failed;
    }
    // The document factory is an engine service, not a desktop window.
    KisPart::instance();

    KisSpinBoxUnitManagerFactory::setDefaultUnitManagerBuilder(
        new KisDocumentAwareSpinBoxUnitManagerBuilder());
    QObject::connect(&app,&KisApplication::aboutToQuit,
                     &KisSpinBoxUnitManagerFactory::clearUnitManagerBuilder);

    // Keep Krita's long-press handling and Android stylus platform flags
    // intact while replacing desktop startup.
    auto *notifier=KisConfigNotifier::instance();
    QObject::connect(notifier,SIGNAL(sigLongPressChanged(bool)),
                     &app,SLOT(slotSetLongPress(bool)));
    QMetaObject::invokeMethod(&app,"slotSetLongPress",
                              Q_ARG(bool,cfg.longPressEnabled()));
#if KRITA_QT_HAS_ANDROID_EMULATE_MOUSE_BUTTONS_FOR_PAGE_UP_DOWN
    auto setPageKeys=[](bool enabled) {
        QCoreApplication::setKritaAttribute(
            KRITA_QATTRIBUTE_ANDROID_EMULATE_MOUSE_BUTTONS_FOR_PAGE_UP_DOWN,
            enabled);
    };
    QObject::connect(
        notifier,
        &KisConfigNotifier::sigUsePageUpDownMouseButtonEmulationWorkaroundChanged,
        &app,setPageKeys);
    setPageKeys(cfg.usePageUpDownMouseButtonEmulationWorkaround());
#endif
#if KRITA_QT_HAS_ANDROID_EMULATE_MOUSE_BUTTONS_FOR_HIGH_FUNCTION_KEYS
    QCoreApplication::setKritaAttribute(
        KRITA_QATTRIBUTE_ANDROID_EMULATE_MOUSE_BUTTONS_FOR_HIGH_FUNCTION_KEYS,
        true);
#endif
#if KRITA_QT_HAS_ANDROID_IGNORE_HISTORIC_TABLET_EVENTS
    auto setHistoric=[](bool enabled) {
        QCoreApplication::setKritaAttribute(
            KRITA_QATTRIBUTE_ANDROID_IGNORE_HISTORIC_TABLET_EVENTS,enabled);
    };
    QObject::connect(
        notifier,
        &KisConfigNotifier::sigUseIgnoreHistoricTabletEventsWorkaroundChanged,
        &app,setHistoric);
    setHistoric(cfg.useIgnoreHistoricTabletEventsWorkaround());
#endif

    // Hide the Krita splash and launch the only native drawing window.
    // An initialization failure is an error, not an automatic UI switch.
    app.hideSplashScreen();
    if(!MobileKritaApplicationEntry::start())
        return Result::Failed;

    app.verifyMetatypeRegistration();
    qInfo() << "Mobile Studio: independent Android startup ready";
    return Result::Started;
}

void MobileKritaAndroidRuntime::resetCanvasView()
{
    auto *app=QCoreApplication::instance();
    if(!app)return;
    QMetaObject::invokeMethod(app,[]() {
        for(QWidget *window:QApplication::topLevelWidgets()) {
            if(!window)continue;
            auto *object=window->findChild<QObject*>(
                QStringLiteral("_mobileNativeEngineWorkspaces"));
            if(auto *workspace=dynamic_cast<MobileKritaNativeWorkspace*>(object)) {
                workspace->resetCanvasView();
                return;
            }
        }
    },Qt::QueuedConnection);
}

namespace {
void dispatchMobileVolume(int code)
{
    if(code<=0 || code>6)return;
    auto *app=QCoreApplication::instance();
    if(!app)return;
    // Android's Java callback is on the Activity UI thread. The real
    // Krita document, view and QAction must be touched on Qt's thread.
    QMetaObject::invokeMethod(app,[code]() {
        for(QWidget *window:QApplication::topLevelWidgets()) {
            if(!window)continue;
            auto *object=window->findChild<QObject*>(
                QStringLiteral("_mobileNativeEngineWorkspaces"));
            if(auto *workspace=dynamic_cast<MobileKritaNativeWorkspace*>(object)) {
                workspace->dispatchVolumeAction(code);
                return;
            }
        }
    },Qt::QueuedConnection);
}
}

// The original Android Java MainActivity already recognizes volume short
// and long presses. Its former JNI callback targeted KisMainWindow, which
// Mobile Studio intentionally does not instantiate. Provide callbacks in
// the actual independent app library and reuse existing standalone actions.
extern "C" JNIEXPORT jboolean JNICALL
Java_org_krita_android_JNIWrappers_volumeMappingEnabled(JNIEnv*,jclass)
{
    return QSettings().value(QStringLiteral("TabletUi/volumeKeys"),true)
        .toBool()?JNI_TRUE:JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_krita_android_JNIWrappers_volumeUndoRedo(JNIEnv*,jclass,jboolean redo)
{
    dispatchMobileVolume(redo?2:1);
}

extern "C" JNIEXPORT jint JNICALL
Java_org_krita_android_JNIWrappers_volumeKeyAction(
    JNIEnv*,jclass,jboolean up,jboolean longPress)
{
    const QSettings settings;
    const QString key=up
        ? (longPress?QStringLiteral("TabletUi/volumeUpLong"):
                     QStringLiteral("TabletUi/volumeUpShort"))
        : (longPress?QStringLiteral("TabletUi/volumeDownLong"):
                     QStringLiteral("TabletUi/volumeDownShort"));
    const int fallback=up?2:1;
    return qBound(0,settings.value(key,fallback).toInt(),6);
}

extern "C" JNIEXPORT jint JNICALL
Java_org_krita_android_JNIWrappers_volumeLongPressMs(JNIEnv*,jclass)
{
    return qBound(80,QSettings().value(
        QStringLiteral("TabletUi/volumeLongMs"),200).toInt(),1000);
}

extern "C" JNIEXPORT void JNICALL
Java_org_krita_android_JNIWrappers_volumeExecute(JNIEnv*,jclass,jint code)
{
    dispatchMobileVolume(code);
}
#endif
