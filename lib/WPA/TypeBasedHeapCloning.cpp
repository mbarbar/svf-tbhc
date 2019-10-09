//===- TypeBasedHeapCloning.cpp -- Type-based flow-sensitive heap cloning------------//

/*
 * TypeBasedHeapCloning.cpp
 *
 *  Created on: Oct 08, 2019
 *      Author: Mohamad Barbar
 */

#include "WPA/TypeBasedHeapCloning.h"

// TODO: maybe better to actually construct something.
const llvm::DIType *TypeBasedHeapCloning::undefType = static_cast<llvm::DIType *>(malloc(sizeof(llvm::DIType)));

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

    /// TODO: see FlowSensitive::processAddr.
    if (isFieldInsensitive(srcID)) {
        srcID = getFIObjNode(srcID);
    }

    bool changed = addPts(dstID, srcID);

    // We should not have any type, not even undefined.
    assert(objToType.find(srcID) == objToType.end() && "TBHC: addr: already has a type?");

    const llvm::DIType *type = isHeapMemObj(srcID) ? undefType : getTypeFromMetadata(srcNode->getValue());
    objToType[srcID] = type;
    objToAllocation[srcID] = addr->getId();

    return changed;
}

const llvm::DIType *TypeBasedHeapCloning::getTypeFromMetadata(const Value *v) const {
    assert(v != nullptr && "TBHC: trying to get metadata from nullptr!");

    const llvm::MDNode *mdNode = nullptr;
    if (const Instruction *inst = SVFUtil::dyn_cast<llvm::Instruction>(v)) {
        mdNode = inst->getMetadata(SVFModule::tirMetadataName);
    } else if (const GlobalObject *go = SVFUtil::dyn_cast<llvm::GlobalObject>(v)) {
        mdNode = go->getMetadata(SVFModule::tirMetadataName);
    }

    const llvm::DIType *type = SVFUtil::dyn_cast<llvm::DIType>(mdNode);

    if (type == nullptr) {
        llvm::outs() << "TBHC: no tir metadata found\n";
    }

    return type;
}

