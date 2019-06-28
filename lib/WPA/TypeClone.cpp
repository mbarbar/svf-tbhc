/*
 * TypeClone.cpp
 *
 *  Created on: Apr 09, 2019
 *      Author: Mohamad Barbar
 */

#include <queue>

#include "MemoryModel/CHA.h"
#include "WPA/TypeClone.h"
#include "WPA/WPAStat.h"
#include "Util/CPPUtil.h"

#include <queue>

// TODO: add back all the timers.

const std::string UNDEF_TYPE = "";

void TypeClone::initialize(SVFModule svfModule) {
    this->svfModule = svfModule;
    findAllocGlobals();
    FlowSensitive::initialize(svfModule);
}

bool TypeClone::processAddr(const AddrSVFGNode* addr) {
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
        idToAllocLocMap[srcID] = addr->getId();
    } else {
        idToTypeMap[srcID] = tilde(cppUtil::getNameFromType(pag->getPAGNode(srcID)->getType()));
        idToAllocLocMap[srcID] = addr->getId();
        assert(idToTypeMap[srcID] != "" && "TypeClone: non-heap does not have a type?");
    }

    return changed;
}

bool TypeClone::processGep(const GepSVFGNode* gep) {
    bool derefChanged = processDeref(gep, gep->getPAGSrcNodeID());  // TODO: double check.
    return derefChanged || FlowSensitive::processGep(gep);
    // TODO: this will probably change more substantially.
}

bool TypeClone::processLoad(const LoadSVFGNode* load) {
    bool derefChanged = processDeref(load, load->getPAGSrcNodeID());
    return derefChanged || FlowSensitive::processLoad(load);
}

bool TypeClone::processStore(const StoreSVFGNode* store) {
    bool derefChanged = processDeref(store, store->getPAGDstNodeID());
    return derefChanged || FlowSensitive::processStore(store);
}

bool TypeClone::propagateFromAPToFP(const ActualParmSVFGNode* ap, const SVFGNode* dst) {
    const FormalParmSVFGNode* fp = SVFUtil::dyn_cast<FormalParmSVFGNode>(dst);
    assert(fp && "not a formal param node?!");

    NodeID pagDst = fp->getParam()->getId();
    PointsTo &srcCPts = getPts(ap->getParam()->getId());
    PointsTo &dstCPts = getPts(pagDst);

    const Argument *arg = SVFUtil::dyn_cast<Argument>(fp->getParam()->getValue());
    assert(arg && "Not an argument?!");
    const Function *f = arg->getParent();

    bool changed = false;
    if (cppUtil::isConstructor(f) && arg->getArgNo() == 0) {
        // Passing `this` argument - clone some of the objects.
        for (PointsTo::iterator oI = srcCPts.begin(); oI != srcCPts.end(); ++oI) {
            NodeID o = *oI;
            // Propagate o, UNLESS we need to clone.
            NodeID prop = o;

            if (T(o) == UNDEF_TYPE) {
                // CALL-CONS
                prop = getCloneObject(o, ap);
                if (prop == 0) {
                    // The arguments static type is what we are initialising to.
                    TypeStr t = cppUtil::getNameFromType(arg->getType());
                    prop = cloneObject(o, ap, t);
                }
            }

            changed = changed || dstCPts.test_and_set(prop);
        }
    } else {
        // Standard case, not a constructor's `this`.
        changed = unionPts(pagDst, srcCPts);
    }

    return changed;
}

bool TypeClone::propVarPtsFromSrcToDst(NodeID var, const SVFGNode* src, const SVFGNode* dst) {
    const PointsTo &srcPts = getPts(0);  // TODO: get the points-to set!!
    NodeID varAllocLoc = idToAllocLocMap[var];

    // TODO: can be easily optimised.
    for (PointsTo::iterator oI = srcPts.begin(); oI != srcPts.end(); ++oI) {
        NodeID o = *oI;
        if (idToAllocLocMap[*oI] == varAllocLoc) {
            bool changed = false;
            if (SVFUtil::isa<StoreSVFGNode>(src)) {
                if (updateInFromOut(src, o, dst, o))
                    changed = true;
            }
            else {
                if (updateInFromIn(src, o, dst, o))
                    changed = true;
            }
            return changed;
        }
    }

}

