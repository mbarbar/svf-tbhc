//===- TypeBasedHeapCloning.cpp -- Type-based flow-sensitive heap cloning------------//

/*
 * TypeBasedHeapCloning.cpp
 *
 *  Created on: Oct 08, 2019
 *      Author: Mohamad Barbar
 */

#include "WPA/TypeBasedHeapCloning.h"

// TODO: maybe better to actually construct something.
const DIType *TypeBasedHeapCloning::undefType = static_cast<DIType *>(malloc(sizeof(DIType)));

void TypeBasedHeapCloning::analyze(SVFModule svfModule) {
    // TODO: unclear if this will need to change.
    FlowSensitive::analyze(svfModule);
}

void TypeBasedHeapCloning::initialize(SVFModule svfModule) {
    FlowSensitive::initialize(svfModule);

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
    assert(objToType.find(srcID) == objToType.end() && "TBHC: addr: already has a type?");

    // TODO: maybe tilde(getType...)
    const DIType *type = isHeapMemObj(srcID) ? undefType : getTypeFromMetadata(srcNode->getValue());
    objToType[srcID] = type;
    objToAllocation[srcID] = addr->getId();

    return changed;
}

bool TypeBasedHeapCloning::processDeref(const SVFGNode *stmt, const NodeID pId) {
    bool changed = false;
    PointsTo &pPt = getPts(pId);
    const PAGNode *pNode = pag->getPAGNode(pId);
    assert(pNode && "TBHC: dereferencing something not in PAG?");
    const DIType *t = getTypeFromMetadata(pNode->getValue());

    for (PointsTo::iterator oI = pPt.begin(); oI != pPt.end(); ++oI) {
        NodeID o = *oI;
        const DIType *tp = objToType[o];  // tp == t'

        NodeID prop;
        // Split into the three DEREF cases.
        if (tp == undefType) {
            // [DEREF-UNTYPED]
            prop = cloneObject(o, stmt, tilde(t));
        } else if (isBase(tilde(t), tp) || isVoid(tilde(t))) {
            // [DEREF-UP]
            prop = o;
        } else if (isBase(tp, tilde(t)) && tp != tilde(t)) {
            // [DEREF-DOWN]
            prop = cloneObject(o, stmt, tilde(t));
        } else {
            // Implicit FILTER.
            prop = 0;
        }

        // TODO: this has plenty of room for optimisation. Might be wiser to
        //       empty it out and make a new set or just have fewer branches.
        if (prop == 0) {
            // Not propagating in this case; remove.
            pPt.reset(o);
        } else {
            // If prop is the same as o, we're not doing anything!
            if (prop != o) {
                pPt.reset(o);
                pPt.set(prop);
            }
        }
    }

    return changed;
}

bool TypeBasedHeapCloning::processGep(const GepSVFGNode* edge) {
    return FlowSensitive::processGep(edge);
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
    assert(ptrType && ptrType->getTag() == dwarf::DW_TAG_pointer_type && "TBHC: trying to tilde a non-pointer");

    DIType *pointeeType = ptrType->getBaseType();
    return pointeeType;
}

NodeID TypeBasedHeapCloning::cloneObject(const NodeID o, const SVFGNode *cloneSite, const DIType *type) {
    // Dummy objects for clones are okay because tracking is done with maps.
    NodeID clone = pag->addDummyObjNode();

    // Clone's attributes.
    objToType[clone] = type;
    objToCloneSite[clone] = cloneSite->getId();
    // Same allocation site as the original object.
    objToAllocation[clone] = objToAllocation[o];

    // Tracking of object<->clone.
    objToClones[o].insert(clone);
    cloneToOriginalObj[clone] = o;

    backPropagateDumb(clone);

    return clone;
}

bool TypeBasedHeapCloning::isVoid(const DIType *type) const {
    return false;
}

bool TypeBasedHeapCloning::isBase(const llvm::DIType *a, const llvm::DIType *b) const {
    return dchg->isBase(a, b, true);
}

void TypeBasedHeapCloning::backPropagateDumb(NodeID o) {
    NodeID allocSite = objToAllocation[o];
    assert(allocSite != 0 && "TBHC: alloc for clone never set");
    SVFGNode *genericNode = svfg->getSVFGNode(allocSite);
    assert(genericNode != nullptr && "TBHC: Allocation site not found?");
    AddrSVFGNode *allocSiteNode = SVFUtil::dyn_cast<AddrSVFGNode>(allocSiteNode);
    assert(allocSiteNode != nullptr && "TBHC: Allocation site is not an Addr SVFG node?");

    if (getPts(allocSiteNode->getPAGDstNodeID()).test_and_set(o)) {
        // If o had never been to allocSite, need to re-propagate.
        propagate(&genericNode);
    }
}

