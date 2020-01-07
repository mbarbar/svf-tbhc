//===- FlowSensitiveTypeFilter.cpp -- flow-sensitive type filter ------------//

/*
 * FlowSensitiveTypeFilter.cpp
 *
 *  Created on: Oct 08, 2019
 *      Author: Mohamad Barbar
 */

#include <stack>
#include <forward_list>

#include "Util/CPPUtil.h"
#include "WPA/FlowSensitiveTypeFilter.h"
#include "WPA/WPAStat.h"
#include "WPA/Andersen.h"

const DIType *FlowSensitiveTypeFilter::undefType = nullptr;

void FlowSensitiveTypeFilter::analyze(SVFModule svfModule) {
    FlowSensitive::analyze(svfModule);
}

void FlowSensitiveTypeFilter::initialize(SVFModule svfModule) {
    PointerAnalysis::initialize(svfModule);
    AndersenWaveDiff* ander = AndersenWaveDiff::createAndersenWaveDiff(svfModule);
    svfg = memSSA.buildFullSVFG(ander);
    setGraph(svfg);
    stat = new FlowSensitiveStat(this);

    dchg = SVFUtil::dyn_cast<DCHGraph>(chgraph);
    assert(dchg != nullptr && "FSTF: requires DCHGraph");

    determineWhichGepsAreLoads();
}

void FlowSensitiveTypeFilter::finalize(void) {
    FlowSensitive::finalize();
    // ^ Will print call graph stats.

    // Print clones with their types.
    llvm::outs() << "=== Original objects to clones ===\n";
    unsigned total = 0;
    for (std::map<NodeID, std::set<NodeID>>::iterator ocI = objToClones.begin(); ocI != objToClones.end(); ++ocI) {
        NodeID originalObjId = ocI->first;
        std::set<NodeID> clones = ocI->second;
        if (clones.size() == 0) continue;

        total += clones.size();
        llvm::outs() << "  " << originalObjId << " : "
                     << "(" << clones.size() << ")"
                     << "[ ";
        for (std::set<NodeID>::iterator cloneI = clones.begin(); cloneI != clones.end(); ++cloneI) {
            llvm::outs() << *cloneI
                         << "{"
                         << dchg->diTypeToStr(objToType[*cloneI])
                         << "}"
                         << (std::next(cloneI) == clones.end() ? "" : ", ");
        }

        llvm::outs() << " ]\n";
    }

    llvm::outs() << "Total: " << total << "\n";
    llvm::outs() << "==================================\n";

    // getDFPTDataTy()->dumpPTData();
}

