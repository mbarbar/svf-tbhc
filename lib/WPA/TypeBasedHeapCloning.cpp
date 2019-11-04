//===- TypeBasedHeapCloning.cpp -- Type-based flow-sensitive heap cloning------------//

/*
 * TypeBasedHeapCloning.cpp
 *
 *  Created on: Oct 08, 2019
 *      Author: Mohamad Barbar
 */

// TODO: rename CloneObjNode -> ConstantCloneNode
// TODO: Deref function in rules
// TODO: cloneObject, use original always
// TODO: reference set for perf., not return set

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
}

void TypeBasedHeapCloning::finalize(void) {
    FlowSensitive::finalize();
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

    return changed;
}

bool TypeBasedHeapCloning::processDeref(const StmtSVFGNode *stmt, const NodeID pId) {
    bool changed = false;
    PointsTo &pPt = getPts(pId);
    PointsTo pNewPt;
    const PAGNode *pNode = pag->getPAGNode(pId);
    assert(pNode && "TBHC: dereferencing something not in PAG?");
    // TODO: this ternary op. is due to deficiency in tir's coverage.
    const DIType *tildet = getTypeFromMetadata(stmt->getInst() != nullptr ?
                                                     stmt->getInst()
                                                   : pNode->getValue());

    for (PointsTo::iterator oI = pPt.begin(); oI != pPt.end(); ++oI) {
        NodeID o = *oI;
        const DIType *tp = objToType[o];  // tp == t'

        NodeID prop = 0;
        bool filter = false;
        // Split into the three DEREF cases.
        if (tp == tildet) {
            // Early case for [DEREF-UP]
            prop = o;
        } else if (isVoid(tp) && !isVoid(tildet)) {
            // [DEREF-UNTYPED]
            prop = cloneObject(o, stmt, tildet);
        } else if (isBase(tildet, tp) || isVoid(tildet)) {
            // [DEREF-UP]
            prop = o;
        } else if (isBase(tp, tildet) && tp != tildet) {
            // [DEREF-DOWN]
            prop = cloneObject(o, stmt, tildet);
        } else {
            // Implicit FILTER.
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

bool TypeBasedHeapCloning::processGep(const GepSVFGNode* edge) {
    // Copy of that in FlowSensitive.cpp + some changes.
    const PointsTo& srcPts = getPts(edge->getPAGSrcNodeID());

    PointsTo tmpDstPts;
    for (PointsTo::iterator qi = srcPts.begin(); qi != srcPts.end(); ++qi) {
        NodeID q = *qi;
        if (isBlkObjOrConstantObj(q)) {
            tmpDstPts.set(q);
        } else {
            if (SVFUtil::isa<VariantGepPE>(edge->getPAGEdge())) {
                setObjFieldInsensitive(q);
                NodeID fiObj = getFIObjNode(q);
                tmpDstPts.set(fiObj);

                // TODO: check type!
                objToType[fiObj] = objToType.at(q);
            } else if (const NormalGepPE* normalGep = SVFUtil::dyn_cast<NormalGepPE>(edge->getPAGEdge())) {
                std::set<NodeID> fieldClones = getGepObjClones(q, normalGep->getLocationSet());
                for (std::set<NodeID>::iterator fcI = fieldClones.begin(); fcI != fieldClones.end(); ++fcI) {
                    NodeID fc = *fcI;
                    tmpDstPts.set(fc);

                    assert(objToType.find(q) != objToType.end() && "TBHC: GEP base is untyped?");
                    const DIType *t = objToType[q];
                    objToType[fc] = dchg->getFieldType(t, normalGep->getLocationSet().getOffset());
                    objToAllocation[fc] = objToAllocation[q];
                }
            } else {
                assert(false && "new gep edge?");
            }
        }
    }

    return unionPts(edge->getPAGDstNodeID(), tmpDstPts);
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
        mdNode = inst->getMetadata(SVFModule::tirMetadataName);
    } else if (const GlobalObject *go = SVFUtil::dyn_cast<GlobalObject>(v)) {
        mdNode = go->getMetadata(SVFModule::tirMetadataName);
    }

    if (mdNode == nullptr) {
        llvm::outs() << "TBHC: unannotated value\n";
        llvm::outs().flush();
        v->dump();
        return nullptr;
    }

    const DIType *type = SVFUtil::dyn_cast<DIType>(mdNode);

    if (type == nullptr) {
        llvm::outs() << "TBHC: no tir metadata found\n";
    }

    return dchg->getCanonicalType(type);
}

const DIType *TypeBasedHeapCloning::tilde(const DIType *generalType) const {
    const DIDerivedType *ptrType = SVFUtil::dyn_cast<DIDerivedType>(generalType);
    assert(ptrType
           && (ptrType->getTag() == dwarf::DW_TAG_pointer_type
               || ptrType->getTag() == dwarf::DW_TAG_reference_type)
           && "TBHC: trying to tilde a non-pointer");
    // TODO: we'll have to see if we need to consider rvalue_reference.

    DIType *pointeeType = ptrType->getBaseType();
    return pointeeType;
}

NodeID TypeBasedHeapCloning::cloneObject(NodeID o, const SVFGNode *cloneSite, const DIType *type) {
    // Check the desired clone doesn't already exist.
    // TODO: this can be cached better.
    std::set<NodeID> clones = objToClones[o];
    for (std::set<NodeID>::iterator cloneI = clones.begin(); cloneI != clones.end(); ++cloneI) {
        NodeID clone = *cloneI;
        if (objToCloneSite[clone] == cloneSite->getId() && objToType[clone] == type) {
            return clone;
        }
    }

    // CloneObjs for standard objects, CloneGepObjs for GepObjs, CloneFIObjs for FIObjs.
    const PAGNode *obj = pag->getPAGNode(o);
    NodeID clone;
    if (const GepObjPN *gepObj = SVFUtil::dyn_cast<GepObjPN>(obj)) {
        clone = pag->addCloneGepObjNode(gepObj->getMemObj(), gepObj->getLocationSet());
        // The base needs to know about this guy.
        objToGeps[gepObj->getMemObj()->getSymId()].insert(clone);
    } else if (const FIObjPN *fiObj = SVFUtil::dyn_cast<FIObjPN>(obj)) {
        clone = pag->addCloneFIObjNode(fiObj->getMemObj());
    } else {
        // Could be a dummy object.
        clone = pag->addCloneObjNode();
    }

    // We also need to clone the GEP objects underneath o.
    /*
    std::set<NodeID> geps = objToGeps[o];
    for (std::set<NodeID>::iterator gepI = geps.begin(); gepI != geps.end(); ++gepI) {
        NodeID gep = *gepI;
        // TODO: why is this happening?
        if (o == gep) continue;
        NodeID gepClone = cloneObject(gep, cloneSite, objToType[gep]);
        objToGeps[clone].insert(gepClone);
    }
    */

    // Clone's attributes.
    objToType[clone] = type;
    objToCloneSite[clone] = cloneSite->getId();
    // Same allocation site as the original object.
    objToAllocation[clone] = objToAllocation[o];

    // Tracking of object<->clone.
    if (isClone(o)) o = cloneToOriginalObj[o];
    objToClones[o].insert(clone);
    cloneToOriginalObj[clone] = o;

    backPropagateDumb(clone);

    return clone;
}

bool TypeBasedHeapCloning::isVoid(const DIType *type) const {
    // TODO: not sufficient.
    return type == nullptr;
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
    ObjPN *objNode = SVFUtil::dyn_cast<ObjPN>(node);
    assert(objNode);

    std::set<NodeID> geps;
    if (objNode->getMemObj()->isFieldInsensitive()) {
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
        NodeID gep = pag->getGepObjNode(base, ls);
        objToGeps[base].insert(gep);
        geps.insert(gep);
    }

    return geps;
}

void TypeBasedHeapCloning::backPropagateDumb(NodeID o) {
    NodeID allocSite = objToAllocation[o];
    assert(allocSite != 0 && "TBHC: alloc for clone never set");
    SVFGNode *genericNode = svfg->getSVFGNode(allocSite);
    assert(genericNode != nullptr && "TBHC: Allocation site not found?");
    AddrSVFGNode *allocSiteNode = SVFUtil::dyn_cast<AddrSVFGNode>(genericNode);
    assert(allocSiteNode != nullptr && "TBHC: Allocation site is not an Addr SVFG node?");

    if (getPts(allocSiteNode->getPAGDstNodeID()).test_and_set(o)) {
        // If o had never been to allocSite, need to re-propagate.
        propagate(&genericNode);
    }
}

