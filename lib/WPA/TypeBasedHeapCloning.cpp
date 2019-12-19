//===- TypeBasedHeapCloning.cpp -- Type-based flow-sensitive heap cloning------------//

/*
 * TypeBasedHeapCloning.cpp
 *
 *  Created on: Oct 08, 2019
 *      Author: Mohamad Barbar
 */

// TODO: rename CloneObjNode -> ConstantCloneNode
// TODO: Deref function in rules
// DONE: cloneObject, use original always
// TODO: reference set for perf., not return set
// TODO: back-propagating geps incorrectly.

#include <stack>

#include "WPA/TypeBasedHeapCloning.h"
#include "WPA/WPAStat.h"
#include "WPA/Andersen.h"

// TODO: maybe better to actually construct something.
const DIType *TypeBasedHeapCloning::undefType = nullptr;

void TypeBasedHeapCloning::analyze(SVFModule svfModule) {
    // TODO: unclear if this will need to change.
    FlowSensitive::analyze(svfModule);
}

void TypeBasedHeapCloning::initialize(SVFModule svfModule) {
    PointerAnalysis::initialize(svfModule);
    AndersenWaveDiff* ander = AndersenWaveDiff::createAndersenWaveDiff(svfModule);
    svfg = memSSA.buildFullSVFG(ander);
    setGraph(svfg);
    stat = new FlowSensitiveStat(this);

    dchg = SVFUtil::dyn_cast<DCHGraph>(chgraph);
    assert(dchg != nullptr && "TBHC: requires DCHGraph");

    buildBackPropagationMap();
}

void TypeBasedHeapCloning::finalize(void) {
    FlowSensitive::finalize();

    // Print clones with their types.
    llvm::outs() << "=== Original objects to clones ===\n";
    for (std::map<NodeID, std::set<NodeID>>::iterator ocI = objToClones.begin(); ocI != objToClones.end(); ++ocI) {
        NodeID originalObjId = ocI->first;
        std::set<NodeID> clones = ocI->second;
        llvm::outs() << "  " << originalObjId << " : "
                     << "(" << clones.size() << ")"
                     << "[ ";
        for (std::set<NodeID>::iterator cloneI = clones.begin(); cloneI != clones.end(); ++cloneI) {
            llvm::outs() << *cloneI
                         << "{"
                         << dchg->diTypeToStr(objToType[*cloneI])
                         << ":"
                         << objToCloneSite[*cloneI]
                         << "}"
                         << (std::next(cloneI) == clones.end() ? "" : ", ");
        }

        llvm::outs() << " ]\n";
    }

    llvm::outs() << "==================================\n";

    getDFPTDataTy()->dumpPTData();
}

bool TypeBasedHeapCloning::propAlongIndirectEdge(const IndirectSVFGEdge* edge) {
    SVFGNode* src = edge->getSrcNode();
    SVFGNode* dst = edge->getDstNode();

    bool changed = false;

    // Get points-to targets may be used by next SVFG node.
    // Propagate points-to set for node used in dst.
    const PointsTo& pts = edge->getPointsTo();
    std::set<NodeID> edgePtsAndClones;
    for (PointsTo::iterator oI = pts.begin(), oEI = pts.end(); oI != oEI; ++oI) {
        edgePtsAndClones.insert(*oI);
        edgePtsAndClones.insert(objToClones[*oI].begin(), objToClones[*oI].end());
    }

    for (std::set<NodeID>::iterator oI = edgePtsAndClones.begin(), oEI = edgePtsAndClones.end(); oI != oEI; ++oI) {
        NodeID o = *oI;
        llvm::errs() << "  src: " << src->getId()
                     << " dst: " << dst->getId()
                     << " o: " << o
                     << "\n";;

        if (propVarPtsFromSrcToDst(o, src, dst))
            changed = true;

        if (isFIObjNode(o)) {
            /// If this is a field-insensitive obj, propagate all field node's pts
            const NodeBS& allFields = getAllFieldsObjNode(o);
            for (NodeBS::iterator fieldIt = allFields.begin(), fieldEit = allFields.end();
                    fieldIt != fieldEit; ++fieldIt) {
                if (propVarPtsFromSrcToDst(*fieldIt, src, dst))
                    changed = true;
            }
        }
    }

    llvm::outs() << "changed = " << changed << "\n";

    return changed;
}

