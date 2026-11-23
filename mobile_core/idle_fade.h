/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <QWidget>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>

namespace MobileIdleFade
{
inline void wake(QWidget *panel,bool enabled)
{
    if(!panel)return;
    if(auto *effect=qobject_cast<QGraphicsOpacityEffect*>(panel->graphicsEffect())) {
        if(auto *animation=effect->findChild<QPropertyAnimation*>())animation->stop();
        effect->setOpacity(1);
        if(!enabled)panel->setGraphicsEffect(nullptr);
    }
}
inline void dim(QWidget *panel,int percent)
{
    if(!panel)return;
    auto *effect=qobject_cast<QGraphicsOpacityEffect*>(panel->graphicsEffect());
    if(!effect) {
        effect=new QGraphicsOpacityEffect(panel);
        effect->setOpacity(1);
        panel->setGraphicsEffect(effect);
    }
    const qreal target=qBound(0,percent,100)/100.0;
    auto *animation=effect->findChild<QPropertyAnimation*>();
    if(qFuzzyCompare(effect->opacity(),target) ||
       (animation && animation->state()==QAbstractAnimation::Running))return;
    if(!animation)animation=new QPropertyAnimation(effect,"opacity",effect);
    animation->setDuration(180);
    animation->setStartValue(effect->opacity());
    animation->setEndValue(target);
    animation->start();
}
}
