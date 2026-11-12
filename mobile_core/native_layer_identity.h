// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace MobileLayerIdentity
{
template<class Node>
bool inTree(Node node,const Node &root)
{
    if(!root)return false;
    for(;node;node=node->parent())
        if(node==root)return true;
    return false;
}

template<class Node,class List,class IsInternal>
List transformTargets(const Node &root,const List &selected,const Node &active,
                      IsInternal isInternal,bool editingSelection)
{
    if(editingSelection && active!=root && inTree(active,root) && selected==List{active})
        return selected;
    List targets;
    bool hasExplicitTarget=false;
    for(const auto &node:selected) {
        if(!node || isInternal(node))continue;
        hasExplicitTarget=true;
        if(node!=root && inTree(node,root) && !targets.contains(node))
            targets.append(node);
    }
    if(targets.isEmpty() && !hasExplicitTarget && active && active!=root &&
       inTree(active,root) && !isInternal(active))
        targets.append(active);
    return targets;
}
}
