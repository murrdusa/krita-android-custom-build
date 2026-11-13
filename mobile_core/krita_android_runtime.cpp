/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Android entrypoint for Krita Mobile Studio. Krita's Qt application
 * object still handles input/event dispatch, while engine setup and
 * the drawing window no longer enter KisApplication::start.
 */
#include "krita_android_runtime.h"
#ifdef Q_OS_ANDROID
#include <QApplication>
#include <QInputMethod>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTouchEvent>
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
        QPoint global;
        bool start=false,end=false;
        switch(event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease: {
            const auto *mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()!=Qt::LeftButton)return false;
            if(m_touch && mouse->source()!=Qt::MouseEventNotSynthesized)return false;
            start=event->type()==QEvent::MouseButtonPress;
            end=!start;
            global=mouse->globalPos();
            break;
        }
        case QEvent::TabletPress:
        case QEvent::TabletRelease:
            start=event->type()==QEvent::TabletPress;
            end=!start;
            global=static_cast<QTabletEvent*>(event)->globalPos();
            break;
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd: {
            const auto &points=static_cast<QTouchEvent*>(event)->touchPoints();
            if(points.size()!=1) {m_target.clear();m_touch=false;return false;}
            global=points.first().screenPos().toPoint();
            start=event->type()==QEvent::TouchBegin;
            end=event->type()==QEvent::TouchEnd;
            if(start)m_touch=true;
            break;
        }
        case QEvent::TouchCancel:
            m_touch=false;
            m_target.clear();
            return false;
        default:
            return false;
        }
        if(start) {
            m_target=textEditor(qobject_cast<QWidget*>(object));
            if(!m_target && event->type()!=QEvent::MouseButtonPress)
                m_target=textEditor(QApplication::widgetAt(global));
            m_start=global;
        } else {
            if((global-m_start).manhattanLength()>=QApplication::startDragDistance())
                m_target.clear();
            if(end) {
                if(m_target && m_target->rect().contains(m_target->mapFromGlobal(global))) {
                    auto *editor=m_target.data();
                    QTimer::singleShot(0,editor,[editor]() {
                        if(!editor->isVisible() || !editor->isEnabled() ||
                           !editor->window()->isActiveWindow() || !textEditor(editor))return;
                        editor->setFocus(Qt::MouseFocusReason);
                        auto *input=QGuiApplication::inputMethod();
                        input->update(Qt::ImQueryAll);
                        input->show();
                    });
                }
                m_touch=false;
                m_target.clear();
            }
        }
        return false;
    }

private:
    static QWidget *textEditor(QWidget *widget)
    {
        for(;widget && widget->isEnabled();widget=widget->parentWidget()) {
            if(auto *line=qobject_cast<QLineEdit*>(widget))
                return line->isReadOnly()?nullptr:line;
            if(auto *plain=qobject_cast<QPlainTextEdit*>(widget))
                return plain->isReadOnly()?nullptr:plain;
            if(auto *rich=qobject_cast<QTextEdit*>(widget))
                return rich->isReadOnly()?nullptr:rich;
            if(widget->isWindow())break;
        }
        return nullptr;
    }

    QPointer<QWidget> m_target;
    QPoint m_start;
    bool m_touch=false;
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
