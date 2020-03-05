//===- FlowSensitiveTBHC.cpp -- flow-sensitive type filter ------------//

/*
 * FlowSensitiveTBHC.cpp
 *
 *  Created on: Oct 08, 2019
 *      Author: Mohamad Barbar
 */

#include "MemoryModel/DCHG.h"
#include "Util/CPPUtil.h"
#include "WPA/FlowSensitiveTBHC.h"
#include "WPA/WPAStat.h"
#include "WPA/Andersen.h"

/// Whether we allow reuse for TBHC.
static llvm::cl::opt<bool> TBHCReuse("tbhc-reuse", llvm::cl::init(false), llvm::cl::desc("Allow for object reuse in TBHC"));

FlowSensitiveTBHC::FlowSensitiveTBHC(PTATY type) : FlowSensitive(type), TypeBasedHeapCloning(this, TBHCReuse) {
    // Using `this` as the argument for TypeBasedHeapCloning is okay. As PointerAnalysis, it's
    // already constructed. TypeBasedHeapCloning also doesn't use pta in the constructor so it
    // just needs to be allocated, which it is.
}

void FlowSensitiveTBHC::analyze(SVFModule svfModule) {
    FlowSensitive::analyze(svfModule);
}

void FlowSensitiveTBHC::initialize(SVFModule svfModule) {
    PointerAnalysis::initialize(svfModule);
    AndersenWaveDiff* ander = AndersenWaveDiff::createAndersenWaveDiff(svfModule);
    svfg = memSSA.buildFullSVFG(ander);
    setGraph(svfg);
    stat = new FlowSensitiveStat(this);

    DCHGraph *dchg = SVFUtil::dyn_cast<DCHGraph>(getCHGraph());
    assert(dchg != nullptr && "FSTBHC: DCHGraph required!");

    TypeBasedHeapCloning::setDCHG(dchg);
    TypeBasedHeapCloning::setPAG(pag);

    determineWhichGepsAreLoads();
}

void FlowSensitiveTBHC::finalize(void) {
    FlowSensitive::finalize();
    // ^ Will print call graph and alias stats.

    dumpStats();
    // getDFPTDataTy()->dumpPTData();

    validateTBHCTests(svfMod);
}

void FlowSensitiveTBHC::backPropagate(NodeID clone) {
    PAGNode *cloneObj = pag->getPAGNode(clone);
    assert(cloneObj && "FSTBHC: clone does not exist in PAG?");
    if (SVFUtil::isa<CloneFIObjPN>(cloneObj) || SVFUtil::isa<CloneObjPN>(cloneObj)) {
        pushIntoWorklist(getAllocationSite(getOriginalObj(clone)));
    } else if (SVFUtil::isa<CloneGepObjPN>(cloneObj)) {
        // Since getGepObjClones is updated, some GEP nodes need to be redone.
        const NodeBS &retrievers = gepToSVFGRetrievers[getOriginalObj(clone)];
        for (NodeID r : retrievers) {
            pushIntoWorklist(r);
        }
    } else {
        assert(false && "FSTBHC: unexpected object type?");
    }
}

bool FlowSensitiveTBHC::propAlongIndirectEdge(const IndirectSVFGEdge* edge) {
    SVFGNode* src = edge->getSrcNode();
    SVFGNode* dst = edge->getDstNode();

    bool changed = false;

    // Get points-to targets may be used by next SVFG node.
    // Propagate points-to set for node used in dst.
    const PointsTo& pts = edge->getPointsTo();

    // Since the base Andersen's analysis does NOT perform type-based heap cloning,
    // it uses only the base objects; we want to account for clones too.
    PointsTo edgePtsAndClones;

    // TODO: the conditional bool may be unnecessary.
    // Adding all clones is redundant, and introduces too many calls to propVarPts...
    // This + preparePtsFromIn introduces performance and precision penalties.
    // We should filter out according to src.
    bool isStore = false;
    const DIType *tildet = nullptr;
    if (const StoreSVFGNode *store = SVFUtil::dyn_cast<StoreSVFGNode>(src)) {
        tildet = getTypeFromCTirMetadata(store);
        isStore = true;
    }

    for (NodeID o : pts) {
        edgePtsAndClones.set(o);
        for (NodeID c : getClones(o)) {
            if (!isStore || isBase(tildet, getType(c))) {
                edgePtsAndClones.set(c);
            }
        }

        if (GepObjPN *gep = SVFUtil::dyn_cast<GepObjPN>(pag->getPAGNode(o))) {
            // Want the geps which are at the same "level" as this one (same mem obj, same offset).
            const NodeBS &geps = getGepObjsFromMemObj(gep->getMemObj(), gep->getLocationSet().getOffset());
            for (NodeID g : geps) {
                if (!isStore || getType(g) == nullptr || isBase(tildet, getType(g))) {
                    edgePtsAndClones.set(g);
                }
            }
        }
    }

    const PointsTo &filterSet = getFilterSet(src->getId());
    for (NodeID o : edgePtsAndClones) {
        if (filterSet.test(o)) continue;

        if (propVarPtsFromSrcToDst(o, src, dst))
            changed = true;

        if (isFIObjNode(o)) {
            /// If this is a field-insensitive obj, propagate all field node's pts
            const NodeBS &allFields = getAllFieldsObjNode(o);
            for (NodeID f : allFields) {
                if (propVarPtsFromSrcToDst(f, src, dst))
                    changed = true;
            }
        }
    }

    return changed;
}

