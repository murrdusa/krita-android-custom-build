/*
 * Qt5 Widgets test: deferred UI actions cannot accidentally target the
 * wrong canvas when native MDI views are removed or reordered.
 *
 * No Krita document, saved file, or Android APK is involved.
 */
#include <QApplication>
#include <QDebug>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPointer>
#include <QWidget>

#include "mobile_core/mdi_view_identity.h"

static void require(bool value,const char *message)
{
    if(!value)qFatal("MDI identity test failed: %s",message);
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QMdiArea area;
    auto *first=area.addSubWindow(new QWidget);
    auto *second=area.addSubWindow(new QWidget);
    auto *third=area.addSubWindow(new QWidget);
    require(first && second && third,"could not construct native MDI views");

    const QPointer<QObject> target=MobileMdiViewIdentity::capture(second);
    const QPointer<QObject> removedTarget=
        MobileMdiViewIdentity::capture(third);
    require(MobileMdiViewIdentity::resolve(&area,target)==second,
            "initial identity does not resolve to the selected view");

    // Remove a view that was ahead of our target in CreationOrder.
    // Resolving by the old integer index would now select the wrong view.
    delete first;
    QApplication::processEvents();
    const auto remaining=area.subWindowList(QMdiArea::CreationOrder);
    require(remaining.indexOf(second)==0,
            "test setup did not reorder creation indices");
    require(MobileMdiViewIdentity::resolve(&area,target)==second,
            "view identity shifted to the wrong document after deletion");

    // Detach another window. It may still exist as a QObject, but it is
    // no longer owned by the source QMdiArea and must not be actionable.
    area.removeSubWindow(third);
    QApplication::processEvents();
    require(!MobileMdiViewIdentity::resolve(&area,removedTarget),
            "removed MDI view was still accepted");
    require(MobileMdiViewIdentity::resolve(&area,target)==second,
            "detaching another view affected the intended target");

    // If the saved target itself disappears, the guarded pointer must
    // reject it instead of resolving to whatever now occupies index 0.
    delete second;
    QApplication::processEvents();
    require(!MobileMdiViewIdentity::resolve(&area,target),
            "deleted view identity resolved to a different live view");
    require(!MobileMdiViewIdentity::resolve(nullptr,removedTarget),
            "missing MDI backend resolved a stale identity");
    require(!MobileMdiViewIdentity::resolve(&area,{}),
            "empty identity resolved to a live view");

    qInfo() << "PASS: reorder, detachment, deletion and invalid MDI identity";
    return 0;
}
