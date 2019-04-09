/*
 * TypeClone.cpp
 *
 *  Created on: Apr 09, 2019
 *      Author: Mohamad Barbar
 */

#include "WPA/TypeClone.h"
#include "WPA/WPAStat.h"

bool TypeClone::processAddr(const AddrSVFGNode* addr) {
    double start = stat->getClk();

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
        idToTypeMap[srcID] = NULL;
    } else {
        idToTypeMap[srcID] = pag->getPAGNode(srcID)->getType();
        assert(idToTypeMap[srcID] != NULL && "TypeClone: non-heap does not have a type?");
    }

    double end = stat->getClk();
    addrTime += (end - start) / TIMEINTERVAL;

    return changed;
}

bool TypeClone::processCopy(const CopySVFGNode* copy) {
    double start = stat->getClk();

    bool changed;

    Type *fromType, *toType;
    if (isCast(copy, &fromType, &toType)) {
        llvm::outs() << "fromType: " << *fromType << "\n";
        llvm::outs() << "toType: " << *toType << "\n";
    } else {
        bool changed = unionPts(copy->getPAGDstNodeID(), copy->getPAGSrcNodeID());
    }

    double end = stat->getClk();
    copyGepTime += (end - start) / TIMEINTERVAL;

    return changed;
}

bool TypeClone::isCast(const CopySVFGNode *copy, Type **fromType, Type **toType) const {
    bool cast = false;

    PAGNode *dstPagNode = pag->getPAGNode(copy->getPAGDstNodeID());
    if (dstPagNode->hasValue()) {
        const Value *dstVal = dstPagNode->getValue();
        const Instruction *dstInst = SVFUtil::dyn_cast<Instruction>(dstVal); // TODO: will it always be an inst?
        // TODO: why not copy->getInst()?
        if (const CastInst *castInst = SVFUtil::dyn_cast<CastInst>(dstInst)) {
            cast = true;
            *toType = castInst->getDestTy();
            *fromType = castInst->getSrcTy();
        }
    }

    return cast;
}

