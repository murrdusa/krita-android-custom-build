#!/usr/bin/env python3
"""Touch-scrollable post-processing for Krita's complete Tablet Controls dialog.

Runs after refinements/polish so all settings rows are already present.
"""
from pathlib import Path
import sys

def replace_one(s,old,new,label):
    n=s.count(old)
    if n!=1:raise RuntimeError(f"Tablet scroll {label}: expected one anchor, found {n}")
    return s.replace(old,new,1)

def main(root):
    path=Path(root)/"libs/ui/KisMainWindow.cpp"
    s=path.read_text(encoding="utf-8")
    s=replace_one(s,
'''#include <QDialogButtonBox>
#include <QCheckBox>''',
'''#include <QDialogButtonBox>
#include <QScrollArea>
#include <QScroller>
#include <QCheckBox>''',"headers")
    s=replace_one(s,
'''        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(14,14,14,14);
        layout->setSpacing(10);
        QFormLayout *form = new QFormLayout;''',
'''        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(12,12,12,12);
        layout->setSpacing(8);
        // Scroll the full completed form, but leave OK/Cancel stationary.
        QScrollArea *settingsScroll = new QScrollArea(&dialog);
        settingsScroll->setWidgetResizable(true);
        settingsScroll->setFrameShape(QFrame::NoFrame);
        settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        settingsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        settingsScroll->viewport()->setAttribute(
            Qt::WA_AcceptTouchEvents,true);
        QScroller::grabGesture(settingsScroll->viewport(),
                               QScroller::TouchGesture);
        QWidget *settingsPage = new QWidget(settingsScroll);
        QVBoxLayout *settingsContent = new QVBoxLayout(settingsPage);
        settingsContent->setContentsMargins(3,4,3,4);
        settingsContent->setSpacing(7);
        QFormLayout *form = new QFormLayout;''',"scroll scaffold")
    s=replace_one(s,
'''        layout->addLayout(form);
        QDialogButtonBox *buttons = new QDialogButtonBox(''',
'''        settingsContent->addLayout(form);
        settingsContent->addStretch(1);
        settingsScroll->setWidget(settingsPage);
        layout->addWidget(settingsScroll,1);
        QDialogButtonBox *buttons = new QDialogButtonBox(''',"form into scroll")
    s=replace_one(s,
'''        dialog.resize(qMax(300,qMin(440,this->width()-30)),
                      qMax(360,qMin(640,this->height()-40)));''',
'''        dialog.resize(qMax(295,qMin(445,this->width()-24)),
                      qMax(330,qMin(675,this->height()-35)));''',"mobile dialog bounds")
    path.write_text(s,encoding="utf-8")
    print("Tablet Controls form scrollable; OK/Cancel remain visible")

if __name__=="__main__":
    if len(sys.argv)!=2:raise SystemExit("Usage: apply_mobile_touch_scroll.py KRITA_SOURCE")
    main(sys.argv[1])
