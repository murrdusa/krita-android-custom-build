/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QTouchEvent>
#include <functional>

class MobileCanvasLayoutEditor final : public QWidget
{
public:
    struct Target {
        QPointer<QWidget> widget;
        QSize minimum;
        bool rotatable=false;
        std::function<void(const QRect &,bool)> save;
        QString rotateLabel;
        Qt::Orientations thicknessAxis={};
    };

    MobileCanvasLayoutEditor(QWidget *canvas,const QVector<Target> &targets,
                             std::function<void()> finished,
                             std::function<void()> workspaceOptions={})
        : QWidget(canvas),m_finished(std::move(finished))
    {
        setObjectName(QStringLiteral("_mobileCanvasLayoutEditor"));
        setAttribute(Qt::WA_AcceptTouchEvents);
        setFocusPolicy(Qt::StrongFocus);
        setStyleSheet(QStringLiteral(
            "QPushButton{background:#252529;color:#ffffff;border:1px solid #a0c1d2;"
            "border-radius:8px;padding:4px;}"
            "QPushButton:pressed{background:#365c78;}"));
        m_done=new QPushButton(tr("Done"),this);
        m_cancel=new QPushButton(tr("Cancel"),this);
        m_rotate=new QPushButton(tr("Rotate toolbar"),this);
        connect(m_done,&QPushButton::clicked,this,[this]() {closeEditor(true);});
        connect(m_cancel,&QPushButton::clicked,this,[this]() {closeEditor(false);});
        connect(m_rotate,&QPushButton::clicked,this,[this]() {
            if(m_selected<0)return;
            auto &item=m_items[m_selected];
            const QRect before=item.rect;
            item.rect.setSize(fit(QSize(before.height(),before.width()),bounds().size()));
            item.rect.moveCenter(before.center());
            item.rect=clamp(item.rect,bounds());
            item.rotated=!item.rotated;
            update();
        });
        if(workspaceOptions) {
            m_workspaces=new QPushButton(tr("Layouts…"),this);
            connect(m_workspaces,&QPushButton::clicked,this,std::move(workspaceOptions));
        }
        m_rotate->hide();
        setGeometry(canvas->rect());
        for(const auto &target:targets) {
            if(!target.widget || !target.widget->isVisible())continue;
            const QRect rect=target.widget->geometry();
            if(!rect.isValid())continue;
            m_items.append({target,rect,rect,target.widget->grab(),false});
        }
        for(const auto &item:m_items)item.target.widget->hide();

        show();
        raise();
        setFocus();
    }

    static QRectF relative(const QRect &rect,const QRect &bounds)
    {
        const qreal scale=qMax(1,qMin(bounds.width(),bounds.height()));
        const auto pos=rect.topLeft()-bounds.topLeft();
        return QRectF(qBound(0.0,pos.x()/qreal(qMax(1,bounds.width()-rect.width())),1.0),
                      qBound(0.0,pos.y()/qreal(qMax(1,bounds.height()-rect.height())),1.0),
                      rect.width()/scale,rect.height()/scale);
    }

    static QRect absolute(const QRectF &placement,const QRect &bounds)
    {
        const int scale=qMin(bounds.width(),bounds.height());
        const QSize size=fit(QSize(qRound(placement.width()*scale),
                                   qRound(placement.height()*scale)),bounds.size());
        return QRect(bounds.x()+qRound(placement.x()*(bounds.width()-size.width())),
                     bounds.y()+qRound(placement.y()*(bounds.height()-size.height())),
                     size.width(),size.height());
    }

    void closeEditor(bool save)
    {
        if(m_closing)return;
        m_closing=true;
        hide();
        for(auto &item:m_items) {
            if(!item.target.widget)continue;
            item.target.widget->show();
            if(save && (item.rotated || item.rect!=item.original))
                item.target.save(item.rect,item.rotated);
        }
        if(m_finished)m_finished();
        deleteLater();
    }

protected:
    bool event(QEvent *event) override
    {
        switch(event->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            auto *touch=static_cast<QTouchEvent*>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
            const auto &points=touch->points();
            const auto point=points.isEmpty()?m_pointer:points.first().position().toPoint();
#else
            const auto &points=touch->touchPoints();
            const auto point=points.isEmpty()?m_pointer:points.first().pos().toPoint();
#endif
            if(event->type()==QEvent::TouchBegin && points.size()==1)begin(point);
            else if(event->type()==QEvent::TouchUpdate && points.size()==1)move(point);
            else if(event->type()==QEvent::TouchEnd) {move(point);finish();}
            else {
                if(m_active)m_items[m_selected].rect=m_initial;
                finish(false);
                update();
            }
            event->accept();
            return true;
        }
        case QEvent::Wheel:
        case QEvent::ContextMenu:
        case QEvent::ShortcutOverride:
            event->accept();
            return true;
        default:
            return QWidget::event(event);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if(event->button()==Qt::LeftButton)begin(event->pos());
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override { move(event->pos()); }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if(event->button()==Qt::LeftButton) { move(event->pos()); finish(); }
    }

