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
#include <forward_list>

#include "Util/CPPUtil.h"
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

    //buildBackPropagationMap();
    determineWhichGepsAreLoads();
}

void TypeBasedHeapCloning::finalize(void) {
    FlowSensitive::finalize();
    // ^ Will print call graph stats.

    // Print clones with their types.
    /*
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
    */

    // getDFPTDataTy()->dumpPTData();
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
        if (GepObjPN *gep = SVFUtil::dyn_cast<GepObjPN>(pag->getPAGNode(*oI))) {
            // Want the geps which are at the same "level" as this one (same mem obj, same offset).
            const MemObj *memObj = gep->getMemObj();
            unsigned offset = gep->getLocationSet().getOffset();
            edgePtsAndClones.insert(memObjToGeps[memObj][offset].begin(),
                                    memObjToGeps[memObj][offset].end());
        }
    }

    for (std::set<NodeID>::iterator oI = edgePtsAndClones.begin(), oEI = edgePtsAndClones.end(); oI != oEI; ++oI) {
        NodeID o = *oI;
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

    return changed;
}

bool TypeBasedHeapCloning::propAlongDirectEdge(const DirectSVFGEdge* edge) {
    double start = stat->getClk();
    bool changed = false;

    SVFGNode* src = edge->getSrcNode();
    SVFGNode* dst = edge->getDstNode();
    // If this is an actual-param or formal-ret, top-level pointer's pts must be
    // propagated from src to dst.
    if (ActualParmSVFGNode* ap = SVFUtil::dyn_cast<ActualParmSVFGNode>(src)) {
        if (!ap->getParam()->isPointer()) return false;
        changed = propagateFromAPToFP(ap, dst);
    } else if (FormalRetSVFGNode* fp = SVFUtil::dyn_cast<FormalRetSVFGNode>(src)) {
        if (!fp->getRet()->isPointer()) return false;
        changed = propagateFromFRToAR(fp, dst);
    } else {
        // Direct SVFG edge links between def and use of a top-level pointer.
        // There's no points-to information propagated along direct edge.
        // Since the top-level pointer's value has been changed at src node,
        // return TRUE to put dst node into the work list.
        changed = true;
    }

    double end = stat->getClk();
    directPropaTime += (end - start) / TIMEINTERVAL;
    return changed;
}

bool TypeBasedHeapCloning::processAddr(const AddrSVFGNode* addr) {
    double start = stat->getClk();

    NodeID srcID = addr->getPAGSrcNodeID();
    NodeID dstID = addr->getPAGDstNodeID();
    PAGNode *srcNode = addr->getPAGSrcNode();

    double end = stat->getClk();
    addrTime += (end - start) / TIMEINTERVAL;

    if (!addr->getPAGEdge()->isPTAEdge()) return false;

    bool changed = FlowSensitive::processAddr(addr);

    start = stat->getClk();

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

    end = stat->getClk();
    addrTime += (end - start) / TIMEINTERVAL;

    return changed;
}

