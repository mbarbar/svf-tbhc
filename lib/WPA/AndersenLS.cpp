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

    NodeID src = load->getSrcID();
    NodeID dst = load->getDstID();

    NodeID lsId = getLsNodeId(node);
    LSObjPN *lsObj = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(lsId));
    assert(lsObj);

    if (isOrigNodeId(node)) {
        // If we're trying to load the original object, then add an edge
        // from the LS object.
        if (addCopyEdge(lsId, dst)) {
            // Push into worklist ourselves since we changed node's value.
            pushIntoWorklist(lsId);
        }
    } else {
        // If we're trying to load an LS object, then add an edge from
        // everything that was stored into the LS object, i.e. the
        // unnatural nodes.
        std::set<NodeID> &unnaturalPtsNodes = lsObj->getUnnaturalPtsNodes();
        for (std::set<NodeID>::iterator nI = unnaturalPtsNodes.begin(); nI != unnaturalPtsNodes.end(); ++nI) {
            if (addCopyEdge(*nI, dst)) {
                // Push into worklist ourselves since we changed node's value.
                pushIntoWorklist(*nI);
            }
        }

        /*
        std::set<NodeID> &naturalPtsNodes = lsObj->getNaturalPtsNodes();
        for (std::set<NodeID>::iterator nI = naturalPtsNodes.begin(); nI != naturalPtsNodes.end(); ++nI) {
            if (addCopyEdge(*nI, dst)) {
                // Push into worklist ourselves since we changed node's value.
                pushIntoWorklist(*nI);
            }
        }
        */

        // Now this load edge relies on any changes to lsId.
        reliance[lsId].insert(src);
    }

    return false;
}

bool AndersenLS::processStore(NodeID node, const ConstraintEdge* store) {
    if (pag->isConstantObj(node) || isNonPointerObj(node))
        return false;

    numOfProcessedStore++;

    NodeID src = store->getSrcID();

    bool natural = isOrigNodeId(node);
    // If we are storing an object to the ORIGINAL ID, not the LS ID, then
    // it is natural. I.e. we are storing to the object propagated by the
    // addr edge earlier.

    node = getLsNodeId(node);
    assert(SVFUtil::isa<LSObjPN>(pag->getPAGNode(node)));
    LSObjPN *lsObjNode = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(node));

    // Rather than adding a copy edge, save the node in the LS object.
    lsObjNode->addPtsNode(src, natural);
    if (!natural) {
        // TODO: change node to lsId.
        std::set<NodeID> reliers = reliance[node];
        for (std::set<NodeID>::iterator relierI = reliers.begin(); relierI != reliers.end(); ++relierI) {
            pushIntoWorklist(*relierI);
        }
    }

    return false;
}

void AndersenLS::prepareLSNodes(void) {
    std::set<NodeID> origObjs;
    for (PAG::iterator nodeI = pag->begin(); nodeI != pag->end(); ++nodeI) {
        if (SVFUtil::isa<ObjPN>(nodeI->second)) origObjs.insert(nodeI->first);
    }

    for (std::set<NodeID>::const_iterator origIdI = origObjs.begin(); origIdI != origObjs.end(); ++origIdI) {
        const NodeID lsNodeId = pag->addDummyLSObjNode(*origIdI);
        LSObjPN *lsObjNode = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(lsNodeId));
        consCG->addConstraintNode(new ConstraintNode(lsNodeId), lsNodeId);

        //lsObjNode->addPtsNode(*origIdI); // TODO: this would be incorrect: make sure.
        // So it propagates itself.
        getPts(lsNodeId).set(lsNodeId);

        llvm::outs() << "orig: " << *origIdI << "ls: " << lsNodeId << "\n";;
        origToLs[*origIdI] = lsNodeId;
        lsToOrig[lsNodeId] = *origIdI;
    }
}

void AndersenLS::removeValFromLS(std::set<NodeID> &ptsNodes) {
    std::set<NodeID> cleanPtsNodes;

    for (std::set<NodeID>::iterator ptNodeI = ptsNodes.begin(); ptNodeI != ptsNodes.end(); ++ptNodeI) {
        if (SVFUtil::isa<ObjPN>(pag->getPAGNode(*ptNodeI))) {
            // Have an actual object: either original, LS, or dummy.
            cleanPtsNodes.insert(*ptNodeI);
        } else {
            if (*ptNodeI == 31) {
                llvm::outs() << "HI\n";
            }
            // Have a value node, so we want its PTS.
            PointsTo &pts = getPts(*ptNodeI);

            for (PointsTo::iterator ptoI = pts.begin(); ptoI != pts.end(); ++ptoI) {
                assert(SVFUtil::isa<ObjPN>(pag->getPAGNode(*ptoI)));
                cleanPtsNodes.insert(*ptoI);
            }
        }
    }

    ptsNodes.clear();
    ptsNodes.insert(cleanPtsNodes.begin(), cleanPtsNodes.end());
}