bool TypeClone::processDeref(const SVFGNode *stmt, const NodeID ptrId) {
    PointsTo &ptrPt = getPts(ptrId);
    TypeStr t = staticType(ptrId);
    bool changed = false;

    PointsTo tmp;
    for (PointsTo::iterator oI = ptrPt.begin(); oI != ptrPt.end(); ++oI) {
        NodeID o = *oI;
        TypeStr tp = T(o);
        NodeID prop = 0;

        if (T(o) == UNDEF_TYPE) {
            // DEREF-UNTYPED
            NodeID cloneId = getCloneObject(o, stmt);
            if (cloneId == 0) {
                cloneId = cloneObject(o, stmt, tilde(t));
            }

            prop = cloneId;
        } else if (isBase(tp, tilde(t)) && tp != tilde(t)) {
            // DEREF-DOWN
            // We want the absolute base of o (which is a clone).
            NodeID base = cloneToBaseMap[o];
            assert(base && "not looking at a clone?!");

            NodeID downCloneId = getCloneObject(base, stmt);
            if (downCloneId == 0) {
                downCloneId = cloneObject(base, stmt, tilde(t));
            }

            prop = downCloneId;
        } else if (isBase(tilde(t), tp) || tilde(t) == tp || tilde(t) == UNDEF_TYPE) {
            // DEREF-UP
            prop = o;
        } else {
            assert(false && "FAILURE!");
        }

        assert(prop && "propagating nothing?!");
        tmp.set(prop);
    }

    return ptrPt |= tmp;
}

bool TypeClone::baseBackPropagate(NodeID o) {
    NodeID allocId = idToAllocLocMap[o];
    assert(allocId != 0 && "Allocation site never set!");
    SVFGNode *node = svfg->getSVFGNode(allocId);
    AddrSVFGNode *allocNode = SVFUtil::dyn_cast<AddrSVFGNode>(node);
    assert(allocNode && "Allocation site incorrectly set!");
    NodeID allocAssigneeId = allocNode->getPAGDstNode()->getId();

    bool changed = getPts(allocAssigneeId).test_and_set(o);
    return changed;
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
    if (idToTypeMap.find(n) != idToTypeMap.end()) {
        return idToTypeMap.at(n);
    } else {
        return UNDEF_TYPE;
    }
}

TypeClone::TypeStr TypeClone::staticType(NodeID p) const {
    // TODO.
    return UNDEF_TYPE;
}

NodeID TypeClone::getCloneObject(const NodeID o, const SVFGNode *cloneLoc) {
    return idToClonesMap[o][cloneLoc->getId()];
}

NodeID TypeClone::cloneObject(const NodeID o, const SVFGNode *cloneLoc, TypeStr type) {
    // Clone created.
    NodeID cloneId = pag->addDummyObjNode();

    // Attributes of the clone
    idToTypeMap[cloneId] = type;
    idToCloneLocMap[cloneId] = cloneLoc->getId();

    // Track the clone
    idToClonesMap[o][cloneLoc->getId()] = cloneId;

    // Reverse tracking.
    cloneToBaseMap[cloneId] = o;

    return cloneId;
}

void TypeClone::findAllocGlobals(void) {
    for (SVFG::iterator svfgNodeI = svfg->begin(); svfgNodeI != svfg->end(); ++svfgNodeI) {
        if (!SVFUtil::isa<AddrSVFGNode>(svfgNodeI->second)) {
            // We are only looking for nodes reachable by allocation sites.
            continue;
        }

        AddrSVFGNode *addrSvfgNode = &(SVFUtil::dyn_cast<AddrSVFGNode>(*svfgNodeI));
        std::set<NodeID> seen;
        std::queue<NodeID> bfs;

        bfs.push(addrSvfgNode->getId());
        do {
            NodeID curr = bfs.front();
            bfs.pop();
            seen.insert(curr);

            if (glob(curr)) {
                // Found a global which alloc flows to.
                allocToGlobalsMap[addrSvfgNode->getId()].insert(curr);
            } else {
                // Keep looking.
                SVFGNode *currNode = svfg->getSVFGNode(curr);
                for (SVFGEdge::SVFGEdgeSetTy::iterator edgeI = currNode->getOutEdges().begin(); edgeI != currNode->getOutEdges().end(); ++edgeI) {
                    NodeID next = (*edgeI)->getDstID();
                    if (seen.find(next) != seen.end()) {
                        continue;
                    }

                    bfs.push(next);
                }
            }
        } while (!bfs.empty());
    }
}

bool glob(NodeID svfgNodeId) {
    return false;
}

