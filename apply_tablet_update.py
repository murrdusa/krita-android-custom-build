#!/usr/bin/env python3
"""Second-stage Android tablet UX customization for Krita v6.0.4.

Run *after* apply_customizations.py. Patches the checked-out Krita tree,
not the copy in the Play Store. The new controls are Android-only.
"""
import sys
from pathlib import Path

def main(root: Path):
    modified = {}
    def change(path, old, new, expected=1):
        src = modified.get(path)
        if src is None:
            src = (root / path).read_text(encoding="utf-8")
        count = src.count(old)
        if count != expected:
            raise RuntimeError(f"{path}: expected {expected} instances of {old[:85]!r}, got {count}")
        modified[path] = src.replace(old, new)

    # Separate, persistent timing control for the existing one-finger
    # double-tap matcher. Default 250ms instead of the Android/Qt interval.
    path = "libs/ui/input/kis_shortcut_matcher.cpp"
    change(path, "#include <QTimer>\n", "#include <QTimer>\n#include <QSettings>\n")
    change(path, "QApplication::doubleClickInterval()",
           'qBound(120, QSettings().value(QStringLiteral("TabletUi/doubleTapMs"), 250).toInt(), 600)',
           expected=2)

    # The normal sampler can still be used continuously. Our new action
    # selects the sampler for precisely one stylus contact, then returns to
    # the freehand brush; the tap never paints a brush stroke.
    path = "plugins/tools/basictools/kis_tool_colorsampler.cc"
    change(path, "#include <QSignalBlocker>\n",
           "#include <QSignalBlocker>\n#include <QSettings>\n#include <QTimer>\n#include <kactioncollection.h>\n")
    change(path, """    m_isActivated = true;
    m_config->load();

    updateOptionWidget();""",
           """    m_isActivated = true;
    m_config->load();
#ifdef Q_OS_ANDROID
    m_config->sampleMerged = QSettings().value(QStringLiteral("TabletUi/sampleMerged"), true).toBool();
#endif

    updateOptionWidget();""")
    change(path, """    m_helper.deactivate();
    requestUpdateOutline(event->point, event);

}""",
           """    m_helper.deactivate();
    requestUpdateOutline(event->point, event);

#ifdef Q_OS_ANDROID
    QSettings tabletSettings;
    if (tabletSettings.value(QStringLiteral("TabletUi/oneShotSampler"), false).toBool()) {
        tabletSettings.setValue(QStringLiteral("TabletUi/oneShotSampler"), false);
        QTimer::singleShot(0, this, [this]() {
            KisCanvas2 *kisCanvas = dynamic_cast<KisCanvas2 *>(canvas());
            if (!kisCanvas || !kisCanvas->viewManager()) return;
            QAction *brushAction = kisCanvas->viewManager()->actionCollection()->action(
                QStringLiteral("KritaShape/KisToolBrush"));
            if (brushAction) brushAction->trigger();
        });
    }
#endif
}""")

    # The custom actions use Krita's existing command implementations.
    # We do not replace window state/Android immersive mode when Compact UI
    # is toggled, avoiding the status bar regression in Canvas Only mode.
    path = "libs/ui/KisMainWindow.cpp"
    change(path, "#include <QActionGroup>\n", """#include <QActionGroup>
#ifdef Q_OS_ANDROID
#include <QSettings>
#include <QTimer>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QVBoxLayout>
#endif
""")
    change(path, """    d->fullScreenMode = actionManager->createStandardAction(KStandardAction::FullScreen, this, SLOT(viewFullscreen(bool)));

    d->recentFiles""",
           """    d->fullScreenMode = actionManager->createStandardAction(KStandardAction::FullScreen, this, SLOT(viewFullscreen(bool)));

#ifdef Q_OS_ANDROID
    QAction *tabletCompact = new QAction(i18n("Compact UI"), this);
    tabletCompact->setIcon(KisIconUtils::loadIcon("view-fullscreen"));
    tabletCompact->setCheckable(true);
    tabletCompact->setToolTip(i18n("Show or hide menu and document tabs; keep the current workspace and Android immersive mode"));
    actionCollection()->addAction(QStringLiteral("tablet_compact_ui"), tabletCompact);

    connect(tabletCompact, &QAction::toggled, this, [this](bool enabled) {
        QSettings().setValue(QStringLiteral("TabletUi/compact"), enabled);
        if (enabled) {
            menuBar()->setProperty("tabletWasVisible", menuBar()->isVisible());
            menuBar()->hide();
            if (QTabBar *tabs = d->findTabBarHACK()) {
                tabs->setProperty("tabletWasVisible", tabs->isVisible());
                tabs->hide();
            }
            if (statusBar()) {
                statusBar()->setProperty("tabletWasVisible", statusBar()->isVisible());
                statusBar()->hide();
            }
        } else {
            menuBar()->setVisible(menuBar()->property("tabletWasVisible").toBool());
            if (QTabBar *tabs = d->findTabBarHACK()) {
                tabs->setVisible(tabs->property("tabletWasVisible").toBool());
            }
            if (statusBar()) {
                statusBar()->setVisible(statusBar()->property("tabletWasVisible").toBool());
            }
        }
    });
    connect(d->mdiArea, &QMdiArea::subWindowActivated, this,
            [this, tabletCompact](QMdiSubWindow *) {
        if (tabletCompact->isChecked()) {
            QTimer::singleShot(0, this, [this, tabletCompact] {
                if (!tabletCompact->isChecked()) return;
                if (QTabBar *tabs = d->findTabBarHACK()) tabs->hide();
            });
        }
    });
    QTimer::singleShot(0, this, [tabletCompact]() {
        tabletCompact->setChecked(QSettings().value(QStringLiteral("TabletUi/compact"), true).toBool());
    });

    QAction *oneShot = new QAction(i18n("Pick Color Once"), this);
    oneShot->setIcon(KisIconUtils::loadIcon("color-picker"));
    oneShot->setToolTip(i18n("Next stylus tap samples a color and returns to the brush"));
    actionCollection()->addAction(QStringLiteral("tablet_pick_once"), oneShot);
    connect(oneShot, &QAction::triggered, this, [this]() {
        QAction *picker = actionCollection()->action(QStringLiteral("KritaSelected/KisToolColorSampler"));
        if (!picker) return;
        QSettings().setValue(QStringLiteral("TabletUi/oneShotSampler"), true);
        picker->trigger();
    });

    QAction *sampleMerged = new QAction(i18n("Sample Visible Layers"), this);
    sampleMerged->setToolTip(i18n("Color picker samples all visible layers when checked, current layer otherwise"));
    sampleMerged->setIcon(KisIconUtils::loadIcon("color-picker"));
    sampleMerged->setCheckable(true);
    sampleMerged->setChecked(QSettings().value(QStringLiteral("TabletUi/sampleMerged"), true).toBool());
    actionCollection()->addAction(QStringLiteral("tablet_sample_merged"), sampleMerged);
    connect(sampleMerged, &QAction::toggled, this, [](bool checked) {
        QSettings().setValue(QStringLiteral("TabletUi/sampleMerged"), checked);
    });

    QAction *resetTabletView = new QAction(i18n("Reset Canvas View"), this);
    resetTabletView->setToolTip(i18n("Reset rotation and fit the entire image"));
    resetTabletView->setIcon(KisIconUtils::loadIcon("zoom-fit-best"));
    actionCollection()->addAction(QStringLiteral("tablet_reset_view"), resetTabletView);
    connect(resetTabletView, &QAction::triggered, this, [this]() {
        QAction *rotation = actionCollection()->action(QStringLiteral("reset_canvas_rotation"));
        QAction *fit = actionCollection()->action(QStringLiteral("zoom_to_fit"));
        if (rotation) rotation->trigger();
        if (fit) fit->trigger();
    });

    QAction *tabletSettings = new QAction(i18n("Tablet Controls..."), this);
    tabletSettings->setIcon(KisIconUtils::loadIcon("configure"));
    actionCollection()->addAction(QStringLiteral("tablet_settings"), tabletSettings);
    connect(tabletSettings, &QAction::triggered, this, [this]() {
        QSettings stored;
        QDialog dialog(this);
        dialog.setWindowTitle(i18n("Tablet Controls"));
        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        QFormLayout *form = new QFormLayout;
        QSpinBox *doubleTapMs = new QSpinBox(&dialog);
        doubleTapMs->setRange(120, 600);
        doubleTapMs->setSuffix(i18n(" ms"));
        doubleTapMs->setValue(stored.value(QStringLiteral("TabletUi/doubleTapMs"), 250).toInt());
        form->addRow(i18n("Double-tap interval"), doubleTapMs);
        QSpinBox *pinchMs = new QSpinBox(&dialog);
        pinchMs->setRange(120, 500);
        pinchMs->setSuffix(i18n(" ms"));
        pinchMs->setValue(stored.value(QStringLiteral("TabletUi/pinchMs"), 250).toInt());
        form->addRow(i18n("Quick-pinch interval (experimental)"), pinchMs);
        QCheckBox *volumeKeys = new QCheckBox(i18n("Volume down: Undo / Volume up: Redo"), &dialog);
        volumeKeys->setChecked(stored.value(QStringLiteral("TabletUi/volumeKeys"), true).toBool());
        form->addRow(volumeKeys);
        QCheckBox *pinchEnabled = new QCheckBox(i18n("Enable quick-pinch reset (experimental)"), &dialog);
        pinchEnabled->setChecked(stored.value(QStringLiteral("TabletUi/quickPinch"), false).toBool());
        form->addRow(pinchEnabled);
        layout->addLayout(form);
        QDialogButtonBox *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            stored.setValue(QStringLiteral("TabletUi/doubleTapMs"), doubleTapMs->value());
            stored.setValue(QStringLiteral("TabletUi/pinchMs"), pinchMs->value());
            stored.setValue(QStringLiteral("TabletUi/volumeKeys"), volumeKeys->isChecked());
            stored.setValue(QStringLiteral("TabletUi/quickPinch"), pinchEnabled->isChecked());
        }
    });

    QMenu *quickMenu = new QMenu(i18n("Quick Actions"), this);
    quickMenu->menuAction()->setIcon(KisIconUtils::loadIcon("application-menu"));
    actionCollection()->addAction(QStringLiteral("tablet_quick_actions"), quickMenu->menuAction());
    connect(quickMenu, &QMenu::aboutToShow, this, [this, quickMenu]() {
        quickMenu->clear();
        auto add = [this, quickMenu](const char *name) {
            if (QAction *action = actionCollection()->action(QLatin1String(name))) {
                quickMenu->addAction(action);
            }
        };
        add("file_new"); add("file_open"); add("file_save_as");
        add("file_export_file");
        quickMenu->addSeparator();
        add("select_all"); add("invert_selection"); add("reselect");
        add("deselect"); add("fill_selection_foreground_color");
        quickMenu->addSeparator();
        add("duplicate_layer"); add("merge_layer"); add("quick_group");
        quickMenu->addSeparator();
        add("edit_undo"); add("edit_redo");
        add("tablet_reset_view");
        add("tablet_settings");
    });
#endif

    d->recentFiles""")

    # Keep brush opacity and size sliders in their original tool bar. The
    # Android preset makes common actions visible without crowding those sliders.
    path = "krita/krita5.xmlgui"
    change(path, """  <ToolBar name="mainToolBar" fullWidth="false" noMerge="1">
    <Text>File</Text>
    <Action name="file_new"/>
    <Action name="file_open"/>
    <Action name="file_save"/>
  </ToolBar>
  <ToolBar name="editToolBar" fullWidth="false" noMerge="1">
    <Text>Edit</Text>
    <Action name="edit_undo"/>
    <Action name="edit_redo"/>
  </ToolBar>""", """  <ToolBar name="mainToolBar" fullWidth="false" noMerge="1">
    <Text>File</Text>
    <Action name="file_save"/>
    <Action name="deselect"/>
    <Action name="toggle_layer_alpha_lock"/>
    <Action name="tablet_compact_ui"/>
    <Action name="tablet_quick_actions"/>
  </ToolBar>
  <ToolBar name="editToolBar" fullWidth="false" noMerge="1">
    <Text>Edit</Text>
    <Action name="tablet_pick_once"/>
    <Action name="tablet_sample_merged"/>
    <Action name="view_toggle_reference_images"/>
    <Action name="tablet_reset_view"/>
    <Action name="tablet_settings"/>
  </ToolBar>""")
    change(path, """    <Action name="brushslider2"/>
    <Action name="mirror_actions"/>""",
                 """    <Action name="brushslider2"/>
    <Action name="reload_preset_action"/>
    <Action name="mirror_actions"/>""")

    # Native Android hardware buttons, implemented on Qt's GUI thread.
    path = "packaging/android/apk/src/org/krita/android/JNIWrappers.java"
    change(path, "    public static native boolean hasMainWindowLoaded();",
           """    public static native boolean hasMainWindowLoaded();
    public static native boolean volumeMappingEnabled();
    public static native void volumeUndoRedo(boolean redo);""")
    path = "packaging/android/apk/src/org/krita/android/MainActivity.java"
    change(path, """    @Override
    public boolean onKeyUp(final int keyCode, final KeyEvent event) {""",
           """    @Override
    public boolean onKeyDown(final int keyCode, final KeyEvent event) {
        if ((keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_UP)
                && haveLibsLoaded && JNIWrappers.volumeMappingEnabled()) {
            if (event.getRepeatCount() == 0) {
                JNIWrappers.volumeUndoRedo(keyCode == KeyEvent.KEYCODE_VOLUME_UP);
            }
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(final int keyCode, final KeyEvent event) {
        if ((keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_UP)
                && haveLibsLoaded && JNIWrappers.volumeMappingEnabled()) {
            return true;
        }""")
    path = "krita/main.cc"
    change(path, """#ifdef Q_OS_ANDROID
#include <QtAndroid>""",
           """#ifdef Q_OS_ANDROID
#include <QSettings>
#include <QtAndroid>""")
    change(path, """#define MAIN_EXPORT __attribute__ ((visibility ("default")))
#define MAIN_FN main""",
           """extern "C" JNIEXPORT jboolean JNICALL
Java_org_krita_android_JNIWrappers_volumeMappingEnabled(JNIEnv*, jobject)
{
    return QSettings().value(QStringLiteral("TabletUi/volumeKeys"), true).toBool();
}

extern "C" JNIEXPORT void JNICALL
Java_org_krita_android_JNIWrappers_volumeUndoRedo(JNIEnv*, jobject, jboolean redo)
{
    if (!KisPart::exists()) return;
    KisMainWindow *window = KisPart::instance()->currentMainwindow();
    if (window) {
        QMetaObject::invokeMethod(window, redo ? "redo" : "undo", Qt::QueuedConnection);
    }
}

#define MAIN_EXPORT __attribute__ ((visibility ("default")))
#define MAIN_FN main""")

    # Increase Android selection bar touch targets; pin it to the bottom in
    # compact mode. Uses Krita's existing selection actions and undo logic.
    path = "libs/ui/kis_selection_actions_panel.cpp"
    change(path, """static constexpr int BUTTON_SIZE = 30;""",
           """#ifdef Q_OS_ANDROID
static constexpr int BUTTON_SIZE = 44;
#else
static constexpr int BUTTON_SIZE = 30;
#endif""")
    # Krita already has a fixed-bottom behavior. Keep its users' normal
    # settings when not in Compact UI.
    change(path, """    configChanged(true);

}""",
           """    configChanged(true);

}""") if False else None

    # Optional quick two-finger inward pinch; the original pinch gesture still
    # runs normally. The reset is applied only at gesture completion, after an
    # unmistakable rapid inward movement. Defaults OFF until user opts in.
    path = "libs/ui/input/kis_input_manager.cpp"
    change(path, "#include <QTouchEvent>\n", """#include <QTouchEvent>
#ifdef Q_OS_ANDROID
#include <QSettings>
#include <QElapsedTimer>
#include <QLineF>
#include <QTimer>
#include <QHash>
#include <KisPart.h>
#include <KisMainWindow.h>
#include <kactioncollection.h>
#endif
""")
    change(path, """KisInputManager::KisInputManager(QObject *parent)
""", """#ifdef Q_OS_ANDROID
namespace {
struct TabletPinchTracking {
    QElapsedTimer age;
    qreal startDistance = 0;
    qreal smallestDistance = 0;
    bool tracked = false;
};
QHash<KisInputManager*, TabletPinchTracking> tabletPinches;
}
#endif

KisInputManager::KisInputManager(QObject *parent)
""")
    change(path, """KisInputManager::~KisInputManager()
{
""", """KisInputManager::~KisInputManager()
{
#ifdef Q_OS_ANDROID
    tabletPinches.remove(this);
#endif
""")
    change(path, """    case QEvent::TouchBegin:
    {
        d->debugEvent<QTouchEvent, false>(event);""", """    case QEvent::TouchBegin:
    {
#ifdef Q_OS_ANDROID
        tabletPinches.remove(this);
#endif
        d->debugEvent<QTouchEvent, false>(event);""")
    change(path, """        QTouchEvent *touchEvent = static_cast<QTouchEvent*>(event);
        d->debugEvent<QTouchEvent, false>(event);

        int eventPointCount = touchEvent->touchPoints().size();""",
           """        QTouchEvent *touchEvent = static_cast<QTouchEvent*>(event);
        d->debugEvent<QTouchEvent, false>(event);
#ifdef Q_OS_ANDROID
        if (QSettings().value(QStringLiteral("TabletUi/quickPinch"), false).toBool()
            && touchEvent->touchPoints().size() == 2) {
            const auto &points = touchEvent->touchPoints();
            qreal distance = QLineF(points.at(0).pos(), points.at(1).pos()).length();
            TabletPinchTracking &tracker = tabletPinches[this];
            if (!tracker.tracked && distance > 80) {
                tracker.tracked = true;
                tracker.startDistance = distance;
                tracker.smallestDistance = distance;
                tracker.age.start();
            } else if (tracker.tracked) {
                tracker.smallestDistance = qMin(tracker.smallestDistance, distance);
            }
        }
#endif

        int eventPointCount = touchEvent->touchPoints().size();""")
    change(path, """        retval = d->matcher.touchEndEvent(touchEvent);
        if (d->touchStrokeStarted) {""",
           """#ifdef Q_OS_ANDROID
        auto tracker = tabletPinches.take(this);
        if (tracker.tracked && tracker.age.isValid() &&
            tracker.age.elapsed() <= qBound(120, QSettings().value(
                QStringLiteral("TabletUi/pinchMs"), 250).toInt(), 500) &&
            tracker.startDistance - tracker.smallestDistance > 45 &&
            tracker.smallestDistance < tracker.startDistance * 0.68) {
            QTimer::singleShot(0, this, []() {
                if (!KisPart::exists()) return;
                KisMainWindow *window = KisPart::instance()->currentMainwindow();
                if (!window) return;
                QAction *rotation = window->actionCollection()->action(
                    QStringLiteral("reset_canvas_rotation"));
                QAction *fit = window->actionCollection()->action(QStringLiteral("zoom_to_fit"));
                if (rotation) rotation->trigger();
                if (fit) fit->trigger();
            });
        }
#endif
        retval = d->matcher.touchEndEvent(touchEvent);
        if (d->touchStrokeStarted) {""")

    # Direct stylus sampling on reference images, regardless of drawing tool.
    # No brush stroke is allowed to start for the consumed tablet sequence.
    path = "libs/ui/input/kis_input_manager.cpp"
    change(path, "#include <QHash>\n", """#include <QHash>
#include <QSet>
#include <KisDocument.h>
#include <KisReferenceImagesLayer.h>
#include <KisReferenceImagesDecoration.h>
#include <KoColor.h>
#include <KoCanvasResource.h>
#include <kis_coordinates_converter.h>
""")
    change(path, """QHash<KisInputManager*, TabletPinchTracking> tabletPinches;
}""",
           """QHash<KisInputManager*, TabletPinchTracking> tabletPinches;
QSet<KisInputManager*> tabletReferencePenPresses;
}""")
    change(path, """    tabletPinches.remove(this);
#endif
    delete d;""",
           """    tabletPinches.remove(this);
    tabletReferencePenPresses.remove(this);
#endif
    delete d;""")
    change(path, """    case QEvent::TabletPress: {
        d->debugEvent<QTabletEvent, false>(event);
        QTabletEvent *tabletEvent = static_cast<QTabletEvent*>(event);

        {""",
           """    case QEvent::TabletPress: {
        d->debugEvent<QTabletEvent, false>(event);
        QTabletEvent *tabletEvent = static_cast<QTabletEvent*>(event);
#ifdef Q_OS_ANDROID
        if (tabletEvent->button() == Qt::LeftButton &&
            QSettings().value(QStringLiteral("TabletUi/referencePenSample"), true).toBool() &&
            KisPart::exists()) {
            KisMainWindow *window = KisPart::instance()->currentMainwindow();
            KisCanvas2 *canvas = window && window->viewManager()
                ? window->viewManager()->canvasBase() : nullptr;
            if (canvas && window->activeView() &&
                canvas->referenceImagesDecoration() &&
                canvas->referenceImagesDecoration()->visible()) {
                KisReferenceImagesLayerSP layer =
                    window->activeView()->document()->referenceImagesLayer();
                if (layer) {
                    QColor sampled = layer->getPixel(
                        canvas->coordinatesConverter()->widgetToImage(tabletEvent->posF()));
                    if (sampled.isValid() && sampled.alpha() > 0) {
                        KoColor result(sampled, canvas->image()->colorSpace());
                        canvas->resourceManager()->setResource(
                            KoCanvasResource::ForegroundColor, result);
                        tabletReferencePenPresses.insert(this);
                        d->blockMouseEvents();
                        event->accept();
                        return true;
                    }
                }
            }
        }
#endif

        {""")
    change(path, """    case QEvent::TabletMove: {
        d->debugEvent<QTabletEvent, false>(event);""",
           """    case QEvent::TabletMove: {
#ifdef Q_OS_ANDROID
        if (tabletReferencePenPresses.contains(this)) {
            event->accept();
            return true;
        }
#endif
        d->debugEvent<QTabletEvent, false>(event);""")
    change(path, """    case QEvent::TabletRelease: {
#if defined(Q_OS_MAC) || defined(Q_OS_ANDROID)""",
           """    case QEvent::TabletRelease: {
#ifdef Q_OS_ANDROID
        if (tabletReferencePenPresses.remove(this)) {
            d->allowMouseEvents();
            event->accept();
            return true;
        }
#endif
#if defined(Q_OS_MAC) || defined(Q_OS_ANDROID)""")

    # Replace the v1 popup gallery with an inline page occupying the same
    # area as Recent Images; hide the news column on Android.
    path = "libs/ui/KisWelcomePageWidget.cpp"
    src = modified.get(path, (root / path).read_text(encoding="utf-8"))
    begin_token = "        auto *galleryButton = new QToolButton(this);"
    begin = src.find(begin_token)
    end = src.find("\n#endif", begin)
    if begin < 0 or end < 0 or src.count(begin_token) != 1:
        raise RuntimeError("Could not locate v1 gallery injection")
    gallery_code = r'''        widgetRight->hide();
        auto *galleryButton = new QToolButton(this);
        galleryButton->setText(i18n("Project Gallery"));
        galleryButton->setToolTip(i18n("Switch between recent projects and the folder gallery"));
        horizontalLayout_10->addWidget(galleryButton);
        QStackedWidget *pages = recentDocsStackedWidget;
        QWidget *recentPage = pages->currentWidget();
        QWidget *galleryPage = new QWidget(pages);
        QVBoxLayout *galleryLayout = new QVBoxLayout(galleryPage);
        auto *galleryControls = new QHBoxLayout;
        auto *folderButton = new QPushButton(i18n("Choose Folder"), galleryPage);
        auto *refreshButton = new QPushButton(i18n("Refresh"), galleryPage);
        auto *sortMode = new QComboBox(galleryPage);
        sortMode->addItem(i18n("Newest first"));
        sortMode->addItem(i18n("Name A-Z"));
        galleryControls->addWidget(folderButton);
        galleryControls->addWidget(refreshButton);
        galleryControls->addWidget(sortMode);
        galleryLayout->addLayout(galleryControls);
        auto *galleryList = new QListWidget(galleryPage);
        galleryList->setViewMode(QListView::IconMode);
        galleryList->setIconSize(QSize(156, 156));
        galleryList->setGridSize(QSize(184, 205));
        galleryList->setResizeMode(QListView::Adjust);
        galleryList->setMovement(QListView::Static);
        galleryList->setWordWrap(true);
        galleryLayout->addWidget(galleryList);
        pages->addWidget(galleryPage);
        auto folder = std::make_shared<QString>(
            QSettings().value(QStringLiteral("CustomProjectGalleryFolder"), QDir::homePath()).toString());
        auto populate = [this, galleryList, folder, sortMode]() {
            galleryList->clear();
            if (!folder->isEmpty() && QDir(*folder).exists()) {
                QFileInfoList files;
                QDirIterator iter(*folder, QStringList() << QStringLiteral("*.kra")
                                                        << QStringLiteral("*.krz"),
                                  QDir::Files, QDirIterator::Subdirectories);
                while (iter.hasNext() && files.size() < 2000) {
                    iter.next();
                    files.append(iter.fileInfo());
                }
                if (sortMode->currentIndex() == 0) {
                    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
                        return a.lastModified() > b.lastModified();
                    });
                } else {
                    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
                        return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0;
                    });
                }
                for (const QFileInfo &file : files) {
                    QIcon icon;
                    QScopedPointer<KoStore> store(KoStore::createStore(file.absoluteFilePath(), KoStore::Read));
                    if (store && !store->bad()) {
                        for (const QString &entry : {QStringLiteral("preview.png"),
                                                     QStringLiteral("Thumbnails/thumbnail.png")}) {
                            if (!store->hasFile(entry) || !store->open(entry)) continue;
                            QImage image;
                            image.loadFromData(store->read(store->size()), "PNG");
                            store->close();
                            if (!image.isNull()) {
                                icon = QIcon(QPixmap::fromImage(image.scaled(
                                    156, 156, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                                break;
                            }
                        }
                    }
                    auto *item = new QListWidgetItem(icon, file.completeBaseName(), galleryList);
                    item->setData(Qt::UserRole, file.absoluteFilePath());
                }
            }
            // Keep SAF content URI projects in reach through Krita recent files.
            QSet<QString> seen;
            for (int i = 0; i < galleryList->count(); ++i)
                seen.insert(galleryList->item(i)->data(Qt::UserRole).toString());
            const QAbstractItemModel *recent = recentDocumentsListView->model();
            if (recent) {
                for (int i = 0; i < recent->rowCount(); ++i) {
                    QModelIndex index = recent->index(i, 0);
                    QString uri = index.data(Qt::ToolTipRole).toString();
                    if (uri.isEmpty() || seen.contains(uri)) continue;
                    auto *item = new QListWidgetItem(
                        qvariant_cast<QIcon>(index.data(Qt::DecorationRole)),
                        index.data(Qt::DisplayRole).toString(), galleryList);
                    item->setData(Qt::UserRole, uri);
                    seen.insert(uri);
                }
            }
        };
        connect(galleryButton, &QToolButton::clicked, this,
                [pages, recentPage, galleryPage, galleryButton, populate, this]() {
            if (pages->currentWidget() == galleryPage) {
                pages->setCurrentWidget(recentPage);
                galleryButton->setText(i18n("Project Gallery"));
                recentDocumentsLabel->setText(i18n("Recent Images"));
                clearRecentFilesLink->show();
            } else {
                populate();
                pages->setCurrentWidget(galleryPage);
                galleryButton->setText(i18n("Recent Images"));
                recentDocumentsLabel->setText(i18n("Project Gallery"));
                clearRecentFilesLink->hide();
            }
        });
        connect(folderButton, &QPushButton::clicked, this, [this, folder, populate]() {
            QString selection = QFileDialog::getExistingDirectory(
                this, i18n("Choose a Krita Projects Folder"), *folder);
            if (!selection.isEmpty()) {
                *folder = selection;
                QSettings().setValue(QStringLiteral("CustomProjectGalleryFolder"), *folder);
                populate();
            }
        });
        connect(refreshButton, &QPushButton::clicked, this, populate);
        connect(sortMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [populate](int) { populate(); });
        connect(galleryList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            m_mainWindow->openDocument(item->data(Qt::UserRole).toString(), KisMainWindow::None);
        });
'''
    src = src[:begin] + gallery_code + src[end:]
    # New gallery uses std::shared_ptr and QComboBox.
    if "#include <algorithm>\n" not in src:
        raise RuntimeError("Gallery dependencies from v1 not applied")
    src = src.replace("#include <algorithm>\n",
                      "#include <algorithm>\n#include <memory>\n#include <QComboBox>\n", 1)
    modified[path] = src

    for path, contents in modified.items():
        (root / path).write_text(contents, encoding="utf-8")
        print(f"tablet UI patch: {path}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: apply_tablet_update.py /path/to/krita")
    main(Path(sys.argv[1]))
