// SPDX-License-Identifier: GPL-2.0-or-later
#include "mobile_core/recent_colors.h"
#include <QCoreApplication>
#include <QTemporaryDir>

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir settings;
    if(!settings.isValid())return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QCoreApplication::setOrganizationName(QStringLiteral("KritaStrokeColorTest"));
    QCoreApplication::setApplicationName(QStringLiteral("Studio"));
    QSettings().setValue(QStringLiteral("MobileStudio/recentColors"),QStringList{QStringLiteral("#ff123456")});
    using namespace MobileRecentColors;
    if(!load().isEmpty() || !recordStroke(QColor()).isEmpty() ||
       !recordStroke(Qt::transparent).isEmpty())return 2;
    for(int i=0;i<28;++i)recordStroke(QColor(i,0,0));
    if(load().size()!=8 || load().first()!=QColor(27,0,0).name(QColor::HexArgb))return 3;
    const auto expanded=load(pickerCapacity);
    if(expanded.size()!=24 || expanded.last()!=QColor(4,0,0).name(QColor::HexArgb) ||
       !recordStroke(QColor(27,0,0)).isEmpty())return 4;
    const auto history=recordStroke(QColor(5,0,0));
    if(history.size()!=8 || history.first()!=QColor(5,0,0).name(QColor::HexArgb) ||
       load(pickerCapacity).count(history.first())!=1)return 5;
}
