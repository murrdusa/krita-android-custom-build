/* SPDX-License-Identifier: GPL-2.0-or-later */
// Native Krita reorder anchor policy: exhaustive sibling-position checks.
// No Qt dependency, so this runs with a normal host C++ compiler.
#define KRITA_MOBILE_QT_TEST
#include "mobile_studio/native_layer_reorder_policy.h"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <vector>

static int failures=0;
static void check(bool ok,const char *name,int count,int index,bool up)
{
    if(ok)return;
    ++failures;
    std::cerr<<"FAIL: "<<name<<" siblings="<<count
             <<" index="<<index<<" up="<<up<<"\n";
}
int main()
{
    using Policy=MobileNativeLayerReorderPolicy;
    for(int count=0;count<=128;++count) {
        for(int current=-2;current<=count+1;++current) {
            for(bool up:{false,true}) {
                const auto plan=Policy::plan(current,count,up);
                const int expectedIndex=current+(up?1:-1);
                const bool allowed=count>=2 && current>=0 &&
                    current<count && expectedIndex>=0 &&
                    expectedIndex<count;
                check(plan.valid==allowed,"move availability",count,current,up);
                check(Policy::targetIndex(current,count,up)==
                      (allowed?expectedIndex:-1),
                      "target index",count,current,up);
                if(!allowed)continue;
                check(plan.targetIndex==expectedIndex,"planned target",
                      count,current,up);
                check(plan.anchorIndex==(up?current+1:current-2),
                      "anchor index",count,current,up);
                check(plan.anchorIndex!=current &&
                      plan.anchorIndex>=-1 && plan.anchorIndex<count,
                      "valid non-self anchor",count,current,up);
                std::vector<int> original(count);
                std::iota(original.begin(),original.end(),0);
                std::vector<int> actual=original;
                const int moving=actual[current];
                const int anchor=plan.anchorIndex>=0?
                    actual[plan.anchorIndex]:-1;
                actual.erase(actual.begin()+current);
                if(anchor<0)actual.insert(actual.begin(),moving);
                else {
                    const auto it=std::find(actual.begin(),actual.end(),anchor);
                    if(it==actual.end()) {
                        check(false,"anchor lost",count,current,up);
                        continue;
                    }
                    actual.insert(it+1,moving);
                }
                auto expected=original;
                std::swap(expected[current],expected[expectedIndex]);
                check(actual==expected,"native sibling reorder result",
                      count,current,up);
                check(!Policy::plan(current,count,up,2).valid,
                      "multi-selection policy blocked",count,current,up);
            }
        }
    }
    if(failures) {
        std::cerr<<failures<<" native reorder policy failures\n";
        return 1;
    }
    std::cout<<"PASS: exhaustive native layer reorder targets, anchors and"
                " sibling permutations (0-128 layers)\n";
    return 0;
}
