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

bool TypeBasedHeapCloning::processDeref(const SVFGNode *stmt, const NodeID ptrId) {
    bool changed = false;
    PointsTo &ptrPt = getPts(ptrId);

    return changed;
}

bool TypeBasedHeapCloning::processGep(const GepSVFGNode* edge) {
    return FlowSensitive::processGep(edge);
}

bool TypeBasedHeapCloning::processLoad(const LoadSVFGNode* load) {
    return FlowSensitive::processLoad(load);
}

bool TypeBasedHeapCloning::processStore(const StoreSVFGNode* store) {
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

    return type;
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

    // Tracking of object<->clone.
    objToClones[o].insert(clone);
    cloneToOriginalObj[clone] = o;

    return clone;
}

bool isVoid(const DIType *type) const {
    return false;
}