bool FlowSensitiveTypeFilter::propAlongIndirectEdge(const IndirectSVFGEdge* edge) {
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

    const PointsTo &filterSet = locToFilterSet[src->getId()];
    for (std::set<NodeID>::iterator oI = edgePtsAndClones.begin(), oEI = edgePtsAndClones.end(); oI != oEI; ++oI) {
        NodeID o = *oI;
        if (filterSet.test(o)) continue;

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

bool FlowSensitiveTypeFilter::propAlongDirectEdge(const DirectSVFGEdge* edge) {
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

bool FlowSensitiveTypeFilter::processAddr(const AddrSVFGNode* addr) {
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
           && "FSTF: addr: already has a type?");
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
        objType = dchg->getTypeFromCTirMetadata(srcNode->getValue());
    }

    objToType[srcID] = objType;
    objToAllocation[srcID] = addr->getId();

    // All the typed versions of srcID.
    std::set<NodeID> clones = objToClones[srcID];
    for (std::set<NodeID>::iterator oI = clones.begin(); oI != clones.end(); ++oI) {
        changed = addPts(addr->getPAGDstNodeID(), *oI) || changed;
        // No need for type stuff these are all clones; they are all typed.
    }

    end = stat->getClk();
    addrTime += (end - start) / TIMEINTERVAL;

    return changed;
}

bool FlowSensitiveTypeFilter::initialise(const SVFGNode *svfgNode, const NodeID pId, const DIType *tildet, bool reuse, bool gep) {
    bool changed = false;

    PointsTo &pPt = getPts(pId);
    // The points-to set we will populate in the loop to fill pPt.
    PointsTo pNewPt;

    PointsTo &filterSet = locToFilterSet[svfgNode->getId()];
    for (PointsTo::iterator oI = pPt.begin(); oI != pPt.end(); ++oI) {
        NodeID o = *oI;
        const DIType *tp = objToType[o];  // tp is t'

        // When an object is field-insensitive, we can't filter on any of the fields' types.
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

        std::set<const DIType *> aggs = dchg->isAgg(tp) ? dchg->getAggs(tp) : std::set<const DIType *>();

        NodeID prop = 0;
        bool filter = false;
        if (fieldInsensitive && std::find(fieldTypes.begin(), fieldTypes.end(), tildet) != fieldTypes.end()) {
            // Field-insensitive object but the instruction is operating on a field.
            prop = o;
        } else if (gep && aggs.find(tildet) != aggs.end()) {
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
            prop = o;
        }

        if (tp == undefType && prop != o) {
            // If we cloned, we want to keep o in p's PTS but filter it (ignore it later).
            pNewPt.set(o);
            filterSet.set(o);
        }

        pNewPt.set(prop);

        if (filter) {
            filterSet.set(o);
        }
    }

    if (pPt != pNewPt) {
        pPt.clear();
        unionPts(pId, pNewPt);
        changed = true;
    }

    return changed;
}

bool FlowSensitiveTypeFilter::processGep(const GepSVFGNode* gep) {
    double start = stat->getClk();

    // Copy of that in FlowSensitive.cpp + some changes.
    NodeID q = gep->getPAGSrcNodeID();

    const DIType *tildet = dchg->getTypeFromCTirMetadata(gep->getInst() ? gep->getInst()
                                                                        : gep->getPAGEdge()->getValue());
    if (tildet != undefType) {
        initialise(gep, q, tildet, !gepIsLoad[gep->getId()], true);
    }

    if (!gep->getPAGEdge()->isPTAEdge()) {
        return false;
    }

    const PointsTo& qPts = getPts(q);
    PointsTo &filterSet = locToFilterSet[gep->getId()];
    PointsTo tmpDstPts;
    for (PointsTo::iterator oqi = qPts.begin(); oqi != qPts.end(); ++oqi) {
        NodeID oq = *oqi;
        if (filterSet.test(oq)) continue;

        if (isBlkObjOrConstantObj(oq)
            || (isClone(oq) && isBlkObjOrConstantObj(cloneToOriginalObj[oq]))) {
            tmpDstPts.set(oq);
        } else {
            if (SVFUtil::isa<VariantGepPE>(gep->getPAGEdge())) {
                setObjFieldInsensitive(oq);
                NodeID fiObj = oq;
                tmpDstPts.set(fiObj);
            } else if (const NormalGepPE* normalGep = SVFUtil::dyn_cast<NormalGepPE>(gep->getPAGEdge())) {
                const DIType *baseType = objToType[oq];

                // TODO: ctir annotations unavailable for field accesses turned into memcpys/memmoves.
                //       A few other things here and there too.
                // Drop down to field insensitive.
                if (baseType == nullptr) {
                    setObjFieldInsensitive(oq);
                    NodeID fiObj = oq;
                    tmpDstPts.set(fiObj);
                    continue;
                }

                // If the field offset is too high for this object, it is killed. It seems that a
                // clone was made on this GEP but this is not the base (e.g. base->f1->f2), and SVF
                // expects to operate on the base (hence the large offset). The base will have been
                // cloned at another GEP and back-propagated, thus it'll reach here safe and sound.
                // We ignore arrays/pointers because those are array accesses/pointer arithmetic we
                // assume are correct.
                // Obviously, non-aggregates cannot have their fields taken so they are spurious.
                if (!DCHGraph::isAgg(baseType)
                    || (baseType->getTag() != dwarf::DW_TAG_array_type && baseType->getTag() != dwarf::DW_TAG_pointer_type
                        && normalGep->getLocationSet().getOffset() >= dchg->getNumFields(baseType))) {
                    filterSet.set(oq);
                    continue;
                }

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

    return unionPts(gep->getPAGDstNodeID(), tmpDstPts);
}

bool FlowSensitiveTypeFilter::processLoad(const LoadSVFGNode* load) {
    double start = stat->getClk();

    preparePtsFromIn(load, load->getPAGSrcNodeID());

    const DIType *tildet = dchg->getTypeFromCTirMetadata(load->getInst() ? load->getInst()
                                                                         : load->getPAGEdge()->getValue());
    if (tildet != undefType) {
        initialise(load, load->getPAGSrcNodeID(), tildet, false);
    }

    // We want to deref. for non-pointer nodes but not process the load.
    if (!load->getPAGEdge()->isPTAEdge()) {
        return false;
    }

    bool changed = false;

    NodeID dstVar = load->getPAGDstNodeID();

    const PointsTo& srcPts = getPts(load->getPAGSrcNodeID());
    const PointsTo &filterSet = locToFilterSet[load->getId()];
    for (PointsTo::iterator ptdIt = srcPts.begin(); ptdIt != srcPts.end(); ++ptdIt) {
        NodeID ptd = *ptdIt;
        if (filterSet.test(ptd)) continue;

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

    double end = stat->getClk();
    loadTime += (end - start) / TIMEINTERVAL;
    return changed;
}

bool FlowSensitiveTypeFilter::processStore(const StoreSVFGNode* store) {
    double start = stat->getClk();

    const DIType *tildet = dchg->getTypeFromCTirMetadata(store->getInst() ? store->getInst()
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

    const PointsTo & dstPts = getPts(store->getPAGDstNodeID());

    /// STORE statement can only be processed if the pointer on the LHS
    /// points to something. If we handle STORE with an empty points-to
    /// set, the OUT set will be updated from IN set. Then if LHS pointer
    /// points-to one target and it has been identified as a strong
    /// update, we can't remove those points-to information computed
    /// before this strong update from the OUT set.
    if (dstPts.empty())
        return false;

    bool changed = false;
    const PointsTo &filterSet = locToFilterSet[store->getId()];
    if(getPts(store->getPAGSrcNodeID()).empty() == false) {
        for (PointsTo::iterator it = dstPts.begin(), eit = dstPts.end(); it != eit; ++it) {
            NodeID ptd = *it;
            if (filterSet.test(ptd)) continue;

            if (pag->isConstantObj(ptd) || pag->isNonPointerObj(ptd))
                continue;

            if (unionPtsFromTop(store, store->getPAGSrcNodeID(), ptd))
                changed = true;
        }
    }

    double end = stat->getClk();
    storeTime += (end - start) / TIMEINTERVAL;

    double updateStart = stat->getClk();
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
    double updateEnd = stat->getClk();
    updateTime += (updateEnd - updateStart) / TIMEINTERVAL;

    return changed;
}

bool FlowSensitiveTypeFilter::processPhi(const PHISVFGNode* phi) {
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

void FlowSensitiveTypeFilter::preparePtsFromIn(const StmtSVFGNode *stmt, NodeID pId) {
    PointsTo &pPt = getPts(pId);
    PointsTo pNewPt;

    const DIType *tildet = dchg->getTypeFromCTirMetadata(stmt->getInst() ? stmt->getInst()
                                                                         : stmt->getPAGEdge()->getValue());
    const PtsMap &ptsInMap = getDFPTDataTy()->getDFInPtsMap(stmt->getId());
    for (PtsMap::value_type kv : ptsInMap) {
        NodeID o = kv.first;
        if (pPt.test(o)) {
            pNewPt.set(o);
        } else if (isClone(o) && pPt.test(cloneToOriginalObj[o])) {
            pNewPt.set(o);
        }
    }

    if (pPt != pNewPt) {
        //pPt.clear();
        unionPts(pId, pNewPt);
    }
}

NodeID FlowSensitiveTypeFilter::cloneObject(NodeID o, const SVFGNode *cloneSite, const DIType *type) {
    if (isClone(o)) o = cloneToOriginalObj[o];

    // Check if a clone of the correct type exists.
    std::set<NodeID> &clones = objToClones[o];
    for (NodeID clone : clones) {
        if (objToType[clone] == type) {
            return clone;
        }
    }

    const PAGNode *obj = pag->getPAGNode(o);
    NodeID clone = o;
    if (const GepObjPN *gepObj = SVFUtil::dyn_cast<GepObjPN>(obj)) {
        clone = pag->addCloneGepObjNode(gepObj->getMemObj(), gepObj->getLocationSet());
        // The base needs to know about this guy.
        objToGeps[gepObj->getBaseNode()].insert(clone);
        memObjToGeps[gepObj->getMemObj()][gepObj->getLocationSet().getOffset()].insert(clone);
        // Since getGepObjClones is updated, some GEP nodes need to be redone.
        for (std::set<NodeID>::iterator svfgNodeI = gepToSVFGRetrievers[o].begin(); svfgNodeI != gepToSVFGRetrievers[o].end(); ++svfgNodeI) {
            pushIntoWorklist(*svfgNodeI);
        }
    } else if (SVFUtil::isa<FIObjPN>(obj) || SVFUtil::isa<DummyObjPN>(obj)) {
        // Otherwise, we create that clone.
        if (const FIObjPN *fiObj = SVFUtil::dyn_cast<FIObjPN>(obj)) {
            clone = pag->addCloneFIObjNode(fiObj->getMemObj());
        } else {
            clone = pag->addCloneObjNode();
        }

        pushIntoWorklist(objToAllocation[o]);
    } else {
        assert(false && "FSTF: trying to clone unhandled object");
    }

    objToType[clone] = type;
    objToAllocation[clone] = objToAllocation[o];

    // Tracking of object<->clone.
    objToClones[o].insert(clone);
    cloneToOriginalObj[clone] = o;

    return clone;
}

bool FlowSensitiveTypeFilter::isBase(const llvm::DIType *a, const llvm::DIType *b) const {
    return dchg->isBase(a, b, true);
}

bool FlowSensitiveTypeFilter::isClone(NodeID o) const {
    return cloneToOriginalObj.find(o) != cloneToOriginalObj.end();
}

std::set<NodeID> FlowSensitiveTypeFilter::getGepObjClones(NodeID base, const LocationSet& ls) {
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

        GepObjPN *gep = SVFUtil::dyn_cast<GepObjPN>(pag->getPAGNode(newGep));
        gep->setBaseNode(base);

        objToGeps[base].insert(newGep);
        const DIType *baseType = objToType[base];
        const DIType *newGepType;
        if (baseType->getTag() == dwarf::DW_TAG_array_type || baseType->getTag() == dwarf::DW_TAG_pointer_type) {
            // Array access.
            if (const DICompositeType *arrayType = SVFUtil::dyn_cast<DICompositeType>(baseType)) {
                newGepType = arrayType->getBaseType();
            } else if (const DIDerivedType *ptrType = SVFUtil::dyn_cast<DIDerivedType>(baseType)) {
                newGepType = arrayType->getBaseType();
            }
        } else {
            // Must be a struct/class.
            newGepType = dchg->getFieldType(objToType[base], ls.getOffset());
        }

        objToType[newGep] = newGepType;
        objToAllocation[newGep] = objToAllocation[base];
        memObjToGeps[baseNode->getMemObj()][ls.getOffset()].insert(newGep);

        geps.insert(newGep);
    }

    return geps;
}

void FlowSensitiveTypeFilter::determineWhichGepsAreLoads(void) {
    for (SVFG::iterator nI = svfg->begin(); nI != svfg->end(); ++nI) {
        SVFGNode *svfgNode = nI->second;
        if (StmtSVFGNode *gep = SVFUtil::dyn_cast<GepSVFGNode>(svfgNode)) {
            if (dchg->getTypeFromCTirMetadata(gep->getInst() ? gep->getInst()
                                                             : gep->getPAGEdge()->getValue())) {
                // Only care about ctir nodes - they have the reuse problem.
                gepIsLoad[gep->getId()] = true;
                for (auto eI = gep->getOutEdges().begin(); eI != gep->getOutEdges().end(); ++eI) {
                    SVFGEdge *e = *eI;
                    SVFGNode *dst = e->getDstNode();

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

void FlowSensitiveTypeFilter::countAliases(std::set<std::pair<NodeID, NodeID>> cmp, unsigned *mayAliases, unsigned *noAliases) {
    std::map<std::pair<NodeID, NodeID>, PointsTo> filteredPts;
    for (std::pair<NodeID, NodeID> locP : cmp) {
        const PointsTo &filterSet = locToFilterSet[locP.first];
        const PointsTo &pts = getPts(locP.second);
        PointsTo &ptsFiltered = filteredPts[locP];

        for (NodeID o : pts) {
            if (filterSet.test(o)) continue;
            ptsFiltered.set(o);
        }
    }

    for (std::pair<NodeID, NodeID> locPA : cmp) {
        NodeID locA = locPA.first;
        NodeID a = locPA.second;

        const PointsTo &aPts = filteredPts[locPA];
        for (std::pair<NodeID, NodeID> locPB : cmp) {
            if (locPB == locPA) continue;
            const PointsTo &bPts = filteredPts[locPB];

            switch (alias(aPts, bPts)) {
            case llvm::NoAlias:
                ++(*noAliases);
                break;
            case llvm::MayAlias:
                ++(*mayAliases);
                break;
            default:
                assert("Not May/NoAlias?");
            }
        }
    }

}