bool TypeBasedHeapCloning::processAddr(const AddrSVFGNode* addr) {
    NodeID srcID = addr->getPAGSrcNodeID();
    NodeID dstID = addr->getPAGDstNodeID();
    PAGNode *srcNode = addr->getPAGSrcNode();

    bool changed = FlowSensitive::processAddr(addr);

    // We should not have any type, not even undefined.
    // This all assumes that there is only one outgoing edge from each object.
    // Some of the constant objects have more, so we make that exception.
    /* Can't have this because of back-propagation.
    assert((objToType.find(srcID) == objToType.end() || !SVFUtil::isa<DummyObjPN>(srcNode))
           && "TBHC: addr: already has a type?");
     */

    const DIType *objType;
    if (isHeapMemObj(srcID)) {
        objType = undefType;
    } else if (pag->isConstantObj(srcID)) {
        // Probably constants that have been merged into one.
        // We make it undefined even though it's technically a global
        // to keep in line with SVF's design.
        // This will end up splitting into one for each type of constant.
        objType = undefType;
    } else {
        // Stack/global.
        objType = getTypeFromMetadata(srcNode->getValue());
    }

    objToType[srcID] = objType;
    objToAllocation[srcID] = addr->getId();

    std::set<NodeID> bpClones = addrNodeToBPSet[addr->getId()];
    for (std::set<NodeID>::iterator oI = bpClones.begin(); oI != bpClones.end(); ++oI) {
        changed = addPts(addr->getPAGDstNodeID(), *oI) || changed;
        // No need for type stuff these are all clones; they are all typed.
    }

    return changed;
}

bool TypeBasedHeapCloning::processDeref(const StmtSVFGNode *stmt, const NodeID pId) {
    bool changed = false;
    PointsTo &pPt = getPts(pId);
    PointsTo pNewPt;
    const PAGNode *pNode = pag->getPAGNode(pId);
    assert(pNode && "TBHC: dereferencing something not in PAG?");
    // TODO: double check ternary.
    const DIType *tildet = getTypeFromMetadata(stmt->getInst() ? stmt->getInst()
                                                               : stmt->getPAGEdge()->getValue());
    if (tildet == undefType) return false;

    for (PointsTo::iterator oI = pPt.begin(); oI != pPt.end(); ++oI) {
        NodeID o = *oI;
        const DIType *tp = objToType[o];  // tp == t'

        NodeID prop = 0;
        bool filter = false;
        if (tp == undefType || (isBase(tp, tildet) && tp != tildet)) {
            prop = cloneObject(o, stmt, tildet);
        } else {
            prop = o;
        }

        // TODO: filter unimplemented.
        if (!filter) {
            pNewPt.set(prop);
        }
    }

    if (pPt != pNewPt) {
        pPt.clear();
        unionPts(pId, pNewPt);
        changed = true;
    }

    return changed;
}

bool TypeBasedHeapCloning::processGep(const GepSVFGNode* edge) {
    // Copy of that in FlowSensitive.cpp + some changes.
    NodeID q = edge->getPAGSrcNodeID();
    bool qChanged = processDeref(edge, q);
    const PointsTo& qPts = getPts(q);

    PointsTo tmpDstPts;
    for (PointsTo::iterator oqi = qPts.begin(); oqi != qPts.end(); ++oqi) {
        NodeID oq = *oqi;
        if (isBlkObjOrConstantObj(oq)
            || (isClone(oq) && isBlkObjOrConstantObj(cloneToOriginalObj[oq]))) {
            tmpDstPts.set(oq);
        } else {
            // Even though, in LLVM IR, oq was not loaded/stored, assumptions are
            // being made about its type based on q.
            if (SVFUtil::isa<VariantGepPE>(edge->getPAGEdge())) {
                setObjFieldInsensitive(oq);
                NodeID fiObj = getFIObjNode(oq);
                tmpDstPts.set(fiObj);

                // TODO: check type!
                objToType[fiObj] = objToType.at(oq);
            } else if (const NormalGepPE* normalGep = SVFUtil::dyn_cast<NormalGepPE>(edge->getPAGEdge())) {
                std::set<NodeID> fieldClones = getGepObjClones(oq, normalGep->getLocationSet());
                for (std::set<NodeID>::iterator fcI = fieldClones.begin(); fcI != fieldClones.end(); ++fcI) {
                    NodeID fc = *fcI;
                    gepToSVFGRetrievers[fc].insert(edge->getId());
                    tmpDstPts.set(fc);
                }
            } else {
                assert(false && "new gep edge?");
            }
        }
    }

    return unionPts(edge->getPAGDstNodeID(), tmpDstPts) || qChanged;
}

