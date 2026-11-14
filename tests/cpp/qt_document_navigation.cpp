/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Independent Qt regression for exact view-identity document cycling.
 */
#include <QCoreApplication>
#include <QDebug>
#include <QSharedPointer>
#include "mobile_studio/document_navigation.h"

static void check(bool ok,const char *message)
{
    if(!ok)qFatal("Mobile document navigation: %s",message);
}

struct FakeHost final : MobileViewHostPort
{
    QObject a,b,c,da,db,dc;
    mutable QPointer<QObject> active=&a;
    mutable QPointer<QObject> current=&da;
    bool ambiguous=false;
    bool missing=false;
    bool duplicate=false;
    QVector<ViewSnapshot> openViews() const override
    {
        return {
            {QStringLiteral("A"),false,active==&a,0,
             const_cast<QObject*>(&a),const_cast<QObject*>(&da)},
            {QStringLiteral("B"),false,active==&b || ambiguous,1,
             const_cast<QObject*>(&b),const_cast<QObject*>(&db)},
            {QStringLiteral("C"),false,active==&c,2,
             duplicate?const_cast<QObject*>(&a):
                       const_cast<QObject*>(&c),
             missing?nullptr:const_cast<QObject*>(&dc)}
        };
    }
    QPointer<QObject> currentDocument() const override {return current;}
    bool activateView(const QPointer<QObject> &view) const override
    {
        if(view==&a){active=const_cast<QObject*>(&a);current=const_cast<QObject*>(&da);return true;}
        if(view==&b){active=const_cast<QObject*>(&b);current=const_cast<QObject*>(&db);return true;}
        if(view==&c){active=const_cast<QObject*>(&c);current=const_cast<QObject*>(&dc);return true;}
        return false;
    }
    bool closeView(const QPointer<QObject> &) const override {return false;}
};

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    auto host=QSharedPointer<FakeHost>::create();
    check(MobileDocumentNavigation::cycle(host,-1) &&
          host->active==&host->c && host->current==&host->dc,
          "previous navigation did not wrap A to C");
    check(MobileDocumentNavigation::cycle(host,1) &&
          host->active==&host->a && host->current==&host->da,
          "next navigation did not wrap C to A");
    check(MobileDocumentNavigation::cycle(host,1) &&
          host->active==&host->b && host->current==&host->db,
          "next navigation did not select B");
    check(!MobileDocumentNavigation::cycle(host,0),
          "invalid navigation direction accepted");
    // Mark both A and B active; the fake host's previous selection
    // is B, so reset it to A to actually create two active rows.
    host->active=&host->a;
    host->current=&host->da;
    host->ambiguous=true;
    check(!MobileDocumentNavigation::cycle(host,1),
          "multiple active views were accepted");
    host->ambiguous=false;
    host->active=&host->b;
    host->current=&host->da; // A changed without active tab.
    check(!MobileDocumentNavigation::cycle(host,1),
          "out-of-sync selected Krita document was accepted");
    host->current=&host->db;
    host->duplicate=true;
    check(!MobileDocumentNavigation::cycle(host,1),
          "duplicate view identity was accepted");
    host->duplicate=false;
    host->missing=true;
    check(MobileDocumentNavigation::cycle(host,-1) &&
          host->active==&host->a,
          "invalid inactive row broke navigation of remaining views");
    host->current=nullptr;
    check(!MobileDocumentNavigation::cycle(host,1),
          "null selected document was accepted");
    qInfo() << "PASS: native view cycling, exact identities and wrapping";
    return 0;
}
