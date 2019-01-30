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

    if (addCopyEdge(node, dst)) {
        // Push into worklist ourselves since we changed node's value.
        pushIntoWorklist(node);
    }

    return false;
}

bool AndersenLS::processStore(NodeID node, const ConstraintEdge* store) {
    if (pag->isConstantObj(node) || isNonPointerObj(node))
        return false;

    numOfProcessedStore++;

    NodeID src = store->getSrcID();

    node = origToLs[node];
    assert(SVFUtil::isa<LSObjPN>(pag->getPAGNode(node)));
    LSObjPN *lsObjNode = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(node));

    std::set<NodeID> ptsNodes =lsObjNode->getPtsNodes();

    // Rather than adding a copy edge, save the node in lsObjNode.
    lsObjNode->addPtsNode(src);

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

        //lsObjNode->addPtsNode(*origIdI); // TODO: unnecessary?
        // So it propagates itself.
        getPts(lsNodeId).set(lsNodeId);

        origToLs[*origIdI] = lsNodeId;
        lsToOrig[lsNodeId] = *origIdI;
        // This is to prevent checking if a node is an LS node or original node before looking up.
        origToLs[lsNodeId] = lsNodeId;
        lsToOrig[*origIdI] = *origIdI;
    }
}

void AndersenLS::buildAllActualPts(void) {
    for (std::map<NodeID, NodeID>::iterator lsOrigPairI = lsToOrig.begin(); lsOrigPairI != lsToOrig.end(); ++lsOrigPairI) {
        NodeID lsId = lsOrigPairI->first;
        NodeID origObjId = lsOrigPairI->second;

        LSObjPN *lsObj = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(lsId));
        if (lsObj == NULL) continue;

        std::set<NodeID> &lsPtsNodes = lsObj->getPtsNodes();
        std::set<NodeID> actualPts;


        for (std::set<NodeID>::iterator ptNodeI = lsPtsNodes.begin(); ptNodeI != lsPtsNodes.end(); ++ptNodeI) {
            if (SVFUtil::isa<ObjPN>(pag->getPAGNode(*ptNodeI))) {
                // Have an actual object: either original, LS, or dummy.
                actualPts.insert(*ptNodeI);
            } else {
                // Have a points to set to handle.
                PointsTo &pts = getPts(*ptNodeI);

                for (PointsTo::iterator ptoI = pts.begin(); ptoI != pts.end(); ++ptoI) {
                    assert(SVFUtil::isa<ObjPN>(pag->getPAGNode(*ptoI)));
                    actualPts.insert(*ptoI);
                }
            }
        }

        lsObj->setActualPts(actualPts);
    }
}

void AndersenLS::flattenLSNodes(void) {
    buildAllActualPts();

    // Now actualPts only contains LS, dummy, and normal objects.
    // We need to unwrap the LS objects to the actual objects
    for (std::map<NodeID, NodeID>::iterator lsOrigPairI = lsToOrig.begin(); lsOrigPairI != lsToOrig.end(); ++lsOrigPairI) {
        NodeID lsId = lsOrigPairI->first;
        NodeID origObjId = lsOrigPairI->second;

        LSObjPN *lsObj = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(lsId));
        if (lsObj == NULL) continue;

        std::set<NodeID> actualPts = lsObj->getActualPts();
        actualPts.erase(lsId);  // TODO: this may not always be appropriate. Maybe a flag for natural flow?

        bool change = false;
        std::set<NodeID> added;
        do {
            change = false;
            std::set<NodeID> toAdd;
            std::set<NodeID> toRemove;

            for (std::set<NodeID>::iterator nI = actualPts.begin(); nI != actualPts.end(); ++nI) {
                assert(SVFUtil::isa<ObjPN>(pag->getPAGNode(*nI)));
                if (!SVFUtil::isa<LSObjPN>(pag->getPAGNode(*nI))) {
                    // A normal object.
                    continue;
                }

                NodeID ptLsId = *nI;
                if (added.find(ptLsId) != added.end()) {
                    // Don't add same set twice, otherwise may infinite loop.
                    continue;
                }

                LSObjPN *ptLsObj = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(ptLsId));
                assert(ptLsObj);  // TODO error msg

                // Remove the LS node because we're adding everything it refers to.
                toRemove.insert(ptLsId);
                added.insert(ptLsId);
                toAdd.insert(ptLsObj->getActualPts().begin(), ptLsObj->getActualPts().end());
            }

            for (std::set<NodeID>::iterator addI = toAdd.begin(); addI != toAdd.end(); ++addI) {
                if (added.find(*addI) != added.end() || *addI == lsId) {
                    // #1: Already been added, won't process again so it means we point to the underlying obj.
                    // #2: Adding ourself, so we must point to underlying obj.
                    if (actualPts.insert(lsToOrig[*addI]).second) change = true;
                } else {
                    // Normal case, either a plain object is added or another LS obj (to process) is added.
                    if (actualPts.insert(*addI).second) change = true;
                }
            }

            for (std::set<NodeID>::iterator rmI = toRemove.begin(); rmI != toRemove.end(); ++rmI) {
                if (actualPts.erase(*rmI)) change = true;
            }
        } while (change);

        std::transform(actualPts.begin(), actualPts.end(), std::inserter(actualPts, actualPts.begin()),
                       [this](const NodeID id){ return this->lsToOrig[id]; });
        lsObj->setActualPts(actualPts);
    }
}

void AndersenLS::removeLSFromAllPts(void) {
    for (PAG::iterator pagNodeI = pag->begin(); pagNodeI != pag->end(); ++pagNodeI) {
        PointsTo &pts = getPts(pagNodeI->first);
        std::set<NodeID> toAdd;
        std::set<NodeID> toRemove;

        if (ObjPN *fiObj = SVFUtil::dyn_cast<FIObjPN>(pag->getPAGNode(pagNodeI->first))) {
            LSObjPN *repLsNode = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(origToLs[pagNodeI->first]));
            assert(repLsNode); // TODO message.
            std::set<NodeID> &actualPts = repLsNode->getActualPts();
            for (std::set<NodeID>::iterator actualI = actualPts.begin(); actualI != actualPts.end(); ++actualI) {
                pts.set(*actualI);
            }
        } else {
            for (PointsTo::iterator ptoI = pts.begin(); ptoI != pts.end(); ++ptoI) {
                if (LSObjPN *lsObj = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(*ptoI))) {
                    toRemove.insert(lsObj->getId());

                    //toAdd.insert(lsObj->getActualPts().begin(), lsObj->getActualPts().end());
                    for (std::set<NodeID>::iterator actualI = lsObj->getActualPts().begin(); actualI != lsObj->getActualPts().end(); ++actualI) {
                        toAdd.insert(*actualI);
                    }
                }
            }

            std::for_each(toAdd.begin(), toAdd.end(), [&pts](NodeID id){ pts.set(id); });
            std::for_each(toRemove.begin(), toRemove.end(), [&pts](NodeID id){ pts.reset(id); });
        }
    }
}

