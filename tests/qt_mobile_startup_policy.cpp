#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QTemporaryDir>
#include "mobile_core/mobile_startup_policy.h"

static void check(bool ok,const char *reason) {
    if(!ok)qFatal("Mobile startup policy failure: %s",reason);
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir temp;
    check(temp.isValid(),"temporary settings");
    const auto file=temp.filePath(QStringLiteral("mobile.ini"));
    using D=MobileStartupPolicy::Decision;
    {
        QSettings s(file,QSettings::IniFormat);
        check(!s.contains("MobileStudio/startDirectly"),"fresh settings");
        check(MobileStartupPolicy::begin(s)==D::Start,"new installs launch mobile");
        check(s.value("MobileStudio/bootPending").toBool(),"pending marker");
        MobileStartupPolicy::complete(s,true);
        check(!s.value("MobileStudio/bootPending").toBool(),"success cleanup");
        check(!s.contains("MobileStudio/startDirectly"),"default stays implicit");
        s.setValue("MobileStudio/startDirectly",false);
        check(MobileStartupPolicy::begin(s)==D::NoAutostart,"user opt-out");
        s.setValue("MobileStudio/startDirectly",true);
        s.setValue("MobileStudio/bootPending",true);
        check(MobileStartupPolicy::begin(s)==D::RecoverNativeKrita,
              "recover incomplete start");
        check(!s.value("MobileStudio/startDirectly").toBool() &&
              !s.value("MobileStudio/bootPending").toBool(),"loop blocked");
        check(MobileStartupPolicy::begin(s)==D::NoAutostart,"blocked retry");
        s.setValue("MobileStudio/startDirectly",true);
        check(MobileStartupPolicy::begin(s)==D::Start,"manual re-enable");
        MobileStartupPolicy::complete(s,false);
        check(MobileStartupPolicy::begin(s)==D::NoAutostart,"failed handoff blocked");
    }
    check(!MobileStartupPolicy::ready(false,true,false,false),"invisible host");
    check(!MobileStartupPolicy::ready(true,false,false,false),"missing canvas");
    check(!MobileStartupPolicy::ready(true,true,true,false),"nested transaction");
    check(!MobileStartupPolicy::ready(true,true,false,true),"modal dialog");
    check(MobileStartupPolicy::ready(true,true,false,false),"visible ready host");
    qInfo()<<"PASS: fresh mobile-first default, opt-out, crash recovery,"
           <<"single handoff attempt and host readiness";
    return 0;
}
