/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QApplication>
#include <QCloseEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QDebug>
#include <QWidget>
#include "mobile_core/engine_only_startup_policy.h"
#include "mobile_core/engine_only_root.h"

static void check(bool ok,const char *why)
{
    if(!ok)qFatal("Engine-only Android boot: %s",why);
}

class ProtectedCanvas final : public QWidget
{
public:
    explicit ProtectedCanvas(QWidget *parent):QWidget(parent){}
    bool mayClose=false;
    int closeRequests=0;
protected:
    void closeEvent(QCloseEvent *event) override
    {
        ++closeRequests;
        event->setAccepted(mayClose);
    }
};

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QTemporaryDir dir;
    check(dir.isValid(),"missing temporary settings folder");
    const QString ini=dir.filePath(QStringLiteral("boot.ini"));
    QSettings s(ini,QSettings::IniFormat);
    using Policy=MobileEngineOnlyStartupPolicy;
    using Decision=Policy::Decision;
    check(Policy::enabled(s) &&
          Policy::begin(s,true)==Decision::Launch,
          "fresh installs must start independent engine");
    check(s.value(Policy::pendingKey()).toBool(),
          "first independent launch must arm crash recovery");
    Policy::complete(s,true);
    check(Policy::enabled(s) && s.contains(Policy::key()),
          "successful default launch must persist V2 preference");

    Policy::setEnabled(s,false);
    check(!Policy::enabled(s) &&
          Policy::begin(s,true)==Decision::Desktop,
          "explicit desktop opt-out must remain respected");
    Policy::setEnabled(s,true);
    check(Policy::enabled(s),"manual re-enable failed");
    check(!s.value(QStringLiteral("MobileStudio/nativeStartupV1")).toBool(),
          "legacy native startup still enabled");
    // Upgrade from previous native V1 startup; V2 must take ownership
    // of the only Android top-level before the legacy host can launch.
    Policy::setEnabled(s,false);
    s.setValue(QStringLiteral("MobileStudio/nativeStartupV1"),true);
    s.setValue(QStringLiteral("MobileStudio/startDirectly"),true);
    check(Policy::begin(s,true)==Decision::Desktop,
          "explicit opt-out did not override old native preferences");
    // An existing V1 install with no engine-only preference migrates
    // automatically, but only on a compatible normal launch.
    s.remove(Policy::key());
    check(Policy::begin(s,false)==Decision::Desktop &&
          s.value(QStringLiteral("MobileStudio/nativeStartupV1")).toBool() &&
          !s.value(Policy::pendingKey()).toBool(),
          "incompatible file intent must not start V2");
    check(Policy::begin(s,true)==Decision::Launch,
          "old V1 installation did not migrate to V2 default");
    check(!s.value(QStringLiteral("MobileStudio/nativeStartupV1")).toBool() &&
          !s.value(QStringLiteral("MobileStudio/startDirectly")).toBool(),
          "legacy launchers remained enabled during V2 boot");
    Policy::complete(s,true);
    Policy::setEnabled(s,true);
    check(Policy::begin(s,false)==Decision::Desktop &&
          !s.value(Policy::pendingKey()).toBool(),
          "incompatible external launch entered engine-only");
    check(Policy::begin(s,true)==Decision::Launch &&
          s.value(Policy::pendingKey()).toBool(),
          "opt-in did not arm crash recovery");
    QSettings nextProcess(ini,QSettings::IniFormat);
    check(Policy::begin(nextProcess,true)==Decision::RecoverDesktop &&
          !Policy::enabled(nextProcess),
          "interrupted boot did not recover original desktop");
    check(Policy::begin(nextProcess,true)==Decision::Desktop,
          "crash loop was not disabled");

    Policy::setEnabled(s,true);
    check(Policy::begin(s,true)==Decision::Launch,
          "explicit engine-only retry was rejected");
    Policy::complete(s,true);
    check(Policy::enabled(s) &&
          !s.value(Policy::pendingKey()).toBool(),
          "successful native engine boot did not persist");
    check(Policy::begin(s,true)==Decision::Launch,
          "successful boot preference lost on next launch");
    Policy::complete(s,false);
    check(!Policy::enabled(s),"failed launch still enabled native boot");

    // V3 is opt-in; if it crashes it must revert to known-good V2.
    Policy::setEnabled(s,true);
    check(!Policy::directHostEnabled(s),
          "direct window must be opt-in");
    Policy::setDirectHostEnabled(s,true);
    check(Policy::enabled(s) && Policy::directHostEnabled(s),
          "direct window preference must enable the engine");
    check(Policy::begin(s,true)==Decision::Launch,
          "direct host refused first launch");
    QSettings crashedDirect(ini,QSettings::IniFormat);
    check(Policy::begin(crashedDirect,true)==Decision::Launch &&
          !Policy::directHostEnabled(crashedDirect) &&
          Policy::enabled(crashedDirect) &&
          crashedDirect.value(Policy::pendingKey()).toBool(),
          "V3 interrupted boot must retry verified V2");
    Policy::complete(crashedDirect,true);
    Policy::setDirectHostEnabled(s,true);
    check(Policy::begin(s,true)==Decision::Launch,
          "direct host refused second launch");
    Policy::complete(s,false);
    check(Policy::enabled(s) && !Policy::directHostEnabled(s) &&
          !s.value(Policy::pendingKey()).toBool(),
          "failed direct host should preserve verified V2");
    Policy::setDirectHostEnabled(s,true);
    Policy::setEnabled(s,false);
    check(!Policy::directHostEnabled(s) && !Policy::enabled(s),
          "desktop opt-out must disable direct host");

    MobileEngineOnlyRoot root;
    root.setCentralWidget(new QWidget);
    root.resize(420,780);
    auto *drawing=new ProtectedCanvas(&root);
    drawing->setObjectName(QStringLiteral("_mobileStandaloneEnginePilot"));
    drawing->show();
    root.setDrawingHost(drawing);
    root.show();
    QApplication::processEvents();
    check(root.hasDrawingHost(),"Qt root lost native drawing host");
    check(!root.close() && drawing->closeRequests==1 &&
          root.isVisible(),"Cancel must keep engine-only app open");
    drawing->mayClose=true;
    check(root.close() && drawing->closeRequests==2,
          "accepted document close did not close Android root");
    check(!root.hasDrawingHost(),
          "root did not release painting-host ownership on close");
    qInfo()<<"PASS: default engine-only startup, legacy V1 migration,"
              " explicit desktop opt-out, interrupted-boot fallback"
              " and unsaved document close guard";
    return 0;
}
