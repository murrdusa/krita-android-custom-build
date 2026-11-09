/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Future Android application root: one Qt top-level, no KisMainWindow,
 * QMdiArea, Krita desktop dockers or canvas implementation. Krita's
 * independently constructed view manager owns the painting engine.
 *
 * A top-level Android Back/Close must consult the native document host
 * so Save/Discard/Cancel remains effective.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QCloseEvent>
#include <QMainWindow>
#include <QPointer>
#include <QWidget>

class MobileEngineOnlyRoot final : public QMainWindow
{
public:
    explicit MobileEngineOnlyRoot(QWidget *parent=nullptr)
        : QMainWindow(parent)
    {
        setObjectName(QStringLiteral("_mobileEngineOnlyRoot"));
        setProperty("_mobileNativeEngineOnlyRoot",true);
    }

    void setDrawingHost(QWidget *drawingHost)
    {
        if(drawingHost && drawingHost->parentWidget()==this)
            m_drawingHost=drawingHost;
    }

    bool hasDrawingHost() const {return !m_drawingHost.isNull();}

protected:
    void closeEvent(QCloseEvent *event) override
    {
        // Child QWidget::close() returns false when its Krita-backed
        // Save/Discard/Cancel dialog rejects closing. Do not bypass it.
        if(m_drawingHost && !m_drawingHost->close()) {
            event->ignore();
            return;
        }
        m_drawingHost.clear();
        QMainWindow::closeEvent(event);
    }

private:
    QPointer<QWidget> m_drawingHost;
};
#endif
