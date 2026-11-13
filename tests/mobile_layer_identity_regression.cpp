// SPDX-License-Identifier: GPL-2.0-or-later
// Qt-free tests for transform target filtering and nested masks.
#include "../mobile_core/native_layer_identity.h"
#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <vector>

struct Node {
    Node *owner=nullptr;
    bool internal=false;
    Node *parent() const {return owner;}
};
struct Nodes : std::vector<Node*> {
    using std::vector<Node*>::vector;
    Nodes(std::initializer_list<Node*> values):std::vector<Node*>(values) {}
    Nodes()=default;
    bool contains(Node *value) const {
        return std::find(begin(),end(),value)!=end();
    }
    bool isEmpty() const {return empty();}
    void append(Node *value) {push_back(value);}
};
int main() {
    Node root, otherRoot, group{&root}, child{&group}, grandchild{&child};
    Node sibling{&root}, otherChild{&group}, hiddenMask{&child,true};
    Node stale{&otherRoot};
    const auto internal=[](Node *n){return n->internal;};
    const auto targets=[&](const Nodes &selected,Node *active=nullptr,bool editing=false) {
        return MobileLayerIdentity::transformTargets(&root,selected,active,internal,editing);
    };
    struct Case {Nodes input; Nodes expected; Node *active; bool editing; const char *name;};
    const std::vector<Case> cases={
        {{&group,&child},{&group},nullptr,false,"group-first"},
        {{&child,&group},{&group},nullptr,false,"child-first"},
        {{&grandchild,&group,&child},{&group},nullptr,false,"three-level nesting"},
        {{&child,&otherChild},{&child,&otherChild},nullptr,false,"independent children"},
        {{&sibling,&group,&child},{&sibling,&group},nullptr,false,"unrelated sibling"},
        {{&child,&child},{&child},nullptr,false,"duplicate selection"},
        {{&stale},{},&sibling,false,"stale explicit selection blocks fallback"},
        {{&hiddenMask},{&sibling},&sibling,false,"hidden selection allows visible active fallback"},
        {{},{&sibling},&sibling,false,"fallback to active"},
        {{&hiddenMask},{&hiddenMask},&hiddenMask,true,"explicit edit selection"},
        {{&hiddenMask},{},&hiddenMask,false,"hidden mask not transformed"},
        {{&group,&hiddenMask},{&group},nullptr,false,"nested internal mask"},
        {{&root},{},&root,false,"root not paint target"},
        {{&child},{&child},&child,true,"ordinary edit selection"}
    };
    for(const auto &test:cases) {
        if(targets(test.input,test.active,test.editing)!=test.expected) {
            std::cerr<<"FAIL: "<<test.name<<'\n';return 1;
        }
    }
    std::cout<<"PASS: "<<cases.size()<<" native transform target hierarchy cases\n";
}
