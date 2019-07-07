/*
 * TypeClone.cpp
 *
 *  Created on: Apr 09, 2019
 *      Author: Mohamad Barbar
 */

#include <queue>

#include <sstream>

#include "MemoryModel/CHA.h"
#include "WPA/TypeClone.h"
#include "WPA/WPAStat.h"
#include "Util/CPPUtil.h"

// TODO: add back all the timers.

const std::string TypeClone::UNDEF_TYPE = "";

void TypeClone::initialize(SVFModule svfModule) {
    FlowSensitive::initialize(svfModule);

    this->svfModule = svfModule;
    chg = PointerAnalysis::getCHGraph();
    chg->dump("chg.dot");
    chg->addFirstFieldRelations();
    chg->buildClassNameToAncestorsDescendantsMap();
    chg->dump("chg_ff.dot");
    findAllocGlobals();
}

void TypeClone::finalize() {

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
        idToTypeMap[srcID] = UNDEF_TYPE;
        idToAllocLocMap[srcID] = addr->getId();
    } else {
        idToTypeMap[srcID] = cppUtil::getNameFromType(pag->getPAGNode(srcID)->getType());
        idToAllocLocMap[srcID] = addr->getId();
        assert(idToTypeMap[srcID] != UNDEF_TYPE && "TypeClone: non-heap does not have a type?");
    }

    return changed;
}

bool TypeClone::processGep(const GepSVFGNode* gep) {
    //return FlowSensitive::processGep(gep);
    bool derefChanged = processDeref(gep, gep->getPAGSrcNodeID());  // TODO: double check.
    bool gepChanged = processGepProper(gep);
    // TODO: this will probably change more substantially.
    return derefChanged || gepChanged;
}

bool TypeClone::processGepProper(const GepSVFGNode* edge) {
    bool changed = false;
    const PointsTo& srcPts = getPts(edge->getPAGSrcNodeID());

    PointsTo tmpDstPts;
    for (PointsTo::iterator piter = srcPts.begin(); piter != srcPts.end(); ++piter) {
        NodeID ptd = *piter;
        if (isBlkObjOrConstantObj(ptd))
            tmpDstPts.set(ptd);
        else {
            if (SVFUtil::isa<VariantGepPE>(edge->getPAGEdge())) {
                setObjFieldInsensitive(ptd);
                tmpDstPts.set(getFIObjNode(ptd));
            }
            else if (const NormalGepPE* normalGep = SVFUtil::dyn_cast<NormalGepPE>(edge->getPAGEdge())) {
                NodeID fieldSrcPtdNode;
                if (normalGep->getLocationSet().getByteOffset() == 0) {
                    // First field is the same as the object.
                    fieldSrcPtdNode = ptd;
                } else {
                    fieldSrcPtdNode = getGepObjNode(ptd, normalGep->getLocationSet());
                    GepObjPN *gepObj = SVFUtil::dyn_cast<GepObjPN>(pag->getPAGNode(fieldSrcPtdNode));
                    idToTypeMap[fieldSrcPtdNode] = cppUtil::getNameFromType(gepObj->getType());
                }

                tmpDstPts.set(fieldSrcPtdNode);
            }
            else
                assert(false && "new gep edge?");
        }
    }

    if (unionPts(edge->getPAGDstNodeID(), tmpDstPts))
        changed = true;

    return changed;
}

bool TypeClone::processLoad(const LoadSVFGNode* load) {
    bool changed = false;
    NodeID dstVar = load->getPAGDstNodeID();

    const PointsTo& srcPts = getPts(load->getPAGSrcNodeID());
    NodeID srcNodeId = load->getPAGSrcNodeID();
    for (PointsTo::iterator ptdIt = srcPts.begin(); ptdIt != srcPts.end(); ++ptdIt) {
        NodeID ptd = *ptdIt;

        std::string staticString = staticType(srcNodeId);
        llvm::outs() << "size: " << getPts(ptd).count() << "tilde(src) = " << ((staticString == UNDEF_TYPE) ? "undef" : tilde(staticType(srcNodeId))) << " T(ptd) = " << T(ptd) << "\n";

        if (pag->isConstantObj(ptd) || pag->isNonPointerObj(ptd))
            continue;

        if (unionPtsFromIn(load, ptd, dstVar))
            changed = true;

        if (isFIObjNode(ptd)) {
            /// If the ptd is a field-insensitive node, we should also get all field nodes'
            /// points-to sets and pass them to pagDst.
            const NodeBS& allFields = getAllFieldsObjNode(ptd);
            for (NodeBS::iterator fieldIt = allFields.begin(), fieldEit = allFields.end();
                    fieldIt != fieldEit; ++fieldIt) {
                if (unionPtsFromIn(load, *fieldIt, dstVar))
                    changed = true;
            }
        }
    }

    return changed;

    /*
    bool derefChanged = processDeref(load, load->getPAGSrcNodeID());
    bool loadChanged = FlowSensitive::processLoad(load);
    llvm::outs() << "load: " << derefChanged << ":" << loadChanged << "\n";;
    return derefChanged || loadChanged;
    */
}

