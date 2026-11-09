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
    check(Policy::begin(s,true)==Decision::Desktop,
          "fresh installs must boot desktop");
    Policy::setEnabled(s,true);
    check(Policy::enabled(s),"opt-in not saved");
    check(!s.value(QStringLiteral("MobileStudio/nativeStartupV1")).toBool(),
          "legacy native startup still enabled");
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
    qInfo()<<"PASS: engine-only Qt startup preference, interrupted-boot"
              " recovery and unsaved document close guard";
    return 0;
}