bool FlowSensitiveTBHC::propAlongDirectEdge(const DirectSVFGEdge* edge) {
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

bool FlowSensitiveTBHC::processAddr(const AddrSVFGNode* addr) {
    double start = stat->getClk();

    NodeID srcID = addr->getPAGSrcNodeID();
    NodeID dstID = addr->getPAGDstNodeID();
    PAGNode *srcNode = addr->getPAGSrcNode();

    double end = stat->getClk();
    addrTime += (end - start) / TIMEINTERVAL;

    if (!addr->getPAGEdge()->isPTAEdge()) return false;

    bool changed = FlowSensitive::processAddr(addr);

    start = stat->getClk();

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
        objType = getTypeFromCTirMetadata(addr);
    }

    setType(srcID, objType);
    setAllocationSite(srcID, addr->getId());

    // All the typed versions of srcID. This handles back-propagation.
    const NodeBS &clones = getClones(srcID);
    for (NodeID c : clones) {
        changed = addPts(addr->getPAGDstNodeID(), c) || changed;
        // No need for typing these are all clones; they are all typed.
    }

    end = stat->getClk();
    addrTime += (end - start) / TIMEINTERVAL;

    return changed;
}

bool FlowSensitiveTBHC::processGep(const GepSVFGNode* gep) {
    // Copy of that in FlowSensitive.cpp + some changes.
    double start = stat->getClk();

    NodeID q = gep->getPAGSrcNodeID();

    const DIType *tildet = getTypeFromCTirMetadata(gep);
    if (tildet != undefType) {
        init(gep->getId(), q, tildet, !gepIsLoad[gep->getId()], true);
    }

    if (!gep->getPAGEdge()->isPTAEdge()) {
        return false;
    }

    const PointsTo& qPts = getPts(q);
    PointsTo &filterSet = getFilterSet(gep->getId());
    PointsTo tmpDstPts;
    for (NodeID oq : qPts) {
        if (filterSet.test(oq)) continue;

        if (TypeBasedHeapCloning::isBlkObjOrConstantObj(oq)) {
            tmpDstPts.set(oq);
        } else {
            if (SVFUtil::isa<VariantGepPE>(gep->getPAGEdge())) {
                setObjFieldInsensitive(oq);
                NodeID fiObj = oq;
                tmpDstPts.set(fiObj);
            } else if (const NormalGepPE* normalGep = SVFUtil::dyn_cast<NormalGepPE>(gep->getPAGEdge())) {
                const DIType *baseType = getType(oq);

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

                // Operate on the field and all its clones.
                const NodeBS fieldClones = getGepObjClones(oq, normalGep->getLocationSet());
                for (NodeID fc : fieldClones) {
                    gepToSVFGRetrievers[getOriginalObj(fc)].set(gep->getId());
                    tmpDstPts.set(fc);
                }
            } else {
                assert(false && "FSTBHC: new gep edge?");
            }
        }
    }

    double end = stat->getClk();
    copyGepTime += (end - start) / TIMEINTERVAL;

    return unionPts(gep->getPAGDstNodeID(), tmpDstPts);
}

bool FlowSensitiveTBHC::processLoad(const LoadSVFGNode* load) {
    double start = stat->getClk();

    preparePtsFromIn(load, load->getPAGSrcNodeID());

    const DIType *tildet = getTypeFromCTirMetadata(load);
    if (tildet != undefType) {
        init(load->getId(), load->getPAGSrcNodeID(), tildet, false);
    }

    // We want to perform the initialisation for non-pointer nodes but not process the load.
    if (!load->getPAGEdge()->isPTAEdge()) {
        return false;
    }

    bool changed = false;

    NodeID dstVar = load->getPAGDstNodeID();

    const PointsTo& srcPts = getPts(load->getPAGSrcNodeID());
    const PointsTo &filterSet = getFilterSet(load->getId());
    for (NodeID ptd : srcPts) {
        if (filterSet.test(ptd)) continue;

        if (pag->isConstantObj(ptd) || pag->isNonPointerObj(ptd))
            continue;

        if (unionPtsFromIn(load, ptd, dstVar))
            changed = true;

        if (isFIObjNode(ptd)) {
            /// If the ptd is a field-insensitive node, we should also get all field nodes'
            /// points-to sets and pass them to pagDst.
            const NodeBS &allFields = getAllFieldsObjNode(ptd);
            for (NodeID f : allFields) {
                if (unionPtsFromIn(load, f, dstVar))
                    changed = true;
            }
        }
    }

    double end = stat->getClk();
    loadTime += (end - start) / TIMEINTERVAL;
    return changed;
}

