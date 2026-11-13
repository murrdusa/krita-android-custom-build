// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <memory>
#include <QApplication>
#include <QEvent>
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
        if(!m_pixels.size()) {
            if(!m_image->tryBarrierLock(true)) {
                m_delay.start();
                return;
            }
            m_colorSpace=m_source->colorSpace();
            m_pixels.resize(m_window.width()*m_window.height()*m_source->pixelSize());
            m_source->readBytes(reinterpret_cast<quint8*>(m_pixels.data()),m_window);
            if(m_selection && (m_options.selectionBoundary || m_options.clipToSelection)) {
                m_selectionPixels.resize(m_window.width()*m_window.height());
                m_selection->readBytes(reinterpret_cast<quint8*>(m_selectionPixels.data()),m_window);
            }
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
        m_running=true;
        QThreadPool::globalInstance()->start([preview,pixels,selectionPixels,colorSpace,size,seed,options,threshold,generation,request]() {
            // With a busy global pool, rapid pen movement can supersede
            // this request before it even begins. Avoid spending a flood
            // fill on an obsolete tolerance; the GUI callback schedules
            // the latest value.
            if(generation->load(std::memory_order_relaxed)!=request) {
                QMetaObject::invokeMethod(qApp,[preview]() {
                    if(!preview)return;
                    preview->m_running=false;
                    preview->m_delay.start();
                },Qt::QueuedConnection);
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
            if(!mask) {
                QMetaObject::invokeMethod(qApp,[preview,threshold]() {
                    if(!preview)return;
                    preview->m_running=false;
                    if(threshold!=preview->m_threshold)preview->m_delay.start();
                },Qt::QueuedConnection);
                return;
            }
            QByteArray coverage(size.width()*size.height(),Qt::Uninitialized);
            mask->readBytes(reinterpret_cast<quint8*>(coverage.data()),rect);
            QImage overlay(size,QImage::Format_ARGB32_Premultiplied);
            for(int y=0;y<size.height();++y) {
                auto *line=reinterpret_cast<QRgb*>(overlay.scanLine(y));
                for(int x=0;x<size.width();++x) {
                    const int index=y*size.width()+x;
                    int opacity=quint8(coverage.at(index))*150/255;
                    if(options.clipToSelection && selected)
                        opacity=opacity*quint8(selectionPixels.at(index))/255;
                    line[x]=qPremultiply(qRgba(55,195,255,opacity));
                }
            }
            QMetaObject::invokeMethod(qApp,[preview,overlay,threshold]() {
                if(!preview)return;
                preview->m_running=false;
                if(threshold==preview->m_threshold) {
                    preview->m_mask=overlay;
                    preview->update();
                }
                if(threshold!=preview->m_threshold)preview->m_delay.start();
            },Qt::QueuedConnection);
        });
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
