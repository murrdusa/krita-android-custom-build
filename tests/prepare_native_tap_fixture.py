# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare a Qt fixture from the patched native matcher's actual methods.

Usage: python3 tests/prepare_native_tap_fixture.py KRITA_SOURCE OUTPUT_CPP
Compile OUTPUT_CPP with Qt5Widgets and run with QT_QPA_PLATFORM=offscreen.
Only the engine action endpoints are substituted; dispatcher code is extracted.
"""
import sys
from pathlib import Path
src=(Path(sys.argv[1])/'libs/ui/input/kis_shortcut_matcher.cpp').read_text()
def function(signature):
 a=src.index(signature);b=src.index('{',a);depth=1;i=b+1
 while depth:
  if src[i]=='{':depth+=1
  elif src[i]=='}':depth-=1
  i+=1
 return src[a:i]
fixture=r'''
#include <QApplication>
#include <QTouchEvent>
#include <QTouchDevice>
#include <QElapsedTimer>
#include <QTimer>
#include <QSettings>
#include <QScopedPointer>
#include <QEventLoop>
#include <functional>
#include <cassert>
struct Action {int ends=0; void activate(int){} void begin(int,QTouchEvent*){} void end(QTouchEvent*){++ends;} void deactivate(int){} };
struct KisTouchShortcut { Action *a; bool isDoubleTapType()const{return true;} bool isAvailable(int)const{return true;} int priority()const{return 1;} bool matchDoubleTapType(QTouchEvent *e)const{return e->touchPoints().size()==1;} bool matchTapType(QTouchEvent*)const{return false;} Action *action(){return a;} int shortcutIndex(){return 0;} };
struct KoPointerEvent { static void copyQtPointerEvent(QTouchEvent *e,QScopedPointer<QEvent>& copy){copy.reset(new QTouchEvent(*e));} };
class KisShortcutMatcher {
public:
 struct Private {
  QList<KisTouchShortcut*> touchShortcuts;
  bool isTouchHeld=false,isTouchDragDetected=false,touchTapConsumed=false,suppressed=false;
  QTimer pendingSingleTapTimer;
  QElapsedTimer lastOneFingerTapAge;
  QPointF lastOneFingerTapPosition;
  QScopedPointer<QEvent> pendingSingleTapEvent;
  std::function<void(QTouchEvent*)> touchTapFallback,pendingTouchTapFallback;
  int actionGroupMask(){return 0;} bool actionsSuppressedIgnoreFocus(){return suppressed;}
 };
 QScopedPointer<Private> m_d;
 KisShortcutMatcher();
 void fireReadyTouchShortcut(QTouchEvent*,bool bypassDoubleTap=false);
 bool hasOneFingerDoubleTapShortcut()const;
 void setTouchTapFallback(std::function<void(QTouchEvent*)>);
 void flushPendingTouchTap();
 KisTouchShortcut *matchTouchShortcut(QTouchEvent*){return nullptr;}
};
'''
for signature in ['KisShortcutMatcher::KisShortcutMatcher()', 'bool KisShortcutMatcher::hasOneFingerDoubleTapShortcut() const', 'void KisShortcutMatcher::setTouchTapFallback(', 'void KisShortcutMatcher::flushPendingTouchTap()', 'void KisShortcutMatcher::fireReadyTouchShortcut(']:
 fixture+='\n'+function(signature)+'\n'
fixture+=r'''
int main(int argc,char **argv){
 QApplication app(argc,argv);
 QCoreApplication::setOrganizationName("MobileTapFixture");QCoreApplication::setApplicationName("Test");
 QSettings().setValue("TabletUi/doubleTapMs",120);
 Action flip;KisTouchShortcut shortcut{&flip};KisShortcutMatcher matcher;
 matcher.m_d->touchShortcuts.append(&shortcut);
 int dots=0;matcher.setTouchTapFallback([&](QTouchEvent*){++dots;});
 QTouchDevice device;device.setType(QTouchDevice::TouchScreen);
 QTouchEvent::TouchPoint point(0);point.setPos(QPointF(20,20));point.setStartPos(point.pos());point.setState(Qt::TouchPointReleased);
 QTouchEvent tap(QEvent::TouchEnd,&device,Qt::NoModifier,Qt::TouchPointReleased,{point});
 assert(matcher.hasOneFingerDoubleTapShortcut());
 matcher.fireReadyTouchShortcut(&tap);assert(dots==0 && flip.ends==0 && matcher.m_d->touchTapConsumed);
 matcher.fireReadyTouchShortcut(&tap);assert(dots==0 && flip.ends==1 && !matcher.m_d->pendingSingleTapEvent);
 matcher.fireReadyTouchShortcut(&tap);
 QEventLoop loop;QTimer::singleShot(180,&loop,&QEventLoop::quit);loop.exec();
 assert(dots==1 && flip.ends==1);
 matcher.fireReadyTouchShortcut(&tap);matcher.flushPendingTouchTap();assert(dots==2);
 matcher.fireReadyTouchShortcut(&tap);assert(flip.ends==1); // a new stroke flushed the old tap
 point.setPos(QPointF(250,250));QTouchEvent farTap(QEvent::TouchEnd,&device,Qt::NoModifier,Qt::TouchPointReleased,{point});
 matcher.fireReadyTouchShortcut(&farTap);assert(dots==3 && flip.ends==1);
 matcher.flushPendingTouchTap();assert(dots==4 && flip.ends==1);
 matcher.m_d->suppressed=true;matcher.fireReadyTouchShortcut(&tap);assert(!matcher.m_d->pendingSingleTapEvent && dots==4);
 qInfo("PASS: native matcher double taps flip without paint dots, single taps paint, distant taps and drag flush stay separate");
}
'''
Path(sys.argv[2]).write_text(fixture)