bool TypeBasedHeapCloning::processLoad(const LoadSVFGNode* load) {
    processDeref(load, load->getPAGSrcNodeID());
    return FlowSensitive::processLoad(load);
}

bool TypeBasedHeapCloning::processStore(const StoreSVFGNode* store) {
    processDeref(store, store->getPAGDstNodeID());
    return FlowSensitive::processStore(store);
}

const DIType *TypeBasedHeapCloning::getTypeFromMetadata(const Value *v) const {
    assert(v != nullptr && "TBHC: trying to get metadata from nullptr!");

    const MDNode *mdNode = nullptr;
    if (const Instruction *inst = SVFUtil::dyn_cast<Instruction>(v)) {
        mdNode = inst->getMetadata(SVFModule::ctirMetadataName);
    } else if (const GlobalObject *go = SVFUtil::dyn_cast<GlobalObject>(v)) {
        mdNode = go->getMetadata(SVFModule::ctirMetadataName);
    }

    if (mdNode == nullptr) {
        return nullptr;
    }

    const DIType *type = SVFUtil::dyn_cast<DIType>(mdNode);
    if (type == nullptr) {
        llvm::errs() << "TBHC: bad ctir metadata type\n";
        return nullptr;
    }

    return dchg->getCanonicalType(type);
}

NodeID TypeBasedHeapCloning::cloneObject(NodeID o, const SVFGNode *cloneSite, const DIType *type) {
    if (isClone(o)) o = cloneToOriginalObj[o];
    // Check the desired clone doesn't already exist.
    if (cloneSiteToClone.find(cloneSite->getId()) != cloneSiteToClone.end()) {
        // It must be the correct type because everything made here is of one type.
        return cloneSiteToClone[cloneSite->getId()];
    }

    // CloneObjs for standard objects, CloneGepObjs for GepObjs, CloneFIObjs for FIObjs.
    const PAGNode *obj = pag->getPAGNode(o);
    NodeID clone;
    if (const GepObjPN *gepObj = SVFUtil::dyn_cast<GepObjPN>(obj)) {
        clone = pag->addCloneGepObjNode(gepObj->getMemObj(), gepObj->getLocationSet());
        // The base needs to know about this guy.
        objToGeps[gepObj->getMemObj()->getSymId()].insert(clone);
        // Since getGepObjClones is updated, some GEP nodes need to be redone.
        for (std::set<NodeID>::iterator svfgNodeI = gepToSVFGRetrievers[o].begin(); svfgNodeI != gepToSVFGRetrievers[o].end(); ++svfgNodeI) {
            pushIntoWorklist(*svfgNodeI);
        }
    } else if (const FIObjPN *fiObj = SVFUtil::dyn_cast<FIObjPN>(obj)) {
        clone = pag->addCloneFIObjNode(fiObj->getMemObj());
    } else {
        // Could be a dummy object.
        clone = pag->addCloneObjNode();
    }

    // Clone's attributes.
    objToType[clone] = type;
    objToCloneSite[clone] = cloneSite->getId();
    cloneSiteToClone[cloneSite->getId()] = clone;
    // Same allocation site as the original object.
    objToAllocation[clone] = objToAllocation[o];

    // Tracking of object<->clone.
    objToClones[o].insert(clone);
    cloneToOriginalObj[clone] = o;

    return clone;
}

bool TypeBasedHeapCloning::isBase(const llvm::DIType *a, const llvm::DIType *b) const {
    return dchg->isBase(a, b, true);
}

bool TypeBasedHeapCloning::isClone(NodeID o) const {
    return cloneToOriginalObj.find(o) != cloneToOriginalObj.end();
}