    void tabletEvent(QTabletEvent *event) override
    {
        if(event->type()==QEvent::TabletPress)begin(event->pos());
        else if(event->type()==QEvent::TabletMove)move(event->pos());
        else {move(event->pos());finish();}
        event->accept();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if(event->key()==Qt::Key_Escape || event->key()==Qt::Key_Back)closeEditor(false);
        else event->accept();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        if(event->oldSize().isValid() && event->oldSize()!=event->size()) {
            const QRect old=QRect(QPoint(),event->oldSize()).adjusted(2,2,-2,-2);
            if(old.isValid()) {
                for(auto &item:m_items)
                    item.rect=absolute(relative(item.rect,old),bounds());
            }
            finish(false);
        }
        const int x=qMax(2,width()-190);
        const int y=qMax(2,height()-54);
        m_cancel->setGeometry(x,y,86,46);
        m_done->setGeometry(x+92,y,90,46);
        if(m_workspaces)m_workspaces->setGeometry(4,4,90,44);
        m_rotate->setGeometry(x>=152?x-150:x,x>=152?y:qMax(2,y-50),144,46);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.fillRect(rect(),QColor(12,16,22,100));
        for(int i=0;i<m_items.size();++i) {
            const auto &item=m_items.at(i);
            const bool selected=i==m_selected;
            if(item.rotated) {
                painter.save();
                painter.translate(item.rect.x()+item.rect.width(),item.rect.y());
                painter.rotate(90);
                painter.drawPixmap(QRect(0,0,item.rect.height(),item.rect.width()),item.image);
                painter.restore();
            } else {
                painter.drawPixmap(item.rect,item.image);
            }
            painter.setPen(QPen(selected?QColor("#8ae5ff"):QColor("#91aab8"),selected?2:1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(item.rect.adjusted(0,0,-1,-1));
            if(selected) {
                for(const auto &corner:corners(item.rect))
                    painter.fillRect(QRect(corner-QPoint(6,6),QSize(13,13)),QColor("#8ae5ff"));
            }
        }
    }

private:
    struct Item {
        Target target;
        QRect original,rect;
        QPixmap image;
        bool rotated;
    };

    QRect bounds() const {return rect().adjusted(2,2,-2,-2);}
    static QSize fit(const QSize &size,const QSize &limit)
    {
        return size.width()>limit.width() || size.height()>limit.height()?
            size.scaled(limit,Qt::KeepAspectRatio):size;
    }

    static QRect clamp(QRect rect,const QRect &bounds)
    {
        rect.setSize(fit(rect.size(),bounds.size()));
        rect.moveTo(qBound(bounds.left(),rect.x(),bounds.right()-rect.width()+1),
                    qBound(bounds.top(),rect.y(),bounds.bottom()-rect.height()+1));
        return rect;
    }

    static QVector<QPoint> corners(const QRect &rect)
    {
        return {rect.topLeft(),rect.topRight(),rect.bottomLeft(),rect.bottomRight()};
    }

    void begin(const QPoint &point)
    {
        if(m_active || m_pressedControl)return;
        if(auto *button=qobject_cast<QPushButton*>(childAt(point))) {
            m_pressedControl=button;
            m_pointer=m_start=point;
            button->setDown(true);
            return;
        }
        m_handle=-1;
        if(m_selected>=0) {
            const auto handles=corners(m_items.at(m_selected).rect);
            for(int i=0;i<handles.size();++i)
                if((handles.at(i)-point).manhattanLength()<=20) {m_handle=i;break;}
        }
        if(m_handle<0) {
            m_selected=-1;
            for(int i=m_items.size()-1;i>=0;--i)
                if(m_items.at(i).rect.contains(point)) {m_selected=i;break;}
        }
        const bool rotatable=m_selected>=0 && m_items.at(m_selected).target.rotatable;
        if(rotatable) {
            const auto &label=m_items.at(m_selected).target.rotateLabel;
            m_rotate->setText(label.isEmpty()?tr("Rotate toolbar"):label);
        }
        m_rotate->setVisible(rotatable);
        if(m_selected>=0) {
            m_active=true;
            m_moved=false;
            m_pointer=m_start=point;
            m_initial=m_items.at(m_selected).rect;
        }
        update();
    }

    void move(const QPoint &point)
    {
        m_pointer=point;
        if(m_pressedControl) {
            m_pressedControl->setDown(m_pressedControl->geometry().contains(point) &&
                (point-m_start).manhattanLength()<QApplication::startDragDistance());
            return;
        }
        if(!m_active || m_selected<0)return;
        m_moved=m_moved || (point-m_start).manhattanLength()>=QApplication::startDragDistance();
        if(!m_moved)return;
        auto &item=m_items[m_selected];
        QVector<int> xs{bounds().left(),bounds().right()+1};
        QVector<int> ys{bounds().top(),bounds().bottom()+1};
        for(int i=0;i<m_items.size();++i) {
            if(i==m_selected)continue;
            const QRect r=m_items.at(i).rect;
            xs << r.left() << r.right()+1;
            ys << r.top() << r.bottom()+1;
        }
        const auto snap=[](int value,int trailing,const QVector<int> &lines) {
            int delta=0,best=13;
            for(int line:lines) {
                for(int edge:{value,trailing}) {
                    const int next=line-edge;
                    if(qAbs(next)<best) {best=qAbs(next);delta=next;}
                }
            }
            return delta;
        };
        const auto offset=point-m_start;
        if(m_handle<0) {
            auto r=m_initial.translated(offset);
            r.translate(snap(r.left(),r.right()+1,xs),snap(r.top(),r.bottom()+1,ys));
            item.rect=clamp(r,bounds());
        } else {
            const auto originalCorners=corners(m_initial);
            const auto anchor=originalCorners.at(3-m_handle);
            const int signX=(m_handle%2)?1:-1,signY=(m_handle>=2)?1:-1;
            const auto moving=originalCorners.at(m_handle)+offset;
            const qreal w=qMax(1,m_initial.width()),h=qMax(1,m_initial.height());
            const qreal dx=(moving.x()-anchor.x())*signX+1;
            const qreal dy=(moving.y()-anchor.y())*signY+1;
            const qreal sx=snap(moving.x(),moving.x(),xs);
            const qreal sy=snap(moving.y(),moving.y(),ys);
            qreal factor=(dx*w+dy*h)/(w*w+h*h);
            if(sx!=0 && (sy==0 || qAbs(sx)<qAbs(sy)))factor=(dx+sx*signX)/w;
            else if(sy!=0)factor=(dy+sy*signY)/h;
            const QSize minimum=item.rotated?item.target.minimum.transposed():item.target.minimum;
            const qreal limitX=(signX>0?bounds().right()-anchor.x()+1:anchor.x()-bounds().left()+1)/w;
            const qreal limitY=(signY>0?bounds().bottom()-anchor.y()+1:anchor.y()-bounds().top()+1)/h;
            QSize size;
            const auto thickness=item.target.thicknessAxis;
            if(thickness) {
                const bool width=(thickness==Qt::Horizontal)!=item.rotated;
                const int limit=qMax(1,qRound(width?limitX*w:limitY*h));
                const int lower=qMin(limit,width?minimum.width():minimum.height());
                const int value=qBound(lower,qRound(width?dx+sx*signX:dy+sy*signY),limit);
                size=width?QSize(value,m_initial.height()):QSize(m_initial.width(),value);
            } else {
                const qreal limit=qMin(limitX,limitY);
                factor=qBound(qMin(limit,qMax(minimum.width()/w,minimum.height()/h)),factor,limit);
                size=QSize(qMax(1,qRound(w*factor)),qMax(1,qRound(h*factor)));
            }
            item.rect=clamp(QRect(QPoint(anchor.x()+(signX<0?1-size.width():0),
                                         anchor.y()+(signY<0?1-size.height():0)),size),bounds());
        }
        update();
    }

    void finish(bool accept=true)
    {
        m_active=false;
        auto *button=m_pressedControl.data();
        m_pressedControl.clear();
        if(!button)return;
        const bool click=accept && button->isDown();
        button->setDown(false);
        if(click)button->click();
    }

    QVector<Item> m_items;
    std::function<void()> m_finished;
    QPushButton *m_done=nullptr,*m_cancel=nullptr,*m_rotate=nullptr,*m_workspaces=nullptr;
    QPointer<QPushButton> m_pressedControl;
    int m_selected=-1,m_handle=-1;
    bool m_active=false,m_moved=false,m_closing=false;
    QPoint m_start,m_pointer;
    QRect m_initial;
};
