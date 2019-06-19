/*
 * TypeClone.cpp
 *
 *  Created on: Apr 09, 2019
 *      Author: Mohamad Barbar
 */

#include "MemoryModel/CHA.h"
#include "WPA/TypeClone.h"
#include "WPA/WPAStat.h"
#include "Util/CPPUtil.h"

void TypeClone::initialize(SVFModule svfModule) {
    this->svfModule = svfModule;
    FlowSensitive::initialize(svfModule);
}

bool TypeClone::processAddr(const AddrSVFGNode* addr) {
    double start = stat->getClk();

    NodeID srcID = addr->getPAGSrcNodeID();
    /// TODO: see FieldSensitive::processAddr
    if (isFieldInsensitive(srcID)) {
        srcID = getFIObjNode(srcID);
    }

    bool changed = addPts(addr->getPAGDstNodeID(), srcID);

    // Should not have a type, not even undefined.
    assert(idToTypeMap.find(srcID) == idToTypeMap.end() && "TypeClone: already has type!");
    if (isHeapMemObj(srcID)) {
        // Heap objects are initialised with no types.
        idToTypeMap[srcID] = "";
        idToAllocNodeMap[srcID] = addr->getId();
    } else {
        idToTypeMap[srcID] = tilde(cppUtil::getNameFromType(pag->getPAGNode(srcID)->getType()));
        assert(idToTypeMap[srcID] != "" && "TypeClone: non-heap does not have a type?");
    }

    double end = stat->getClk();
    addrTime += (end - start) / TIMEINTERVAL;

    return changed;
}

bool TypeClone::processGep(const GepSVFGNode* gep) {
    processDeref(gep->getPAGSrcNodeID());  // TODO: double check.
    return FlowSensitive::processGep(gep);
    // TODO: this will probably change more substantially.
}

bool TypeClone::processLoad(const LoadSVFGNode* load) {
    processDeref(load->getPAGSrcNodeID());
    return FlowSensitive::processLoad(load);
}

bool TypeClone::processStore(const StoreSVFGNode* store) {
    processDeref(store->getPAGDstNodeID());
    return FlowSensitive::processStore(store);
}

bool TypeClone::processDeref(const NodeID ptrId) {
    return false;
}

void TypeClone::baseBackPropagate(NodeID o) {
    NodeID allocId = idToAllocNodeMap[o];
    assert(allocId != 0 && "Allocation site never set!");
    SVFGNode *allocNode = getSVFGNode(allocId);
    NodeID allocAssigneeId = allocNode->getPAGDstNodeID();
    getPts(allocAssigneeId).set(o);
}

bool TypeClone::isBase(TypeStr a, TypeStr b) const {
    if (a == b) return true;

    const CHGraph::CHNodeSetTy& aChildren = chg->getInstancesAndDescendants(a);
    const CHNode *bNode = chg->getNode(b);
    // If b is in the set of a's children, then a is a base type of b.
    return aChildren.find(bNode) != aChildren.end();
}

TypeClone::TypeStr TypeClone::tilde(TypeStr t) const {
    // TODO
    return t;
}

