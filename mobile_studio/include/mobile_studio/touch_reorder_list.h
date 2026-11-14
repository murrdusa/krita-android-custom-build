/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Long-press reorder list for touch and stylus in Mobile Studio editors.
 * This widget edits only the ordering of UI shortcut IDs. It never sends
 * painting events or modifies Krita's native document/layer model.
 */
#pragma once
#ifdef Q_OS_ANDROID
#include <QApplication>
#include <QListWidget>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimer>
#include <QtMath>

class MobileHoldReorderList final : public QListWidget
{
public:
    explicit MobileHoldReorderList(QWidget *parent=nullptr)
        : QListWidget(parent)
    {
        setSelectionMode(QAbstractItemView::SingleSelection);
        setDragEnabled(true);
        setAcceptDrops(true);
        viewport()->setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::InternalMove);
        setDragDropOverwriteMode(false);
        setDefaultDropAction(Qt::MoveAction);
        setAutoScroll(true);
        setAutoScrollMargin(30);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // Touch events synthesize mouse events on Android when raw touch
        // handling is disabled. Stylus input remains handled by Qt.
        viewport()->setAttribute(Qt::WA_AcceptTouchEvents,false);
        m_hold.setSingleShot(true);
        m_hold.setInterval(360);
        QObject::connect(&m_hold,&QTimer::timeout,this,[this]() {
            if(m_pressed && !m_scrolling) {
                m_canDrag=true;
                viewport()->setCursor(Qt::ClosedHandCursor);
            }
        });
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        resetPointer();
        if(event->button()!=Qt::LeftButton) {
            QListWidget::mousePressEvent(event);
            return;
        }
        m_pressed=true;
        m_start=event->pos();
        m_scrollAtPress=verticalScrollBar()->value();
        // Let QAbstractItemView set its internal pressed index. Only move
        // events are gated until the hold timer has elapsed.
        QListWidget::mousePressEvent(event);
        if(itemAt(event->pos()))m_hold.start();
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if(!m_pressed || !(event->buttons() & Qt::LeftButton)) {
            QListWidget::mouseMoveEvent(event);
            return;
        }
        if(m_canDrag) {
            // Native QAbstractItemView::startDrag handles drop indicators
            // and border-triggered auto-scroll after a deliberate hold.
            QListWidget::mouseMoveEvent(event);
            return;
        }
        const int distance=(event->pos()-m_start).manhattanLength();
        if(m_scrolling || distance>QApplication::startDragDistance()) {
            if(!m_scrolling) {
                m_scrolling=true;
                m_hold.stop();
            }
            // Swiping before the hold is a scroll, never a reorder.
            verticalScrollBar()->setValue(
                m_scrollAtPress+m_start.y()-event->pos().y());
            event->accept();
            return;
        }
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if(m_scrolling) {
            event->accept();
            resetPointer();
            return;
        }
        QListWidget::mouseReleaseEvent(event);
        resetPointer();
    }
    void leaveEvent(QEvent *event) override
    {
        // Don't cancel a drag once the cursor leaves while dragging:
        // native Qt auto-scroll relies on reaching the viewport border.
        QListWidget::leaveEvent(event);
    }
    void focusOutEvent(QFocusEvent *event) override
    {
        resetPointer();
        QListWidget::focusOutEvent(event);
    }
    void dropEvent(QDropEvent *event) override
    {
        QListWidget::dropEvent(event);
        resetPointer();
    }

private:
    void resetPointer()
    {
        m_hold.stop();
        m_pressed=false;
        m_canDrag=false;
        m_scrolling=false;
        viewport()->unsetCursor();
    }
    QTimer m_hold;
    QPoint m_start;
    int m_scrollAtPress=0;
    bool m_pressed=false;
    bool m_canDrag=false;
    bool m_scrolling=false;
};
#endif // Q_OS_ANDROID