bool FlowSensitiveTBHC::processStore(const StoreSVFGNode* store) {
    double start = stat->getClk();

    const DIType *tildet = getTypeFromCTirMetadata(store);
    if (tildet != undefType) {
        init(store->getId(), store->getPAGDstNodeID(), tildet, true);
    }

    // Like processLoad: we want to perform initialisation for non-pointers but not the store.
    if (!store->getPAGEdge()->isPTAEdge()) {
        // Pass through and return because there may be some pointer nodes
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
    const PointsTo &filterSet = getFilterSet(store->getId());
    if(getPts(store->getPAGSrcNodeID()).empty() == false) {
        for (NodeID ptd : dstPts) {
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

bool FlowSensitiveTBHC::processPhi(const PHISVFGNode* phi) {
    if (!phi->isPTANode()) return false;
    bool changed = FlowSensitive::processPhi(phi);
    return changed;
}

/// Returns whether this instruction initialisates an object's
/// vtable (metadata: ctir.vt.init). Returns the object's type,
/// otherwise, nullptr.
static const DIType *getVTInitType(const CopySVFGNode *copy, DCHGraph *dchg) {
    if (copy->getInst() == nullptr) return nullptr;
    const Instruction *inst = copy->getInst();

    const MDNode *mdNode = inst->getMetadata(cppUtil::ctir::vtInitMDName);
    if (mdNode == nullptr) return nullptr;

    const DIType *type = SVFUtil::dyn_cast<DIType>(mdNode);
    assert(type != nullptr && "TBHC: bad ctir.vt.init metadata");
    return dchg->getCanonicalType(type);
}

bool FlowSensitiveTBHC::processCopy(const CopySVFGNode* copy) {
    const DIType *vtInitType = getVTInitType(copy, dchg);
    if (vtInitType != nullptr) {
        // Setting the virtual table pointer.
        init(copy->getId(), copy->getPAGSrcNodeID(), vtInitType, true);
    }

    return FlowSensitive::processCopy(copy);
}

const NodeBS& FlowSensitiveTBHC::getAllFieldsObjNode(NodeID id) {
    return getGepObjs(id);
}

void FlowSensitiveTBHC::preparePtsFromIn(const StmtSVFGNode *stmt, NodeID pId) {
    PointsTo &pPt = getPts(pId);
    PointsTo pNewPt;

    const DIType *tildet = getTypeFromCTirMetadata(stmt);
    const PtsMap &ptsInMap = getDFPTDataTy()->getDFInPtsMap(stmt->getId());
    for (PtsMap::value_type kv : ptsInMap) {
        NodeID o = kv.first;
        if (pPt.test(o)) {
            // Exact match between object in in's set and p's set.
            pNewPt.set(o);
        } else if (isClone(o) && pPt.test(getOriginalObj(o))) {
            // Clone of an object in p's set is in in's set.
            pNewPt.set(o);
        }
    }

    if (pPt != pNewPt) {
        unionPts(pId, pNewPt);
    }
}


void FlowSensitiveTBHC::determineWhichGepsAreLoads(void) {
    for (SVFG::iterator nI = svfg->begin(); nI != svfg->end(); ++nI) {
        SVFGNode *svfgNode = nI->second;
        if (const StmtSVFGNode *gep = SVFUtil::dyn_cast<GepSVFGNode>(svfgNode)) {
            if (getTypeFromCTirMetadata(gep)) {
                // Only care about ctir nodes - they have the reuse problem.
                gepIsLoad[gep->getId()] = true;
                for (auto eI = gep->getOutEdges().begin(); eI != gep->getOutEdges().end(); ++eI) {
                    SVFGEdge *e = *eI;
                    SVFGNode *dst = e->getDstNode();

                    // Loop on itself - don't care.
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

const MDNode *FlowSensitiveTBHC::getRawCTirMetadata(const SVFGNode *s) {
    if (const StmtSVFGNode *stmt = SVFUtil::dyn_cast<StmtSVFGNode>(s)) {
        const Value *v = stmt->getInst() ? stmt->getInst() : stmt->getPAGEdge()->getValue();
        if (v != nullptr) {
            return TypeBasedHeapCloning::getRawCTirMetadata(v);
        }
    }

    return nullptr;
}

const DIType *FlowSensitiveTBHC::getTypeFromCTirMetadata(const SVFGNode *s) {
    if (const StmtSVFGNode *stmt = SVFUtil::dyn_cast<StmtSVFGNode>(s)) {
        const Value *v = stmt->getInst() ? stmt->getInst() : stmt->getPAGEdge()->getValue();
        if (v != nullptr) {
            return TypeBasedHeapCloning::getTypeFromCTirMetadata(v);
        }
    }

    return nullptr;
}

void FlowSensitiveTBHC::countAliases(std::set<std::pair<NodeID, NodeID>> cmp, unsigned *mayAliases, unsigned *noAliases) {
    std::map<std::pair<NodeID, NodeID>, PointsTo> filteredPts;
    for (std::pair<NodeID, NodeID> locP : cmp) {
        const PointsTo &filterSet = getFilterSet(locP.first);
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