void AndersenLS::flattenAllLS(void) {
    for (std::map<NodeID, NodeID>::iterator lsOrigPairI = lsToOrig.begin(); lsOrigPairI != lsToOrig.end(); ++lsOrigPairI) {
        NodeID lsId = lsOrigPairI->first;
        NodeID origId = lsOrigPairI->second;

        LSObjPN *lsObj = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(lsId));
        assert(lsObj && "Non-LS object counted as LS object!");

        removeValFromLS(lsObj->getNaturalPtsNodes());
        removeValFromLS(lsObj->getUnnaturalPtsNodes());
    }

    // TODO: duplication.

    for (std::map<NodeID, NodeID>::iterator lsOrigPairI = lsToOrig.begin(); lsOrigPairI != lsToOrig.end(); ++lsOrigPairI) {
        NodeID lsId = lsOrigPairI->first;
        NodeID origId = lsOrigPairI->second;

        LSObjPN *lsObj = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(lsId));
        assert(lsObj && "Non-LS object counted as LS object!");

        removeLSFromLS(lsObj->getNaturalPtsNodes(), lsId);
        removeLSFromLS(lsObj->getUnnaturalPtsNodes(), lsId);
    }
}

void AndersenLS::removeLSFromLS(std::set<NodeID> &ptsNodes, NodeID selfId) {
    std::set<NodeID> cleanPtsNodes;
    // Start with ptsNodes, and operate within cleanPtsNodes.
    cleanPtsNodes.insert(ptsNodes.begin(), ptsNodes.end());

    // Whether something has been added to cleanPtsNodes.
    bool change = false;
    std::set<NodeID> processed;

    do {
        change = false;

        std::set<NodeID> toAdd;
        std::set<NodeID> toRemove;

        for (std::set<NodeID>::iterator nI = cleanPtsNodes.begin(); nI != cleanPtsNodes.end(); ++nI) {
            assert(SVFUtil::isa<ObjPN>(pag->getPAGNode(*nI)));

            if (!SVFUtil::isa<LSObjPN>(pag->getPAGNode(*nI))) {
                // A normal object.
                continue;
            }

            NodeID lsId = *nI;
            if (processed.find(lsId) != processed.end()) {
                // Don't add same set twice, otherwise may infinite loop.
                continue;
            }

            LSObjPN *lsObj = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(lsId));

            // Remove the LS node because we're adding everything it refers to.
            toRemove.insert(lsId);
            processed.insert(lsId);

            toAdd.insert(lsObj->getNaturalPtsNodes().begin(), lsObj->getNaturalPtsNodes().end());
            // TODO: need to grab unnatural nodes if it's all natural.
            //toAdd.insert(lsObj->getUnnaturalPtsNodes().begin(), lsObj->getUnnaturalPtsNodes().end());
        }

        for (std::set<NodeID>::iterator addI = toAdd.begin(); addI != toAdd.end(); ++addI) {
            if (processed.find(*addI) != processed.end() || *addI == selfId) {
                // #1: Already been added, won't process again so it means we point to the underlying obj.
                // #2: Adding ourself, so we must point to underlying obj.
                if (cleanPtsNodes.insert(getOrigNodeId(*addI)).second) change = true;
            } else {
                // Normal case, either a plain object is added or another LS obj (to process) is added.
                if (cleanPtsNodes.insert(*addI).second) change = true;
            }
        }

        for (std::set<NodeID>::iterator rmI = toRemove.begin(); rmI != toRemove.end(); ++rmI) {
            if (cleanPtsNodes.erase(*rmI)) change = true;
        }
    } while (change);

    /* TODO: WHY?
    std::transform(actualPts.begin(), actualPts.end(), std::inserter(actualPts, actualPts.begin()),
                   [this](const NodeID id){ return this->lsToOrig[id]; });
    */
    for (auto it = cleanPtsNodes.begin(); it != cleanPtsNodes.end(); ++it) {
        llvm::outs() << *it << ", \n";
    }

    ptsNodes.clear();
    ptsNodes.insert(cleanPtsNodes.begin(), cleanPtsNodes.end());
}

