#!/usr/bin/env python3
"""Experimental changes to KDE Krita v6.0.4 Android (Krita 5.3.4).
Provides assignable one-finger double-tap, Mirror View, narrow Layers
docker and a welcome-page .kra thumbnail gallery. Not compiled or tested.
"""
from pathlib import Path
import sys

def main(root):
    if not (root / 'libs/ui/KisWelcomePageWidget.cpp').is_file():
        raise SystemExit('Pass the root of Krita tag v6.0.4')
    changed = {}
    def replace(path, old, new):
        data = changed.get(path)
        if data is None:
            data = (root / path).read_text(encoding='utf-8')
        count = data.count(old)
        if count != 1:
            raise RuntimeError(f'{path}: expected one match; found {count}. Use v6.0.4.')
        changed[path] = data.replace(old, new, 1)

    # Keep serialized IDs stable by appending the gesture to the enum.
    p = 'libs/ui/input/kis_shortcut_configuration.h'
    replace(p, '''        FiveFingerDrag,
        OneFingerHold,
#endif
        MaxGesture,''', '''        FiveFingerDrag,
        OneFingerHold,
        OneFingerDoubleTap,
#endif
        MaxGesture,''')
    p = 'libs/ui/input/kis_shortcut_configuration.cpp'
    replace(p, '''    case OneFingerHold:
        return i18n("One Finger Hold");
#endif''', '''    case OneFingerHold:
        return i18n("One Finger Hold");
    case OneFingerDoubleTap:
        return i18n("One Finger Double Tap");
#endif''')
    p = 'libs/ui/input/KisTouchGestureAction.h'
    replace(p, '''        ToggleEraserPreset,
    };''', '''        ToggleEraserPreset,
        MirrorView,
    };''')
    p = 'libs/ui/input/KisTouchGestureAction.cpp'
    replace(p, '''    shortcuts.insert(i18n("Activate Transform Tool"), KisToolTransform);
    setShortcutIndexes(shortcuts);''', '''    shortcuts.insert(i18n("Activate Transform Tool"), KisToolTransform);
    shortcuts.insert(i18n("Mirror View"), MirrorView);
    setShortcutIndexes(shortcuts);''')
    replace(p, '''    case ToggleEraserPreset:
        actionName = QStringLiteral("eraser_preset_action");
        break;
    }
''', '''    case ToggleEraserPreset:
        actionName = QStringLiteral("eraser_preset_action");
        break;
    case MirrorView:
        actionName = QStringLiteral("mirror_canvas");
        break;
    }
''')
    p = 'libs/ui/input/kis_input_manager_p.cpp'
    replace(p, '''    case KisShortcutConfiguration::OneFingerTap:
    case KisShortcutConfiguration::OneFingerDrag:
''', '''    case KisShortcutConfiguration::OneFingerTap:
    case KisShortcutConfiguration::OneFingerDoubleTap:
    case KisShortcutConfiguration::OneFingerDrag:
''')
    p = 'libs/ui/input/kis_touch_shortcut.h'
    replace(p, '''        bool isHoldType() const;
''', '''        bool isHoldType() const;
        bool isDoubleTapType() const;
''')
    replace(p, '''        bool matchTapType(QTouchEvent *event);
''', '''        bool matchTapType(QTouchEvent *event);
        bool matchDoubleTapType(QTouchEvent *event);
''')
    p = 'libs/ui/input/kis_touch_shortcut.cpp'
    replace(p, '''void KisTouchShortcut::setMinimumTouchPoints(int min)
''', '''bool KisTouchShortcut::isDoubleTapType() const
{
#ifdef Q_OS_MACOS
    return false;
#else
    return d->type == KisShortcutConfiguration::OneFingerDoubleTap;
#endif
}

void KisTouchShortcut::setMinimumTouchPoints(int min)
''')
    replace(p, '''bool KisTouchShortcut::matchDragType(QTouchEvent *event)
''', '''bool KisTouchShortcut::matchDoubleTapType(QTouchEvent *event)
{
    return isDoubleTapType() && matchTouchPoint(event);
}

bool KisTouchShortcut::matchDragType(QTouchEvent *event)
''')
    p = 'libs/ui/input/kis_shortcut_matcher.h'
    replace(p, '''    void fireReadyTouchShortcut(QTouchEvent *event);
''', '''    void fireReadyTouchShortcut(QTouchEvent *event, bool bypassDoubleTap = false);
''')
    p = 'libs/ui/input/kis_shortcut_matcher.cpp'
    replace(p, '''#include <QTabletEvent>
''', '''#include <QTabletEvent>
#include <QApplication>
#include <QElapsedTimer>
#include <QTimer>
''')
    replace(p, '''    QScopedPointer<QEvent> bestCandidateTouchEvent;
''', '''    QScopedPointer<QEvent> bestCandidateTouchEvent;

    // Delay a single tap only when a double-tap shortcut is assigned.
    QTimer pendingSingleTapTimer;
    QElapsedTimer lastOneFingerTapAge;
    QPointF lastOneFingerTapPosition;
    QScopedPointer<QEvent> pendingSingleTapEvent;
''')
    replace(p, '''KisShortcutMatcher::KisShortcutMatcher()
    : m_d(new Private)
{}
''', '''KisShortcutMatcher::KisShortcutMatcher()
    : m_d(new Private)
{
    m_d->pendingSingleTapTimer.setSingleShot(true);
    QObject::connect(&m_d->pendingSingleTapTimer, &QTimer::timeout, [this]() {
        if (m_d->pendingSingleTapEvent) {
            auto *queued = static_cast<QTouchEvent *>(m_d->pendingSingleTapEvent.data());
            fireReadyTouchShortcut(queued, true);
            m_d->pendingSingleTapEvent.reset();
        }
        m_d->lastOneFingerTapAge.invalidate();
    });
}
''')
    replace(p, '''void KisShortcutMatcher::fireReadyTouchShortcut(QTouchEvent *event)
{
    KisTouchShortcut *goodCandidate = matchTouchShortcut(event);
    if (goodCandidate) {
        DEBUG_TOUCH_ACTION("starting", event)
        goodCandidate->action()->activate(goodCandidate->shortcutIndex());
        goodCandidate->action()->begin(goodCandidate->shortcutIndex(), event);

        goodCandidate->action()->end(event);
        goodCandidate->action()->deactivate(goodCandidate->shortcutIndex());
    }
}
''', '''void KisShortcutMatcher::fireReadyTouchShortcut(QTouchEvent *event, bool bypassDoubleTap)
{
    KisTouchShortcut *doubleTap = nullptr;
    if (!bypassDoubleTap && !m_d->isTouchHeld && !m_d->isTouchDragDetected &&
        event->touchPoints().size() == 1 && !m_d->actionsSuppressedIgnoreFocus()) {
        for (KisTouchShortcut *shortcut : m_d->touchShortcuts) {
            if (shortcut->matchDoubleTapType(event) &&
                shortcut->isAvailable(m_d->actionGroupMask()) &&
                (!doubleTap || shortcut->priority() > doubleTap->priority())) {
                doubleTap = shortcut;
            }
        }
    }
    if (doubleTap) {
        const QPointF point = event->touchPoints().first().pos();
        const qreal threshold = qMax(qreal(24), qreal(QApplication::startDragDistance()) * 2);
        const QPointF delta = point - m_d->lastOneFingerTapPosition;
        const bool nearby = delta.x() * delta.x() + delta.y() * delta.y() <= threshold * threshold;
        if (m_d->pendingSingleTapTimer.isActive() &&
            m_d->lastOneFingerTapAge.isValid() &&
            m_d->lastOneFingerTapAge.elapsed() <= QApplication::doubleClickInterval() && nearby) {
            m_d->pendingSingleTapTimer.stop();
            m_d->pendingSingleTapEvent.reset();
            m_d->lastOneFingerTapAge.invalidate();
            doubleTap->action()->activate(doubleTap->shortcutIndex());
            doubleTap->action()->begin(doubleTap->shortcutIndex(), event);
            doubleTap->action()->end(event);
            doubleTap->action()->deactivate(doubleTap->shortcutIndex());
            return;
        }
        if (m_d->pendingSingleTapEvent) {
            m_d->pendingSingleTapTimer.stop();
            auto *queued = static_cast<QTouchEvent *>(m_d->pendingSingleTapEvent.data());
            fireReadyTouchShortcut(queued, true);
            m_d->pendingSingleTapEvent.reset();
        }
        m_d->lastOneFingerTapPosition = point;
        m_d->lastOneFingerTapAge.start();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        KoPointerEvent::copyQtPointerEvent(event, m_d->pendingSingleTapEvent);
#else
        m_d->pendingSingleTapEvent.reset(event->clone());
#endif
        m_d->pendingSingleTapTimer.start(QApplication::doubleClickInterval());
        return;
    }
    KisTouchShortcut *candidate = nullptr;
    if (bypassDoubleTap) {
        for (KisTouchShortcut *shortcut : m_d->touchShortcuts) {
            if (shortcut->matchTapType(event) &&
                shortcut->isAvailable(m_d->actionGroupMask()) &&
                (!candidate || shortcut->priority() > candidate->priority())) {
                candidate = shortcut;
            }
        }
    } else {
        candidate = matchTouchShortcut(event);
    }
    if (candidate && !m_d->actionsSuppressedIgnoreFocus()) {
        candidate->action()->activate(candidate->shortcutIndex());
        candidate->action()->begin(candidate->shortcutIndex(), event);
        candidate->action()->end(event);
        candidate->action()->deactivate(candidate->shortcutIndex());
    }
}
''')
    replace(p, '''void KisShortcutMatcher::clearShortcuts()
{
    reset("Clearing shortcuts");
''', '''void KisShortcutMatcher::clearShortcuts()
{
    m_d->pendingSingleTapTimer.stop();
    m_d->pendingSingleTapEvent.reset();
    m_d->lastOneFingerTapAge.invalidate();
    reset("Clearing shortcuts");
''')

    # Layer controls: keep the necessary commands, move some to an overflow menu.
    p = 'plugins/dockers/layerdocker/LayerBox.cpp'
    replace(p, '''    m_wdgLayerBox->setupUi(mainWidget);
''', '''    m_wdgLayerBox->setupUi(mainWidget);
#ifdef Q_OS_ANDROID
    auto *moreLayers = new QToolButton(mainWidget);
    moreLayers->setText(i18n("More"));
    moreLayers->setToolTip(i18n("Additional layer actions"));
    moreLayers->setPopupMode(QToolButton::InstantPopup);
    auto *moreMenu = new QMenu(moreLayers);
    auto overflow = [moreMenu](const QString &label, QAbstractButton *button) {
        QAction *action = moreMenu->addAction(label);
        QObject::connect(action, &QAction::triggered, button, &QAbstractButton::click);
        button->hide();
    };
    overflow(i18n("Duplicate Layer"), m_wdgLayerBox->bnDuplicate);
    overflow(i18n("Move Layer Down"), m_wdgLayerBox->bnLower);
    overflow(i18n("Move Layer Up"), m_wdgLayerBox->bnRaise);
    overflow(i18n("Layer Properties"), m_wdgLayerBox->bnProperties);
    moreLayers->setMenu(moreMenu);
    m_wdgLayerBox->hbox1->addWidget(moreLayers);
    m_wdgLayerBox->opacityLabel->hide();
    m_wdgLayerBox->cmbComposite->setMinimumWidth(0);
    m_wdgLayerBox->cmbComposite->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_wdgLayerBox->doubleOpacity->setMinimumWidth(0);
    m_wdgLayerBox->doubleOpacity->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_wdgLayerBox->listLayers->setMinimumWidth(0);
    m_wdgLayerBox->bnLayerFilters->setFixedSize(24, 24);
    m_wdgLayerBox->configureLayerDockerToolbar->setFixedSize(24, 24);
    mainWidget->setMinimumWidth(85);
#endif
''')

    # Non-destructive thumbnail gallery on Android Welcome page. SAF URI folders
    # cannot be recursively enumerated with QDir; recent file entries are fallback.
    p = 'libs/ui/KisWelcomePageWidget.cpp'
    replace(p, '''#include <QDir>
''', '''#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <algorithm>
''')
    replace(p, '''        connect(openFileLink, SIGNAL(clicked(bool)), this, SLOT(slotOpenFileClicked()));
''', '''        connect(openFileLink, SIGNAL(clicked(bool)), this, SLOT(slotOpenFileClicked()));
#ifdef Q_OS_ANDROID
        auto *galleryButton = new QToolButton(this);
        galleryButton->setText(i18n("Project Gallery"));
        horizontalLayout_2->addWidget(galleryButton);
        connect(galleryButton, &QToolButton::clicked, this, [this]() {
            QDialog gallery(this);
            gallery.setWindowTitle(i18n("Project Gallery"));
            gallery.resize(760, 640);
            auto *column = new QVBoxLayout(&gallery);
            auto *buttons = new QHBoxLayout;
            auto *chooseFolder = new QPushButton(i18n("Choose Folder"), &gallery);
            auto *refresh = new QPushButton(i18n("Refresh"), &gallery);
            auto *newCanvas = new QPushButton(i18n("New Canvas"), &gallery);
            buttons->addWidget(chooseFolder);
            buttons->addWidget(refresh);
            buttons->addStretch(1);
            buttons->addWidget(newCanvas);
            column->addLayout(buttons);
            auto *projectList = new QListWidget(&gallery);
            projectList->setViewMode(QListView::IconMode);
            projectList->setIconSize(QSize(156, 156));
            projectList->setGridSize(QSize(184, 205));
            projectList->setResizeMode(QListView::Adjust);
            projectList->setMovement(QListView::Static);
            projectList->setWordWrap(true);
            projectList->setSpacing(5);
            column->addWidget(projectList);
            QSettings settings;
            QString folder = settings.value(QStringLiteral("CustomProjectGalleryFolder"),
                                            QDir::homePath()).toString();
            auto populate = [&]() {
                projectList->clear();
                if (!folder.isEmpty() && QDir(folder).exists()) {
                    QFileInfoList projects;
                    QDirIterator iter(folder, QStringList() << QStringLiteral("*.kra")
                                                            << QStringLiteral("*.krz"),
                                      QDir::Files, QDirIterator::Subdirectories);
                    while (iter.hasNext() && projects.size() < 2000) {
                        iter.next();
                        projects.append(iter.fileInfo());
                    }
                    std::sort(projects.begin(), projects.end(),
                              [](const QFileInfo &a, const QFileInfo &b) {
                                  return a.lastModified() > b.lastModified();
                              });
                    for (const QFileInfo &file : projects) {
                        QIcon thumbnail;
                        QScopedPointer<KoStore> store(KoStore::createStore(file.absoluteFilePath(), KoStore::Read));
                        if (store && !store->bad()) {
                            for (const QString &entry : {QStringLiteral("preview.png"),
                                                         QStringLiteral("Thumbnails/thumbnail.png")}) {
                                if (!store->hasFile(entry) || !store->open(entry)) continue;
                                QImage image;
                                image.loadFromData(store->read(store->size()), "PNG");
                                store->close();
                                if (!image.isNull()) {
                                    thumbnail = QIcon(QPixmap::fromImage(image.scaled(
                                        156, 156, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                                    break;
                                }
                            }
                        }
                        auto *item = new QListWidgetItem(thumbnail, file.completeBaseName(), projectList);
                        item->setData(Qt::UserRole, file.absoluteFilePath());
                    }
                }
                // Include Krita's recent documents, including content:// URIs,
                // which normal filesystem scanning cannot enumerate.
                QSet<QString> seen;
                for (int i = 0; i < projectList->count(); ++i) {
                    seen.insert(projectList->item(i)->data(Qt::UserRole).toString());
                }
                const QAbstractItemModel *recent = recentDocumentsListView->model();
                if (recent) {
                    for (int row = 0; row < recent->rowCount(); ++row) {
                        QModelIndex index = recent->index(row, 0);
                        QString uri = index.data(Qt::ToolTipRole).toString();
                        if (uri.isEmpty() || seen.contains(uri)) continue;
                        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
                        auto *item = new QListWidgetItem(icon, index.data(Qt::DisplayRole).toString(), projectList);
                        item->setData(Qt::UserRole, uri);
                        seen.insert(uri);
                    }
                }
            };
            connect(chooseFolder, &QPushButton::clicked, &gallery, [&]() {
                QString path = QFileDialog::getExistingDirectory(
                    &gallery, i18n("Choose a Krita Projects Folder"), folder);
                if (!path.isEmpty()) {
                    folder = path;
                    settings.setValue(QStringLiteral("CustomProjectGalleryFolder"), folder);
                    populate();
                }
            });
            connect(refresh, &QPushButton::clicked, &gallery, populate);
            connect(newCanvas, &QPushButton::clicked, &gallery, [&]() {
                gallery.accept();
                slotNewFileClicked();
            });
            connect(projectList, &QListWidget::itemClicked, &gallery,
                    [&](QListWidgetItem *item) {
                        if (m_mainWindow->openDocument(item->data(Qt::UserRole).toString(), KisMainWindow::None)) {
                            gallery.accept();
                        }
                    });
            populate();
            gallery.exec();
        });
#endif
''')
    for name, contents in changed.items():
        (root / name).write_text(contents, encoding='utf-8')
        print('Patched:', name)
    print('Patch completed. This does not build an APK.')

if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: python3 apply_customizations.py /path/to/Krita-v6.0.4')
    main(Path(sys.argv[1]).expanduser().resolve())
