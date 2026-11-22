"""Keep native double-tap bindings usable alongside single-finger painting."""
from pathlib import Path

def apply(root: Path):
    def edit(name, old, new):
        p=root/name
        s=p.read_text(encoding='utf-8')
        if s.count(old)!=1:
            raise RuntimeError(f'Finger-tap patch: expected one anchor in {name}: {old[:70]}')
        p.write_text(s.replace(old,new,1),encoding='utf-8')
    edit('libs/ui/input/kis_input_manager_p.cpp',
         'shortcut->setDisableOnTouchPainting(true);',
         'shortcut->setDisableOnTouchPainting(gesture != KisShortcutConfiguration::OneFingerDoubleTap);')
    edit('libs/ui/input/kis_shortcut_matcher.h',
         '    bool touchBeginEvent(QTouchEvent *event);',
         '''    bool touchBeginEvent(QTouchEvent *event);
    bool hasOneFingerDoubleTapShortcut() const;
    void setTouchTapFallback(std::function<void(QTouchEvent *)> fallback);
    void flushPendingTouchTap();''')
    edit('libs/ui/input/kis_shortcut_matcher.cpp',
         '    QScopedPointer<QEvent> pendingSingleTapEvent;',
         '''    QScopedPointer<QEvent> pendingSingleTapEvent;
    std::function<void(QTouchEvent *)> touchTapFallback;
    std::function<void(QTouchEvent *)> pendingTouchTapFallback;
    bool touchTapConsumed=false;''')
    edit('libs/ui/input/kis_shortcut_matcher.cpp',
         'bool KisShortcutMatcher::touchBeginEvent( QTouchEvent* event )',
         '''bool KisShortcutMatcher::hasOneFingerDoubleTapShortcut() const
{
    if (m_d->actionsSuppressedIgnoreFocus()) return false;
    for (const auto *shortcut : m_d->touchShortcuts)
        if (shortcut->isDoubleTapType() && shortcut->isAvailable(m_d->actionGroupMask())) return true;
    return false;
}

void KisShortcutMatcher::setTouchTapFallback(std::function<void(QTouchEvent *)> fallback)
{
    m_d->touchTapFallback=std::move(fallback);
}

void KisShortcutMatcher::flushPendingTouchTap()
{
    m_d->pendingSingleTapTimer.stop();
    m_d->lastOneFingerTapAge.invalidate();
    QScopedPointer<QEvent> event(m_d->pendingSingleTapEvent.take());
    if (event) fireReadyTouchShortcut(static_cast<QTouchEvent *>(event.data()),true);
    m_d->pendingTouchTapFallback={};
}

bool KisShortcutMatcher::touchBeginEvent( QTouchEvent* event )''')
    edit('libs/ui/input/kis_shortcut_matcher.cpp',
         '    m_d->lastTouchPoints = event->touchPoints();',
         '    m_d->touchTapConsumed=false;\n    m_d->lastTouchPoints = event->touchPoints();')
    edit('libs/ui/input/kis_shortcut_matcher.cpp',
         '    if (doubleTap) {',
         '    if (doubleTap) {\n        m_d->touchTapConsumed=true;')
    edit('libs/ui/input/kis_shortcut_matcher.cpp',
         '        m_d->lastOneFingerTapPosition = point;',
         '        m_d->pendingTouchTapFallback=m_d->touchTapFallback;\n        m_d->lastOneFingerTapPosition = point;')
    edit('libs/ui/input/kis_shortcut_matcher.cpp',
         '''        candidate->action()->deactivate(candidate->shortcutIndex());
    }
}''',
         '''        candidate->action()->deactivate(candidate->shortcutIndex());
    } else if (bypassDoubleTap && !m_d->actionsSuppressedIgnoreFocus()) {
        const auto fallback=m_d->pendingTouchTapFallback;
        m_d->pendingTouchTapFallback={};
        if (fallback) fallback(event);
    }
}''')
    p=root/'libs/ui/input/kis_shortcut_matcher.cpp'
    s=p.read_text();start=s.index('bool KisShortcutMatcher::touchEndEvent(');end=s.index('void KisShortcutMatcher::touchCancelEvent(',start)
    part=s[start:end];assert '    return retval;' in part
    s=s[:start]+part.replace('    return retval;','    return retval || m_d->touchTapConsumed;')+s[end:]
    # Drop captured view references when the shortcut profile is cleared.
    start=s.index('void KisShortcutMatcher::clearShortcuts()')
    s=s[:start]+s[start:].replace('    m_d->pendingSingleTapTimer.stop();','    m_d->pendingTouchTapFallback={};\n    m_d->touchTapFallback={};\n    m_d->pendingSingleTapTimer.stop();',1)
    p.write_text(s)
    edit('libs/ui/input/kis_input_manager.cpp',
         '''bool KisInputManager::handleTouchBegin(QTouchEvent *touchEvent)
{
    return d->matcher.touchBeginEvent(touchEvent);''',
         '''bool KisInputManager::handleTouchBegin(QTouchEvent *touchEvent)
{
#ifdef Q_OS_ANDROID
    // The matcher delays a painting dot only while a second tap could
    // turn it into a gesture. Keep the original canvas identity: a late
    // single tap must never paint into a different document.
    const QPointer<KisInputManager> self(this);
    const QPointer<KisCanvas2> target(canvas());
    const QString toolId=KoToolManager::instance()->activeToolId();
    d->matcher.setTouchTapFallback([self,target,toolId](QTouchEvent *tap) {
        if (!self || !target || self->canvas()!=target || !target->canvasWidget() ||
            !target->canvasWidget()->isVisible() || KoToolManager::instance()->activeToolId()!=toolId ||
            KisConfig(true).disableTouchOnCanvas() || self->d->touchStrokeStarted) return;
        self->d->matcher.buttonPressed(Qt::LeftButton,tap);
        if (self && target && self->canvas()==target) {
            auto points=tap->touchPoints();
            for (auto &point : points) {
                point.setState(Qt::TouchPointReleased);
                point.setPressure(0);
            }
            QTouchEvent release(QEvent::TouchEnd,tap->device(),tap->modifiers(),Qt::TouchPointReleased,points);
            release.setTimestamp(tap->timestamp());
            self->d->matcher.buttonReleased(Qt::LeftButton,&release);
        }
    });
#endif
    return d->matcher.touchBeginEvent(touchEvent);''')
    edit('libs/ui/input/kis_input_manager.cpp',
         '''    QPointF currentPos = touchEvent->touchPoints().at(0).pos();
    if (d->touchStrokeStarted''',
         '''    QPointF currentPos = touchEvent->touchPoints().at(0).pos();
    qreal strokeThreshold=1;
#ifdef Q_OS_ANDROID
    if (d->matcher.hasOneFingerDoubleTapShortcut())
        strokeThreshold=qMax(1,QApplication::startDragDistance());
#endif
    if (d->touchStrokeStarted''')
    edit('libs/ui/input/kis_input_manager.cpp',
         '''qAbs(currentPos.x() - d->previousPos.x()) > 1 // stop wobbliness which Qt sends us
                || qAbs(currentPos.y() - d->previousPos.y()) > 1''',
         '''qAbs(currentPos.x() - d->previousPos.x()) > strokeThreshold
                || qAbs(currentPos.y() - d->previousPos.y()) > strokeThreshold''')
    edit('libs/ui/input/kis_input_manager.cpp',
         '''            bool retval = d->matcher.buttonPressed(Qt::LeftButton, d->originatingTouchBeginEvent.data());''',
         '''#ifdef Q_OS_ANDROID
            d->matcher.flushPendingTouchTap();
#endif
            bool retval = d->matcher.buttonPressed(Qt::LeftButton, d->originatingTouchBeginEvent.data());''')
    edit('libs/ui/input/kis_input_manager.cpp',
         '''        } else if (!d->touchStrokeBlocked
                   && !KisConfig(true).disableTouchOnCanvas()''',
         '''        } else if (!retval && !d->touchStrokeBlocked
                   && !KisConfig(true).disableTouchOnCanvas()''')
