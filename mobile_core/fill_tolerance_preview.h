// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <memory>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QHideEvent>
#include <QShowEvent>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>
#include <KoColor.h>
#include <klocalizedstring.h>
#include <canvas/kis_canvas2.h>
#include <canvas/kis_coordinates_converter.h>
#include <kis_fill_painter.h>
#include <kis_image.h>
#include <kis_paint_device.h>
#include <kis_pixel_selection.h>

#include "fill_tolerance_drag.h"

class MobileFillTolerancePreview final : public QWidget
{
public:
    struct Options {
        int spread=100;
        int closeGap=0;
        bool boundaryFill=false;
        KoColor boundaryColor;
        bool selectionBoundary=false;
        bool clipToSelection=false;
        bool antiAlias=false;
        int grow=0;
        int feather=0;
        bool stopGrowingAtDarkest=false;
    };

    MobileFillTolerancePreview(KisCanvas2 *canvas,const KisImageSP &image,
                               const KisPaintDeviceSP &source,const KisPixelSelectionSP &selection,
                               const QPoint &seed,const Options &options)
        : QWidget(canvas->canvasWidget()),m_canvas(canvas),m_image(image),m_source(source),
          m_selection(selection),m_seed(seed),m_options(options),
          m_window(mobileFillPreviewWindow(image->bounds(),seed))
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setGeometry(parentWidget()->rect());
        // The drawing view can resize while the stylus is down (fold,
        // split screen, panel changes). Keep the transparent preview
        // aligned with the live canvas instead of retaining its old size.
        parentWidget()->installEventFilter(this);
        m_delay.setSingleShot(true);
        m_delay.setInterval(55);
        connect(&m_delay,&QTimer::timeout,this,[this]() {requestMask();});
        raise();
        show();
    }

    ~MobileFillTolerancePreview() override
    {
        // The worker owns this generation separately from the widget.
        // Stop queued work when Fill/Select ends or switches tools.
        m_requestGeneration->fetch_add(1,std::memory_order_relaxed);
    }

    void setThreshold(int value)
    {
        if(m_threshold==value)return;
        m_threshold=value;
        m_requestGeneration->fetch_add(1,std::memory_order_relaxed);
        // A previous worker's region belongs to a different tolerance.
        // Hide that mask rather than falsely presenting it as the new one.
        m_mask=QImage();
        update();
        if(m_source && !m_window.isEmpty() && !m_running)
            m_delay.start();
    }

protected:
    void hideEvent(QHideEvent *event) override
    {
        // A hidden Android canvas does not need the sampled pixel window,
        // selection copy or rendered tolerance overlay. They can consume
        // substantial RAM while the user works in another document.
        m_delay.stop();
        m_requestGeneration->fetch_add(1,std::memory_order_relaxed);
        m_pixels.clear();
        m_selectionPixels.clear();
        m_colorSpace=nullptr;
        m_mask=QImage();
        QWidget::hideEvent(event);
    }

    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        // The hidden generation was invalidated. A still-running worker
        // schedules this retry when it completes; otherwise start now.
        if(m_threshold>=0 && !m_running)m_delay.start();
    }

    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==parentWidget() && event->type()==QEvent::Resize) {
            setGeometry(parentWidget()->rect());
            update();
        }
        return QWidget::eventFilter(watched,event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if(m_canvas && !m_mask.isNull()) {
            painter.save();
            painter.setWorldTransform(m_canvas->coordinatesConverter()->imageToWidgetTransform());
            painter.drawImage(m_window.topLeft(),m_mask);
            painter.restore();
        }
        if(m_threshold<0)return;
        const QString text=i18n("Tolerance: %1",m_threshold);
        const QRect badge((width()-140)/2,16,140,34);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(37,37,41,225));
        painter.drawRoundedRect(badge,7,7);
        painter.setPen(Qt::white);
        painter.drawText(badge,Qt::AlignCenter,text);
    }