std::set<NodeID> TypeBasedHeapCloning::getGepObjClones(NodeID base, const LocationSet& ls) {
    PAGNode *node = pag->getPAGNode(base);
    assert(node);
    ObjPN *baseNode = SVFUtil::dyn_cast<ObjPN>(node);
    assert(baseNode);

    std::set<NodeID> geps;
    if (baseNode->getMemObj()->isFieldInsensitive()) {
        // If it's field-insensitive, well base represents everything.
        geps.insert(base);
        return geps;
    }

    // TODO: might need to cache on ls for performance.
    for (std::set<NodeID>::iterator gepI = objToGeps[base].begin(); gepI != objToGeps[base].end(); ++gepI) {
        NodeID gep = *gepI;
        PAGNode *node = pag->getPAGNode(gep);
        assert(node && "gep node doesn't exist?");
        assert(SVFUtil::isa<GepObjPN>(node) || SVFUtil::isa<FIObjPN>(node));

        if (GepObjPN *gepNode = SVFUtil::dyn_cast<GepObjPN>(node)) {
            // TODO: is it enough to just compare offsets?
            if (gepNode->getLocationSet().getOffset() == ls.getOffset()) {
                geps.insert(gep);
            }
        } else {
            // Definitely a FIObj.
            geps.insert(gep);
        }
    }

    if (geps.empty()) {
        // No gep node has even be created, so create one.
        NodeID newGep;
        if (isClone(base)) {
            newGep = pag->addCloneGepObjNode(baseNode->getMemObj(), ls);
        } else {
            newGep = pag->getGepObjNode(base, ls);
        }

        objToGeps[base].insert(newGep);
        objToType[newGep] = dchg->getFieldType(objToType[base], ls.getOffset());
        objToAllocation[newGep] = objToAllocation[base];
        geps.insert(newGep);
    }

    return geps;
}

/// Places all paths from initNode to wherever it may need to back-propagate in paths.
static void getBackPropagationPaths(SVFGNode *node, std::vector<std::vector<NodeID>> &paths,
                                    std::vector<NodeID> currPath=std::vector<NodeID>()) {
    currPath.push_back(node->getId());

    std::set<SVFGNode *> nextNodes;
    for (auto inEdgeI = node->getInEdges().begin(); inEdgeI != node->getInEdges().end(); ++inEdgeI) {
        // Don't cross returns. Don't go through GEPs.
        if (!(SVFUtil::isa<RetDirSVFGEdge>(*inEdgeI) || SVFUtil::isa<RetIndSVFGEdge>(*inEdgeI))
            && !(SVFUtil::isa<GepSVFGNode>((*inEdgeI)->getSrcNode()))) {
            nextNodes.insert((*inEdgeI)->getSrcNode());
        }
    }

    if (nextNodes.empty()) {
        paths.push_back(currPath);
    } else {
        for (std::set<SVFGNode *>::iterator nodeI = nextNodes.begin(); nodeI != nextNodes.end(); ++nodeI) {
            getBackPropagationPaths(*nodeI, paths, currPath);
        }
    }
}

void TypeBasedHeapCloning::buildBackPropagationMap(void) {
    std::vector<std::vector<NodeID>> paths;
    for (SVFG::iterator nI = svfg->begin(); nI != svfg->end(); ++nI) {
        SVFGNode *svfgNode = nI->second;
        if (StmtSVFGNode *stmtNode = SVFUtil::dyn_cast<StmtSVFGNode>(svfgNode)) {
            if (SVFUtil::isa<LoadSVFGNode>(stmtNode) || SVFUtil::isa<StoreSVFGNode>(stmtNode)
                || SVFUtil::isa<GepSVFGNode>(stmtNode)) {
                if (getTypeFromMetadata(stmtNode->getInst() ? stmtNode->getInst()
                                                            : stmtNode->getPAGEdge()->getValue())) {
                    // Only nodes which have ctir metadata can possibly be used as
                    // initialisation points.
                    getBackPropagationPaths(stmtNode, paths);
                }
            }
        }
    }

    for (auto vv = paths.begin(); vv != paths.end(); ++vv) {
        llvm::outs() << "[ ";
        for (auto vi = vv->begin(); vi != vv->end(); ++vi) {
            llvm::outs() << *vi << ", ";
        }
        llvm::outs() << " ]\n";
    }
}