void AndersenLS::removeLSFromAllPts(void) {
    for (PAG::iterator pagNodeI = pag->begin(); pagNodeI != pag->end(); ++pagNodeI) {
        PointsTo &pts = getPts(pagNodeI->first);
        std::set<NodeID> toAdd;
        std::set<NodeID> toRemove;

        if (ObjPN *fiObj = SVFUtil::dyn_cast<FIObjPN>(pag->getPAGNode(pagNodeI->first))) {
            LSObjPN *repLsNode = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(origToLs[pagNodeI->first]));
            assert(repLsNode); // TODO message.
            std::set<NodeID> &naturalPts = repLsNode->getNaturalPtsNodes();
            for (std::set<NodeID>::iterator actualI = naturalPts.begin(); actualI != naturalPts.end(); ++actualI) {
                pts.set(*actualI);
            }

            /*
            std::set<NodeID> &unnaturalPts = repLsNode->getUnnaturalPtsNodes();
            for (std::set<NodeID>::iterator actualI = unnaturalPts.begin(); actualI != unnaturalPts.end(); ++actualI) {
                pts.set(*actualI);
            }
            // TODO: unnatural
            */
        } else {
            continue;
        }
    }

    for (PAG::iterator pagNodeI = pag->begin(); pagNodeI != pag->end(); ++pagNodeI) {
        PointsTo &pts = getPts(pagNodeI->first);
        std::set<NodeID> toAdd;
        std::set<NodeID> toRemove;

        if (ObjPN *fiObj = SVFUtil::dyn_cast<FIObjPN>(pag->getPAGNode(pagNodeI->first))) {
            continue;
        } else {
            for (PointsTo::iterator ptoI = pts.begin(); ptoI != pts.end(); ++ptoI) {
                if (LSObjPN *lsObj = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(*ptoI))) {
                    toRemove.insert(lsObj->getId());

                    //toAdd.insert(lsObj->getActualPts().begin(), lsObj->getActualPts().end());
                    PointsTo &pts = getPts(lsToOrig[lsObj->getId()]);
                    for (PointsTo::iterator actualI = pts.begin(); actualI != pts.end(); ++actualI) {
                        llvm::outs() << "HI: " << *actualI << "\n";
                        toAdd.insert(*actualI);
                    }
                }
            }

            std::for_each(toAdd.begin(), toAdd.end(), [&pts](NodeID id){ pts.set(id); });
            std::for_each(toRemove.begin(), toRemove.end(), [&pts](NodeID id){ pts.reset(id); });
        }
    }
}

void AndersenLS::insertUnnaturalContemporariesInObjPts(void) {
    for (std::map<NodeID, NodeID>::iterator lsOrigPairI = lsToOrig.begin(); lsOrigPairI != lsToOrig.end(); ++lsOrigPairI) {
        NodeID lsId = lsOrigPairI->first;
        NodeID origId = lsOrigPairI->second;

        LSObjPN *lsObj = SVFUtil::dyn_cast<LSObjPN>(pag->getPAGNode(lsId));
        assert(lsObj);

        std::set<NodeID> unnaturalPtsNodes = lsObj->getUnnaturalPtsNodes();
        for (std::set<NodeID>::iterator unNodeI = unnaturalPtsNodes.begin(); unNodeI != unnaturalPtsNodes.end(); ++unNodeI) {
            NodeID curr = *unNodeI;
            assert(isOrigNodeId(curr) && "Unnatural PTS nodes not flattened!");
            PointsTo &currPts = getPts(curr);

            for (std::set<NodeID>::iterator unNodeI = unnaturalPtsNodes.begin(); unNodeI != unnaturalPtsNodes.end(); ++unNodeI) {
                // TODO What if you tried to store curr into the LS obj - need a flag...
                assert(isOrigNodeId(*unNodeI) && "Unnatural PTS nodes not flattened!");
                if (curr != *unNodeI) currPts.set(*unNodeI);
            }
        }

        std::set<NodeID> naturalPtsNodes = lsObj->getNaturalPtsNodes();
        for (std::set<NodeID>::iterator nNodeI = naturalPtsNodes.begin(); nNodeI != naturalPtsNodes.end(); ++nNodeI) {
            NodeID curr = *nNodeI;
            assert(isOrigNodeId(curr) && "Natural PTS nodes not flattened!");
            PointsTo &currPts = getPts(curr);

            for (std::set<NodeID>::iterator unNodeI = unnaturalPtsNodes.begin(); unNodeI != unnaturalPtsNodes.end(); ++unNodeI) {
                // TODO What if you tried to store curr into the LS obj - need a flag...
                assert(isOrigNodeId(*unNodeI) && "Unnatural PTS nodes not flattened!");
                if (curr != *unNodeI) currPts.set(*unNodeI);
            }
        }
    }
}