private:
    void requestMask()
    {
        // The preview may outlive the current engine selection while
        // a tool or canvas is being changed. Never sample an empty
        // window or an unavailable paint source.
        if(!isVisible() || !m_image || !m_source || m_window.isEmpty() || m_threshold<0)
            return;
        if(!m_pixels.size()) {
            const int bytes=mobileFillPreviewBufferBytes(
                m_window.size(),m_source->pixelSize());
            const bool needsSelection=m_selection &&
                (m_options.selectionBoundary || m_options.clipToSelection);
            const int selectionBytes=needsSelection?
                mobileFillPreviewSelectionBytes(
                    m_window.size(),m_selection->pixelSize()):0;
            // An unexpected selection pixel stride cannot be written
            // into the one-byte-per-pixel coverage buffer safely.
            if(!bytes || (needsSelection && !selectionBytes) ||
               !m_source->colorSpace())return;
            if(!m_image->tryBarrierLock(true)) {
                m_delay.start();
                return;
            }
            m_colorSpace=m_source->colorSpace();
            m_pixels.resize(bytes);
            if(m_pixels.size()!=bytes) {
                // A failed resize must never be passed to readBytes().
                m_pixels.clear();
                m_image->unlock();
                return;
            }
            if(needsSelection) {
                m_selectionPixels.resize(selectionBytes);
                if(m_selectionPixels.size()!=selectionBytes) {
                    m_pixels.clear();
                    m_selectionPixels.clear();
                    m_image->unlock();
                    return;
                }
            }
            m_source->readBytes(reinterpret_cast<quint8*>(m_pixels.data()),m_window);
            if(needsSelection)
                m_selection->readBytes(reinterpret_cast<quint8*>(m_selectionPixels.data()),m_window);
            m_image->unlock();
        }
        const auto pixels=m_pixels;
        const auto selectionPixels=m_selectionPixels;
        const auto colorSpace=m_colorSpace;
        const auto size=m_window.size();
        const auto seed=m_seed-m_window.topLeft();
        const auto options=m_options;
        const auto threshold=m_threshold;
        const auto generation=m_requestGeneration;
        const int request=generation->load(std::memory_order_relaxed);
        const QPointer<MobileFillTolerancePreview> preview(this);
        // Worker completion may race Android process teardown. Do not
        // dereference qApp from a worker after the QObject has died.
        const QPointer<QCoreApplication> application(QCoreApplication::instance());
        m_running=true;
        // Do not enqueue a full pixel snapshot when the global pool is
        // saturated by gallery scans or other expensive Krita work.
        // A queued closure would retain its sampled buffers even after
        // a tool switch; retry from the live widget when a slot frees.
        const bool started=QThreadPool::globalInstance()->tryStart(
            [preview,application,pixels,selectionPixels,colorSpace,size,seed,options,threshold,generation,request]() {
            // With a busy global pool, rapid pen movement can supersede
            // this request before it even begins. Avoid spending a flood
            // fill on an obsolete tolerance; the GUI callback schedules
            // the latest value.
            const auto notifyStale=[preview,application]() {
                if(!application)return;
                QMetaObject::invokeMethod(application.data(),[preview]() {
                    if(!preview)return;
                    preview->m_running=false;
                    if(preview->isVisible())preview->m_delay.start();
                },Qt::QueuedConnection);
            };
            if(generation->load(std::memory_order_relaxed)!=request) {
                notifyStale();
                return;
            }
            const QRect rect(QPoint(),size);
            KisPaintDeviceSP source=new KisPaintDevice(colorSpace);
            source->writeBytes(reinterpret_cast<const quint8*>(pixels.constData()),rect);
            KisPixelSelectionSP selected;
            if(!selectionPixels.isEmpty()) {
                selected=new KisPixelSelection();
                selected->writeBytes(reinterpret_cast<const quint8*>(selectionPixels.constData()),rect);
            }
            if(generation->load(std::memory_order_relaxed)!=request) {
                notifyStale();
                return;
            }
            KisFillPainter fill(source);
            fill.setWidth(size.width());
            fill.setHeight(size.height());
            fill.setFillThreshold(threshold);
            fill.setUseCompositing(true);
            fill.setOpacitySpread(options.spread);
            fill.setCloseGap(options.closeGap);
            fill.setAntiAlias(options.antiAlias);
            fill.setSizemod(options.grow);
            fill.setFeather(options.feather);
            fill.setStopGrowingAtDarkestPixel(options.stopGrowingAtDarkest);
            fill.setUseSelectionAsBoundary(options.selectionBoundary && selected &&
                selected->pixel(seed).opacityU8()!=0);
            if(options.boundaryFill) {
                fill.setRegionFillingMode(KisFillPainter::RegionFillingMode_BoundaryFill);
                fill.setRegionFillingBoundaryColor(options.boundaryColor);
            }
            const auto mask=fill.createFloodSelection(seed.x(),seed.y(),source,selected);
            if(generation->load(std::memory_order_relaxed)!=request) {
                notifyStale();
                return;
            }
            // The generated flood mask is also read as one alpha byte
            // per pixel. Guard its actual stride independently of the
            // input selection; otherwise a changed engine mask format
            // could overwrite the compact coverage allocation.
            if(!mask || !mobileFillPreviewSelectionBytes(
                              size,mask->pixelSize())) {
                if(!application)return;
                QMetaObject::invokeMethod(application.data(),[preview,threshold,generation,request]() {
                    if(!preview)return;
                    preview->m_running=false;
                    // The pen may move away from a value and back while
                    // this result is queued. Equal tolerance alone does
                    // not mean this worker still owns the latest request.
                    if(!mobileFillPreviewResultCurrent(threshold,preview->m_threshold,
                                                      request,generation->load(std::memory_order_relaxed)))
                        if(preview->isVisible())preview->m_delay.start();
                },Qt::QueuedConnection);
                return;
            }
            const int coverageBytes=mobileFillPreviewSelectionBytes(size,mask->pixelSize());
            QByteArray coverage(coverageBytes,Qt::Uninitialized);
            if(coverage.size()!=coverageBytes) {
                if(application) {
                    QMetaObject::invokeMethod(application.data(),
                        [preview,threshold,generation,request]() {
                            if(!preview)return;
                            preview->m_running=false;
                            if(!mobileFillPreviewResultCurrent(
                                   threshold,preview->m_threshold,request,
                                   generation->load(std::memory_order_relaxed)))
                                if(preview->isVisible())preview->m_delay.start();
                        },Qt::QueuedConnection);
                }
                return;
            }
            mask->readBytes(reinterpret_cast<quint8*>(coverage.data()),rect);
            QImage overlay(size,QImage::Format_ARGB32_Premultiplied);
            // Under Android memory pressure a QImage allocation can
            // fail without throwing. Never dereference a null scanLine.
            // Unlike a superseded request, retrying the same failed
            // allocation immediately would create a busy failure loop.
            if(overlay.isNull()) {
                if(application) {
                    QMetaObject::invokeMethod(application.data(),
                        [preview,threshold,generation,request]() {
                            if(!preview)return;
                            preview->m_running=false;
                            // Only retry if the user has since selected a
                            // different request; the failed allocation
                            // itself must not spin indefinitely.
                            if(!mobileFillPreviewResultCurrent(
                                   threshold,preview->m_threshold,request,
                                   generation->load(std::memory_order_relaxed)))
                                if(preview->isVisible())preview->m_delay.start();
                        },Qt::QueuedConnection);
                }
                return;
            }
            for(int y=0;y<size.height();++y) {
                if(generation->load(std::memory_order_relaxed)!=request) {
                    notifyStale();
                    return;
                }
                auto *line=reinterpret_cast<QRgb*>(overlay.scanLine(y));
                for(int x=0;x<size.width();++x) {
                    const int index=y*size.width()+x;
                    int opacity=quint8(coverage.at(index))*150/255;
                    if(options.clipToSelection && selected)
                        opacity=opacity*quint8(selectionPixels.at(index))/255;
                    line[x]=qPremultiply(qRgba(55,195,255,opacity));
                }
            }
            if(!application)return;
            QMetaObject::invokeMethod(application.data(),[preview,overlay,threshold,generation,request]() {
                if(!preview)return;
                preview->m_running=false;
                if(mobileFillPreviewResultCurrent(threshold,preview->m_threshold,
                                                 request,generation->load(std::memory_order_relaxed))) {
                    preview->m_mask=overlay;
                    preview->update();
                } else {
                    preview->m_delay.start();
                }
            },Qt::QueuedConnection);
        });
        if(!started) {
            // tryStart() never queues a refused task, and its captured
            // pixel arrays are released on return. Keep the latest
            // threshold pending without pinning another worker closure.
            m_running=false;
            m_delay.start();
        }
    }

    QPointer<KisCanvas2> m_canvas;
    KisImageSP m_image;
    KisPaintDeviceSP m_source;
    KisPixelSelectionSP m_selection;
    QPoint m_seed;
    Options m_options;
    QRect m_window;
    QByteArray m_pixels,m_selectionPixels;
    const KoColorSpace *m_colorSpace=nullptr;
    QImage m_mask;
    QTimer m_delay;
    std::shared_ptr<std::atomic<int>> m_requestGeneration=std::make_shared<std::atomic<int>>(0);
    int m_threshold=-1;
    bool m_running=false;
};