bool TypeBasedHeapCloning::initialise(const SVFGNode *svfgNode, const NodeID pId, const DIType *tildet, bool reuse) {
    bool changed = false;
    PointsTo &pPt = getPts(pId);
    PointsTo pNewPt;
    const PAGNode *pNode = pag->getPAGNode(pId);
    assert(pNode && "TBHC: dereferencing something not in PAG?");

    for (PointsTo::iterator oI = pPt.begin(); oI != pPt.end(); ++oI) {
        NodeID o = *oI;
        const DIType *tp = objToType[o];  // tp == t'

        // Is this object field insensitive?
        bool fieldInsensitive = false;
        std::vector<const DIType *> fieldTypes;
        if (ObjPN *obj = SVFUtil::dyn_cast<ObjPN>(pag->getPAGNode(o))) {
            fieldInsensitive = obj->getMemObj()->isFieldInsensitive();
            if (tp != nullptr && (tp->getTag() == dwarf::DW_TAG_structure_type
                                  || tp->getTag() == dwarf::DW_TAG_class_type
                                  || tp->getTag() == dwarf::DW_TAG_union_type)) {
                fieldTypes = dchg->getFieldTypes(tp);
            }
        }

        NodeID prop = 0;
        bool filter = false;
        if (fieldInsensitive && std::find(fieldTypes.begin(), fieldTypes.end(), tildet) != fieldTypes.end()) {
            // Field-insensitive object but the instruction is operating on a field.
            prop = o;
        } else if (tp == undefType || (isBase(tp, tildet) && tp != tildet)) {
            // o is unitialised or this is some downcast.
            prop = cloneObject(o, svfgNode, tildet);
        } else if (isBase(tildet, tp)) {
            // We have an upcast.
            prop = o;
        } else if (tildet != tp && reuse) {
            // We have a case of reuse.
            prop = cloneObject(o, svfgNode, tildet);
        } else {
            // Some spurious objects will be filtered.
            filter = true;
        }

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

bool TypeBasedHeapCloning::processGep(const GepSVFGNode* gep) {
    double start = stat->getClk();

    // Copy of that in FlowSensitive.cpp + some changes.
    NodeID q = gep->getPAGSrcNodeID();

    const DIType *tildet = getTypeFromMetadata(gep->getInst() ? gep->getInst()
                                                              : gep->getPAGEdge()->getValue());
    // TODO: is qChanged necessary?
    bool qChanged = false;
    if (tildet != undefType) {
        qChanged = initialise(gep, q, tildet, !gepIsLoad[gep->getId()]);
    }

    if (!gep->getPAGEdge()->isPTAEdge()) {
        return false;
    }

    const PointsTo& qPts = getPts(q);
    PointsTo tmpDstPts;
    for (PointsTo::iterator oqi = qPts.begin(); oqi != qPts.end(); ++oqi) {
        NodeID oq = *oqi;
        if (isBlkObjOrConstantObj(oq)
            || (isClone(oq) && isBlkObjOrConstantObj(cloneToOriginalObj[oq]))) {
            tmpDstPts.set(oq);
        } else {
            if (SVFUtil::isa<VariantGepPE>(gep->getPAGEdge())) {
                setObjFieldInsensitive(oq);
                NodeID fiObj = oq;
                tmpDstPts.set(fiObj);
            } else if (const NormalGepPE* normalGep = SVFUtil::dyn_cast<NormalGepPE>(gep->getPAGEdge())) {
                std::set<NodeID> fieldClones = getGepObjClones(oq, normalGep->getLocationSet());
                for (std::set<NodeID>::iterator fcI = fieldClones.begin(); fcI != fieldClones.end(); ++fcI) {
                    NodeID fc = *fcI;
                    gepToSVFGRetrievers[fc].insert(gep->getId());
                    tmpDstPts.set(fc);
                }
            } else {
                assert(false && "new gep edge?");
            }
        }
    }

    double end = stat->getClk();
    copyGepTime += (end - start) / TIMEINTERVAL;

    return unionPts(gep->getPAGDstNodeID(), tmpDstPts) || qChanged;
}

bool TypeBasedHeapCloning::processLoad(const LoadSVFGNode* load) {
    double start = stat->getClk();

    preparePtsFromIn(load, load->getPAGSrcNodeID());

    const DIType *tildet = getTypeFromMetadata(load->getInst() ? load->getInst()
                                                               : load->getPAGEdge()->getValue());
    if (tildet != undefType) {
        initialise(load, load->getPAGSrcNodeID(), tildet, false);
    }

    // We want to deref. for non-pointer nodes but not process the load.
    if (!load->getPAGEdge()->isPTAEdge()) {
        return false;
    }

    double end = stat->getClk();
    loadTime += (end - start) / TIMEINTERVAL;

    bool changed = FlowSensitive::processLoad(load);
    return changed;
}

bool TypeBasedHeapCloning::processStore(const StoreSVFGNode* store) {
    double start = stat->getClk();

    const DIType *tildet = getTypeFromMetadata(store->getInst() ? store->getInst()
                                                               : store->getPAGEdge()->getValue());
    if (tildet != undefType) {
        initialise(store, store->getPAGDstNodeID(), tildet, true);
    }

    // Like processLoad: we want to deref. for non-pointers but not the store.
    if (!store->getPAGEdge()->isPTAEdge()) {
        // Pass through and return because there may be some PTA nodes
        // relying on this node's parents.
        bool changed = getDFPTDataTy()->updateAllDFOutFromIn(store->getId(), 0, false);
        return changed;
    }

    double end = stat->getClk();
    storeTime += (end - start) / TIMEINTERVAL;

    bool changed = FlowSensitive::processStore(store);
    return changed;
}

bool TypeBasedHeapCloning::processPhi(const PHISVFGNode* phi) {
    if (!phi->isPTANode()) return false;

    if (const Argument *arg = SVFUtil::dyn_cast<Argument>(phi->getRes()->getValue())) {
        // First argument and for a constructor? Clone.
        if (arg->getArgNo() == 0 && cppUtil::isConstructor(arg->getParent())) {
            const DIType *constructorType = dchg->getConstructorType(arg->getParent());
            for (PHISVFGNode::OPVers::const_iterator it = phi->opVerBegin(); it != phi->opVerEnd(); ++it) {
                NodeID src = it->second->getId();
                initialise(phi, src, constructorType, true);
            }
        }
    }

    bool changed = FlowSensitive::processPhi(phi);
    return changed;
}

void TypeBasedHeapCloning::preparePtsFromIn(const StmtSVFGNode *stmt, NodeID pId) {
    PointsTo &pPt = getPts(pId);
    PointsTo pNewPt;

    // TODO: double check ternary.
    const DIType *tildet = getTypeFromMetadata(stmt->getInst() ? stmt->getInst()
                                                               : stmt->getPAGEdge()->getValue());

    const PtsMap &ptsInMap = getDFPTDataTy()->getDFInPtsMap(stmt->getId());
    for (PointsTo::iterator oI = pPt.begin(); oI != pPt.end(); ++oI) {
        NodeID o = *oI;

        /*
        if (isBlkObjOrConstantObj(o)) {
            pNewPt.set(o);
            continue;
        }
        */

        NodeID originalO = isClone(o) ? cloneToOriginalObj[o] : o;
        bool mergeO = false;
        for (NodeID clone : objToClones[originalO]) {
            if (ptsInMap.find(clone) != ptsInMap.end()) {
                pNewPt.set(clone);

                // If o is not a clone, and a clone is coming through of the same type,
                // we can stop propagating the untyped object, because everything that
                // points to the to-be clone will alias everything that points to the
                // incoming clone.
                if (!isClone(o) && tildet == objToType[clone]) {
                    mergeO = true;
                }
            }
        }

        if (!mergeO) {
            pNewPt.set(o);
        }
    }

    if (pPt != pNewPt) {
        pPt.clear();
        unionPts(pId, pNewPt);
    }
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
    if (cloneSiteToClones[cloneSite->getId()].find(o) != cloneSiteToClones[cloneSite->getId()].end()) {
        // If found, it must be the correct type because everything made here is of one type.
        return cloneSiteToClones[cloneSite->getId()].at(o);
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
        addrNodeToBPSet[objToAllocation[o]].insert(clone);
        pushIntoWorklist(objToAllocation[o]);
    } else {
        // Could be a dummy object.
        clone = pag->addCloneObjNode();
        addrNodeToBPSet[objToAllocation[o]].insert(clone);
        pushIntoWorklist(objToAllocation[o]);
    }

    // Clone's attributes.
    objToType[clone] = type;
    objToCloneSite[clone] = cloneSite->getId();
    cloneSiteToClones[cloneSite->getId()][o] = clone;
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
    std::set<NodeID> geps;
    PAGNode *node = pag->getPAGNode(base);
    assert(node);
    ObjPN *baseNode = SVFUtil::dyn_cast<ObjPN>(node);
    assert(baseNode);

    // First field? Just return the whole object; same thing.
    if (ls.getOffset() == 0) {
        geps.insert(base);
        // The base object is the 0 gep object.
        memObjToGeps[baseNode->getMemObj()][0].insert(base);
        return geps;
    }

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
        memObjToGeps[baseNode->getMemObj()][ls.getOffset()].insert(newGep);

        geps.insert(newGep);
    }

    return geps;
}

/// Places all paths from initNode to wherever it may need to back-propagate in paths.
static void getBackPropagationPaths(std::vector<std::vector<NodeID>> &paths,
                                    std::forward_list<std::tuple<SVFGNode *, std::vector<NodeID>, std::set<NodeID>>> &todoList) {
    while (!todoList.empty()) {
        SVFGNode *currNode = std::get<0>(todoList.front());
        std::vector<NodeID> currPath = std::get<1>(todoList.front());
        std::set<NodeID> currSeen = std::get<2>(todoList.front());
        todoList.pop_front();

        currPath.push_back(currNode->getId());
        currSeen.insert(currNode->getId());

        std::set<SVFGNode *> nextNodes;
        // Just because we didn't add any nodes doesn't mean we are at the end of a path. If we don't
        // add any nodes because of a loop or GEP -> not path end; because of a ret edge -> path end.
        // No nodes to follow anymore (should be at addr, TODO: what about CHI?) -> path end.
        bool pathEnd = currNode->getInEdges().empty();
        for (auto inEdgeI = currNode->getInEdges().begin(); inEdgeI != currNode->getInEdges().end(); ++inEdgeI) {
            // Don't cross returns. Don't go through GEPs. Don't follow loops.
            if (SVFUtil::isa<RetDirSVFGEdge>(*inEdgeI) || SVFUtil::isa<RetIndSVFGEdge>(*inEdgeI)) {
                pathEnd = true;
            } else if (!(SVFUtil::isa<GepSVFGNode>((*inEdgeI)->getSrcNode()))
                       && currSeen.find((*inEdgeI)->getSrcNode()->getId()) == currSeen.end()) {
                nextNodes.insert((*inEdgeI)->getSrcNode());
            }
        }

        if (pathEnd) {
            paths.push_back(currPath);
        } else {
            for (std::set<SVFGNode *>::iterator nextI = nextNodes.begin(); nextI != nextNodes.end(); ++nextI) {
                todoList.push_front(std::make_tuple(*nextI, currPath, currSeen));
            }
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
                    std::forward_list<std::tuple<SVFGNode *, std::vector<NodeID>, std::set<NodeID>>> todoList;
                    todoList.push_front(std::make_tuple(stmtNode, std::vector<NodeID>(), std::set<NodeID>()));
                    getBackPropagationPaths(paths, todoList);
                }
            }
        }
    }

    /*
    for (auto vv = paths.begin(); vv != paths.end(); ++vv) {
        llvm::outs() << "[ ";
        for (auto vi = vv->begin(); vi != vv->end(); ++vi) {
            llvm::outs() << *vi << ", ";
        }
        llvm::outs() << " ]\n";
    }
    */
}

void TypeBasedHeapCloning::determineWhichGepsAreLoads(void) {
    for (SVFG::iterator nI = svfg->begin(); nI != svfg->end(); ++nI) {
        SVFGNode *svfgNode = nI->second;
        if (StmtSVFGNode *gep = SVFUtil::dyn_cast<GepSVFGNode>(svfgNode)) {
            if (getTypeFromMetadata(gep->getInst() ? gep->getInst()
                                                   : gep->getPAGEdge()->getValue())) {
                // Only care about ctir nodes - they have the reuse problem.
                gepIsLoad[gep->getId()] = true;
                for (auto eI = gep->getOutEdges().begin(); eI != gep->getOutEdges().end(); ++eI) {
                    SVFGEdge *e = *eI;
                    SVFGNode *dst = e->getDstNode();

                    // TODO: loops don't count?
                    if (gep == dst) continue;

                    if (!SVFUtil::isa<LoadSVFGNode>(dst)) {
                        gepIsLoad[gep->getId()] = false;
                        continue;
                    }
                }
            }
        }
    }
}

