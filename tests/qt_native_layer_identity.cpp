// SPDX-License-Identifier: GPL-2.0-or-later
#include "mobile_core/native_layer_identity.h"
#include <QList>

struct Node {
    Node *owner=nullptr;
    bool internal=false;
    Node *parent() const {return owner;}
};

int main()
{
    Node root,otherRoot,group{&root},selected{&group},active{&root},
        globalMask{&root,true},stale{&otherRoot};
    const auto internal=[](const Node *node) {return node->internal;};
    using Nodes=QList<Node*>;
    const auto targets=[&](const Nodes &nodes,Node *current,bool editing=false) {
        return MobileLayerIdentity::transformTargets(&root,nodes,current,internal,editing);
    };
    if(targets({&selected},&active)!=Nodes{&selected})return 1;
    if(targets({&group,&selected,&selected,&stale},&active)!=Nodes{&group})return 2;
    if(targets({&selected,&group},&active)!=Nodes{&group})return 7;
    Node sibling{&root};
    if(targets({&selected,&sibling},&active)!=Nodes{&selected,&sibling})return 8;
    Node nestedMask{&selected,true};
    if(targets({&group,&selected,&nestedMask},&active)!=Nodes{&group})return 9;
    if(!targets({&stale},&active).isEmpty() || !targets({&root},&active).isEmpty())return 3;
    if(targets({},&active)!=Nodes{&active} ||
       targets({&globalMask},&active)!=Nodes{&active})return 4;
    if(targets({&globalMask},&globalMask,true)!=Nodes{&globalMask} ||
       !targets({&globalMask},&globalMask).isEmpty())return 5;
    selected.owner=nullptr;
    if(!targets({&selected},&active).isEmpty())return 6;
}
