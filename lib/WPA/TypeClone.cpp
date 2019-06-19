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

const std::string UNDEF_TYPE = "";

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
    PointsTo &ptrPt = getPts(ptrId);
    TypeStr t = staticType(ptrId);
    bool changed = false;

    for (PointTo::iterator oI = ptrPt.begin(); oI != ptrPt.end(); ++oI) {
        NodeID o = *oI;
        TypeStr tp = T(o);
        if (T(o) == UNDEF_TYPE) {
            // DEREF-UNTYPED
        } else if (isBase(tp, tilde(t)) && tp != tilde(t)) {
            // DEREF-DOWN

        } else if (isBase(tilde(t), tp) || tilde(t) == tp || tilde(t) = UNDEF_TYPE) {
            // DEREF-UP

        } else {
            assert(false && "FAILURE!");
        }
    }

    return changed;
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

TypeClone::TypeStr TypeClone::T(NodeID n) const {
    return idToTypeMap[n];
}

TypeClone::TypeStr TypeClone::staticType(NodeID p) const {
    // TODO.
    return UNDEF_TYPE;
}

