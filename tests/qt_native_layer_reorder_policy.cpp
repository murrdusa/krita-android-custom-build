/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QCoreApplication>
#include <QDebug>
#include "mobile_core/native_layer_reorder_policy.h"

static void test(bool ok,const char *message)
{
    if(!ok)qFatal("Native Krita layer reorder: %s",message);
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    using Reorder=MobileNativeLayerReorderPolicy;
    test(Reorder::targetIndex(0,3,true)==1,"raise bottom layer");
    test(Reorder::targetIndex(1,3,true)==2,"raise middle layer");
    test(Reorder::targetIndex(2,3,false)==1,"lower top layer");
    test(Reorder::targetIndex(1,3,false)==0,"lower middle layer");
    test(Reorder::targetIndex(2,3,true)==-1,"raise top must be no-op");
    test(Reorder::targetIndex(0,3,false)==-1,"lower bottom must be no-op");
    test(Reorder::targetIndex(0,1,true)==-1,"single layer must be no-op");
    test(Reorder::targetIndex(-1,3,true)==-1,"missing selection");
    test(Reorder::targetIndex(3,3,false)==-1,"stale out-of-range node");
    test(Reorder::targetIndex(0,3,true,0)==-1,"empty selection");
    test(Reorder::targetIndex(0,3,true,2)==-1,"multi-select unsupported");
    qInfo()<<"PASS: native layer reorder bounds, selection and edge no-ops";
    return 0;
}
