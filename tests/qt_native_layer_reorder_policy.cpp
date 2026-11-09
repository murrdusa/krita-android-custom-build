/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QCoreApplication>
#include <QDebug>
#include <algorithm>
#include <vector>
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
    // Model Krita's sibling-anchor semantics, not Qt's inverted tree
    // display. Moving up from [A,B,C] must produce [A,C,B], and
    // moving down must produce [B,A,C] if B is at the middle.
    // The command stores the original below-sibling for Undo.
    auto roundTrip=[](int current,bool up,int expected) {
        std::vector<int> original{10,20,30,40};
        std::vector<int> nodes=original;
        auto plan=Reorder::plan(current,int(nodes.size()),up);
        test(plan.valid && plan.targetIndex==expected,
             "unexpected native move plan");
        const int moved=nodes.at(current);
        const int oldBelow=current>0 ? nodes.at(current-1) : -1;
        const int newBelow=plan.anchorIndex>=0 ?
                               nodes.at(plan.anchorIndex) : -1;
        test(newBelow!=moved,"self-referential anchor");
        nodes.erase(nodes.begin()+current);
        const auto insertAfter=[&](int below) {
            return below<0 ? nodes.begin() :
                std::find(nodes.begin(),nodes.end(),below)+1;
        };
        nodes.insert(insertAfter(newBelow),moved);
        test(nodes.at(expected)==moved && nodes!=original,
             "reorder did not change expected layer position");

        auto moving=std::find(nodes.begin(),nodes.end(),moved);
        nodes.erase(moving);
        nodes.insert(insertAfter(oldBelow),moved);
        test(nodes==original,"undo did not restore original layer order");
    };
    roundTrip(0,true,1);
    roundTrip(1,true,2);
    roundTrip(2,true,3);
    roundTrip(1,false,0);
    roundTrip(2,false,1);
    roundTrip(3,false,2);
    test(Reorder::plan(1,4,true).anchorIndex==2,
         "upward move must anchor to next sibling, not itself");
    test(Reorder::plan(1,4,false).anchorIndex==-1,
         "downward move to bottom must use null anchor");
    test(Reorder::plan(3,4,false).anchorIndex==1,
         "downward move must anchor two sibling positions below");
    test(!Reorder::plan(3,4,true).valid,
         "moving above the top layer cannot create a stroke");
    test(!Reorder::plan(0,4,false).valid,
         "moving below the bottom layer cannot create a stroke");
    test(!Reorder::plan(1,4,true,2).valid,
         "multi-layer reorder still needs separate validation");
    qInfo()<<"PASS: both layer directions, sibling anchors, undo roundtrip"
              " and no-op boundaries";
    return 0;
}
