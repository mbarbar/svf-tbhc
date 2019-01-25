//===- AndersenLS.cpp -- Andersen's analysis load-store optimisation (TODO) --//

/*
 * AndersenLS.cpp
 *
 *  Created on: 24/12/2019
 *      Author: Mohamad Barbar
 */

#include "WPA/Andersen.h"

bool AndersenLS::processLoad(NodeID node, const ConstraintEdge* load) {
    if (pag->isConstantObj(node) || isNonPointerObj(node))
        return false;

    numOfProcessedLoad++;

    NodeID dst = load->getDstID();

    // Add the copy edge to the LS node.
    node = origToLs[node];
    assert(SVFUtil::isa<LSObjPN>(pag->getPAGNode(node)));

    return addCopyEdge(node, dst);
}

bool AndersenLS::processStore(NodeID node, const ConstraintEdge* store) {
    if (pag->isConstantObj(node) || isNonPointerObj(node))
        return false;

    numOfProcessedStore++;

    NodeID src = store->getSrcID();

    node = origToLs[node];
    assert(SVFUtil::isa<LSObjPN>(pag->getPAGNode(node)));
    LSObjPN *lsObjNode = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(node));

    // Rather than adding a copy edge, copy it to internal representation.
    PointsTo &srcPts = getPts(src);
    for (PointsTo::iterator ptoI = srcPts.begin(); ptoI != srcPts.end(); ++ptoI) {
        lsObjNode->addObjectNode(*ptoI);
    }

    return false;
}

void AndersenLS::prepareLSNodes(void) {
    std::set<NodeID> origObjs;
    for (PAG::iterator nodeI = pag->begin(); nodeI != pag->end(); ++nodeI) {
        if (SVFUtil::isa<ObjPN>(nodeI->second)) origObjs.insert(nodeI->first);
    }

    for (std::set<NodeID>::const_iterator origIdI = origObjs.begin(); origIdI != origObjs.end(); ++origIdI) {
        const NodeID lsNodeId = pag->addDummyLSObjNode();
        LSObjPN *lsObjNode = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(lsNodeId));
        consCG->addConstraintNode(new ConstraintNode(lsNodeId), lsNodeId);

        lsObjNode->addObjectNode(*origIdI);

        origToLs[*origIdI] = lsNodeId;
        lsToOrig[lsNodeId] = *origIdI;
        // This is to prevent checking if a node is an LS node or original node before looking up.
        origToLs[lsNodeId] = lsNodeId;
    }
}