bool TypeClone::processStore(const StoreSVFGNode* store) {
    /*
    bool derefChanged = processDeref(store, store->getPAGDstNodeID());
    bool storeChanged = FlowSensitive::processStore(store);
    llvm::outs() << "store: " << derefChanged << ":" << storeChanged << "\n";;
    return derefChanged || storeChanged;
    */

    bool changed;
    const PointsTo & dstPts = getPts(store->getPAGDstNodeID());

    /// STORE statement can only be processed if the pointer on the LHS
    /// points to something. If we handle STORE with an empty points-to
    /// set, the OUT set will be updated from IN set. Then if LHS pointer
    /// points-to one target and it has been identified as a strong
    /// update, we can't remove those points-to information computed
    /// before this strong update from the OUT set.
    if (dstPts.empty())
        return false;


    if(getPts(store->getPAGSrcNodeID()).empty() == false) {
        for (PointsTo::iterator it = dstPts.begin(), eit = dstPts.end(); it != eit; ++it) {
            NodeID ptd = *it;

            std::string staticString = staticType(store->getPAGSrcNodeID());
            llvm::outs() << "store size: " << getPts(ptd).count() << "tilde(dst) = " << ((staticString == UNDEF_TYPE) ? "undef" : tilde(staticType(store->getPAGDstNodeID()))) << " T(ptd) = " << T(ptd) << "\n";

            if (pag->isConstantObj(ptd) || pag->isNonPointerObj(ptd))
                continue;

            if (unionPtsFromTop(store, store->getPAGSrcNodeID(), ptd))
                changed = true;
        }
    }

    // also merge the DFInSet to DFOutSet.
    /// check if this is a strong updates store
    NodeID singleton;
    bool isSU = isStrongUpdate(store, singleton);
    if (isSU) {
        svfgHasSU.set(store->getId());
        if (strongUpdateOutFromIn(store, singleton))
            changed = true;
    }
    else {
        svfgHasSU.reset(store->getId());
        if (weakUpdateOutFromIn(store))
            changed = true;
    }

    return changed;
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
                    // TODO: back-prop
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
    bool changed = false;
    for (PointsTo::iterator oI = srcPts.begin(); oI != srcPts.end(); ++oI) {
        NodeID o = *oI;
        if (idToAllocLocMap[*oI] == varAllocLoc) {
            if (SVFUtil::isa<StoreSVFGNode>(src)) {
                if (updateInFromOut(src, o, dst, o))
                    changed = true;
            }
            else {
                if (updateInFromIn(src, o, dst, o))
                    changed = true;
            }
        }
    }

    return changed;
}

bool TypeClone::processDeref(const SVFGNode *stmt, const NodeID ptrId) {
    PointsTo &ptrPt = getPts(ptrId);
    TypeStr t = staticType(ptrId);
    bool changed = false;
    std::set<NodeID> toBackPropagate;

    PointsTo filterPt;
    for (PointsTo::iterator oI = ptrPt.begin(); oI != ptrPt.end(); ++oI) {
        NodeID o = *oI;
        TypeStr tp = T(o);
        NodeID prop = 0;

        llvm::outs() << "o: " << o << "T(o): " << T(o) << ", tilde(t)" << tilde(t) << "\n";

        if (T(o) == UNDEF_TYPE) {
            // DEREF-UNTYPED
            NodeID cloneId = getCloneObject(o, stmt);
            if (cloneId == 0) {
                cloneId = cloneObject(o, stmt, tilde(t));
                if (baseBackPropagate(cloneId)) {
                    toBackPropagate.insert(cloneId);
                }
            }

            prop = cloneId;
            llvm::outs() << "to = undef\n";
        } else if (isBase(tp, tilde(t)) && tp != tilde(t)) {
            // DEREF-DOWN
            // We want the absolute base of o (which is a clone).
            NodeID base = cloneToBaseMap[o];
            assert(base && "not looking at a clone?!");

            NodeID downCloneId = getCloneObject(base, stmt);
            if (downCloneId == 0) {
                downCloneId = cloneObject(base, stmt, tilde(t));
                if (baseBackPropagate(downCloneId)) {
                    toBackPropagate.insert(downCloneId);
                }
            }

            prop = downCloneId;
            llvm::outs() << "to = down\n";
        } else if (isBase(tilde(t), tp) || tilde(t) == tp || tilde(t) == UNDEF_TYPE) {
            // DEREF-UP
            prop = o;
            llvm::outs() << "to = up\n";
        } else {
            assert(false && "FAILURE!");
        }

        assert(prop && "propagating nothing?!");
        filterPt.set(prop);
    }

    changed = ptrPt != filterPt;
    ptrPt.clear();
    ptrPt |= filterPt;

    for (std::set<NodeID>::iterator nodeI = toBackPropagate.begin(); nodeI != toBackPropagate.end(); ++nodeI) {
        SVFGNode *fromNode = svfg->getSVFGNode(idToAllocLocMap[*nodeI]);
        propagate(&fromNode);
    }

    return changed;
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

    if (chg->getNode(a) == NULL || chg->getNode(b) == NULL) return false;

    const CHGraph::CHNodeSetTy& aChildren = chg->getInstancesAndDescendants(a);
    const CHNode *bNode = chg->getNode(b);
    // If b is in the set of a's children, then a is a base type of b.
    return aChildren.find(bNode) != aChildren.end();
}

TypeClone::TypeStr TypeClone::tilde(TypeStr t) const {
    // Strip one '*' from the end.
    assert(t[t.size() - 1] == '*' && "Not a pointer?");
    return t.substr(0, t.size() - 1);
}

TypeClone::TypeStr TypeClone::T(NodeID n) const {
    if (idToTypeMap.find(n) != idToTypeMap.end()) {
        return idToTypeMap.at(n);
    } else {
        return UNDEF_TYPE;
    }
}

TypeClone::TypeStr TypeClone::staticType(NodeID p) const {
    const PAGNode *pagNode = pag->getPAGNode(p);
    std::string typeName = cppUtil::getNameFromType(pagNode->getType());
    if (typeName == "i8*" || typeName == "i8") typeName = UNDEF_TYPE;
    return typeName;
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

        AddrSVFGNode *addrSvfgNode = SVFUtil::dyn_cast<AddrSVFGNode>(svfgNodeI->second);
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

bool TypeClone::glob(NodeID svfgNodeId) {
    // TODO.
    return false;
}

