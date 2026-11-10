/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QComboBox>
#include <QScroller>

namespace MobileTouchScroll
{
inline void configure(QAbstractScrollArea *area)
{
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *viewport=area->viewport();
    viewport->setAttribute(Qt::WA_AcceptTouchEvents,true);
    QScroller::ungrabGesture(viewport);
    QScroller::grabGesture(viewport,QScroller::TouchGesture);
}

inline void configureList(QAbstractItemView *view)
{
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    configure(view);
}

inline void configureCombo(QComboBox *combo)
{
    // Menu-style combo popups ignore maxVisibleItems and use arrow scrollers.
    combo->setStyleSheet(QStringLiteral("QComboBox{combobox-popup:0;}"));
    combo->setMaxVisibleItems(6);
    configureList(combo->view());
}
}
